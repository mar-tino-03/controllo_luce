#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ESP8266mDNS.h>
#include <ArduinoOTA.h>

#include "passWords.h"

IPAddress local_IP(192, 168, 1, 170);
IPAddress gateway(192, 168, 1, 1);

IPAddress subnet(255, 255, 0, 0);
IPAddress primaryDNS(8, 8, 8, 8);   //optional
IPAddress secondaryDNS(8, 8, 4, 4); //optional

#include <FS.h>
#include <Arduino_JSON.h>


#include <Wire.h>
#include <Adafruit_BusIO_Register.h>
#include <Adafruit_I2CDevice.h>
#include <Adafruit_I2CRegister.h>
#include <Adafruit_SPIDevice.h>
#include <Adafruit_AHTX0.h>
Adafruit_AHTX0 aht;
sensors_event_t humidity, temp;


#define PinLed 2        //led integrato
#define PinVoltage 14   //D5

#define MinLux 8
#define MaxLux 680

typedef struct {
  float tmp;
  float hum;
  float lux;
  float bat;
} sensor;
sensor analog;

bool ledState = 0;
bool stato;

String msg;

unsigned long oldT;

// Create AsyncWebServer object on port 80
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");


void notifyClients() {

  ws.textAll(msg);
}

void handleWebSocketMessage(void *arg, uint8_t *data, size_t len) {
  AwsFrameInfo *info = (AwsFrameInfo*)arg;
  if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
    data[len] = 0;
    if (strcmp((char*)data, "toggle") == 0) {
      ledState = !ledState;
      digitalWrite(PinLed, ledState);
      notifyClients();
    }
  }
}

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type,
             void *arg, uint8_t *data, size_t len) {
  switch (type) {
    case WS_EVT_CONNECT:
      Serial.printf("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
      notifyClients();
      break;
    case WS_EVT_DISCONNECT:
      Serial.printf("WebSocket client #%u disconnected\n", client->id(), client->remoteIP().toString().c_str());
      break;
    case WS_EVT_DATA:
      handleWebSocketMessage(arg, data, len);
      break;
    case WS_EVT_PONG:
    case WS_EVT_ERROR:
      break;
  }
}

void initWebSocket() {
  ws.onEvent(onEvent);
  server.addHandler(&ws);
}

String processor(const String& var){
  Serial.println(var);
  if(var == "STATE"){
    if (ledState){
      return "ON";
    }
    else{
      return "OFF";
    }
  }
  return String();
}

void setup(){
  // Serial port for debugging purposes
  Serial.begin(115200);
  Serial.print("\n\n");

  pinMode(PinLed, OUTPUT);
  digitalWrite(PinLed, LOW);

  pinMode(PinVoltage, OUTPUT);
  digitalWrite(PinVoltage, HIGH);

  if(!SPIFFS.begin()){
    Serial.println("An Error has occurred while mounting SPIFFS");
    return;
  }
  else{
    Serial.println("SPIFFS aviable");
  }

  String dir_name="{";             //{{"name":1},{"name":2},{"name":3}}
  Dir dir = SPIFFS.openDir("");
  while (dir.next()) {
    Serial.println("file: " + dir.fileName());
    dir_name = "{" + dir_name + "\"" + dir.fileName() + "\"},"; 
  }
  dir_name = dir_name + "}";
  JSONVar json_dir_name = JSON.parse(dir_name);
  for(int i=0; i<json_dir_name.length(); i++){

    String pagina = JSON.stringify(json_dir_name[i]);
    Serial.println("elemento " + String(i) + " : " + pagina);
    
  }
  
  
  if (!WiFi.config(local_IP, gateway, subnet, primaryDNS, secondaryDNS)) {
    Serial.println("STA Failed to configure");
  }

  // Connect to Wi-Fi
  WiFi.mode(WIFI_STA);
  WiFi.begin(MY_SSID, MY_PASS);
  Serial.println("connetting");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  ArduinoOTA.setPassword(OTA_PASS);
  OTA_begin();

  if (MDNS.begin(DNS_NAME)) {                                    // mDNS
    Serial.println("ok dns");
  }

  initWebSocket();


  server_on();
  
  // Start server
  server.begin();

  Wire.setClock(400000);
  Wire.begin(4,5);
  aht.begin();
}

void server_on(){
  
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    //request->send_P(200, "text/html", index_html, processor);
    request->send(SPIFFS, "/index.html", String(), false);
  });

  server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request){
    //request->send_P(200, "text/html", index_html, processor);
    request->send(SPIFFS, "/style.css", String(), false);
  });
}

void loop() {
  MDNS.update();
  ArduinoOTA.handle();
  ws.cleanupClients();

  if(millis() > oldT + 1500){

    switch(stato){
      case 0:
        analog.bat = analogRead(0) / 204.6;
        digitalWrite(PinVoltage, LOW);

        Serial.println(analog.bat);
      break;
      case 1:
        analog.lux = constrain(map(analogRead(0), MinLux, MaxLux, 0, 100),0,100);
        aht.getEvent(&humidity, &temp);
        analog.tmp = temp.temperature;
        analog.hum = humidity.relative_humidity;

        digitalWrite(PinVoltage, HIGH);

        msg = "{\"lux\": "+String(analog.lux)+",\"bat\": "+String(analog.bat)
               + ",\"hum\": "+String(analog.hum)+",\"tmp\": "+String(analog.tmp)+"}";  //{{"name":1,"name":2}...

        Serial.println(msg);

        notifyClients();
      break;
    }
    stato = !stato;
    oldT = millis();
  }
}

void OTA_begin(){  
ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else { // U_FS
      type = "filesystem";
    }

    // NOTE: if updating FS this would be the place to unmount FS using FS.end()
    Serial.println("Start updating " + type);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      Serial.println("Auth Failed");
    } else if (error == OTA_BEGIN_ERROR) {
      Serial.println("Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
      Serial.println("Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      Serial.println("Receive Failed");
    } else if (error == OTA_END_ERROR) {
      Serial.println("End Failed");
    }
  });
  ArduinoOTA.begin();
  Serial.println("ok ota");
}
