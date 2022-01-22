#include <NTPClient.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <Wire.h> // Library for I2C communication
#include <SPI.h>  // not used here, but needed to prevent a RTClib compile error
#include "RTClib.h"
#include <ESP8266WiFiMulti.h>

#define U_PART U_FS
#include "FS.h"
#include <ArduinoJson.h>
#include <ESP8266mDNS.h>
#include <ArduinoOTA.h>
#include <ESP8266WebServer.h>
#include <uri/UriBraces.h>
#include <IRremote.hpp>

ESP8266WiFiMulti wifiMulti;
RTC_DS3231 RTC;
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 25200 );

DynamicJsonDocument wifiConf(512);
DynamicJsonDocument commonConf(512);
DynamicJsonDocument pinoutConf(384);
DynamicJsonDocument schedulesConf(1536);

int prevTime = 0;
unsigned long prevTimer = 0;
uint8_t prevCommand = 0;
ESP8266WebServer server(80);
String getValueByIndex(String data, char separator, int index)
{
    int found = 0;
    int strIndex[] = { 0, -1 };
    int maxIndex = data.length() - 1;

    for (int i = 0; i <= maxIndex && found <= index; i++) {
        if (data.charAt(i) == separator || i == maxIndex) {
            found++;
            strIndex[0] = strIndex[1] + 1;
            strIndex[1] = (i == maxIndex) ? i+1 : i;
        }
    }
    return found > index ? data.substring(strIndex[0], strIndex[1]) : "";
}

boolean updateConfig(String confName, DynamicJsonDocument& jsonDoc){
  boolean res = true;
  File file = SPIFFS.open(confName, "w");
 
  if (!file) {
    Serial.println("Error opening file for writing");
    return false;
  }
  
  String jsonStr;
  serializeJson(jsonDoc, jsonStr);
  
  int bytesWritten = file.print(jsonStr); 
  if (bytesWritten > 0) {
    Serial.println("");
    Serial.println("File Has Been Updated"); 
  } else {
    res = false;
    Serial.println("Failed for update envionrment");
  }
  file.close();
  return res;
}
bool initSPIFFS() {
  bool res = true;
  if (!SPIFFS.begin()) {
    Serial.println("An error has occurred while mounting SPIFFS");
    res = false;
  }
  Serial.println("SPIFFS mounted successfully");
  return res;
}

const char* getAuth(String entity) {
  JsonObject auth = commonConf["auth"].as<JsonObject>();
  const char* res = auth[entity];
  return res;
}

String setPinOut(boolean isTurnOn, String pinName) {
  if(pinName == "all") {
    pinoutConf["d4"]["value"] = isTurnOn;
    pinoutConf["d5"]["value"] = isTurnOn;
    pinoutConf["d6"]["value"] = isTurnOn;
    pinoutConf["d7"]["value"] = isTurnOn;

    digitalWrite(pinoutConf["d4"]["io_pin"], !isTurnOn);//inverse
    digitalWrite(pinoutConf["d5"]["io_pin"], !isTurnOn);//inverse
    digitalWrite(pinoutConf["d6"]["io_pin"], !isTurnOn);//inverse
    digitalWrite(pinoutConf["d7"]["io_pin"], !isTurnOn);//inverse
  } else {
    pinoutConf[pinName]["value"] = isTurnOn;
    digitalWrite(pinoutConf[pinName]["io_pin"], !isTurnOn);//inverse
  }

  if (updateConfig("/pinout.config.json", pinoutConf)) {
   return "{\"success\": true}";
  } else {
    return "{\"success\": false}";
  }
}

bool readConf(String confName,DynamicJsonDocument& jsonDoc) {
  boolean res = true;
  File configFile = SPIFFS.open(confName, "r");
  if (!configFile) {
    Serial.println("- Failed to Open from Flash memory.");
    return false;
  }
  Serial.println("- Success read file from Flash Memory.");
  while ( configFile && configFile.available()) { 
    size_t size = configFile.size();
    if ( size == 0 ) {
      Serial.println("environment file is empty");
      res = false;
    } else {
      String conf = configFile.readString();      
      auto error = deserializeJson(jsonDoc, conf);
      if (error) {
        Serial.print(F("- deserializeJson() failed with code "));
        Serial.println(error.c_str());
        res = false;
      } else {
        Serial.print(F("- deserializeJson success "));
        serializeJson(jsonDoc, Serial);
      }      
    }
  }   

  Serial.println("\n\n");
  configFile.close();
  return res;
}

void setTimeFromNTP(){
  timeClient.update();

  unsigned long epochTime = timeClient.getEpochTime(); 
  delay(500);
 
  struct tm *ptm = gmtime ((time_t *)&epochTime); 
  int monthDay = ptm->tm_mday;
  int currentMonth = ptm->tm_mon+1;
  int currentYear = ptm->tm_year+1900;

  String currentDate = String(currentYear) + "-" + String(currentMonth) + "-" + String(monthDay) + " " +
    String(timeClient.getHours())+ ":" + String(timeClient.getMinutes()) + ":" +String(timeClient.getSeconds());
  
  Serial.print("Time From NTP Server: ");
  Serial.println(currentDate);
  RTC.adjust(DateTime(
    currentYear, 
    currentMonth, 
    monthDay, 
    timeClient.getHours(), 
    timeClient.getMinutes(), 
    timeClient.getSeconds()
  ));
  
  DateTime now = RTC.now();  Serial.print("Time From DS3231 : ");
  Serial.print(now.year(), DEC); Serial.print('-');  Serial.print(now.month(), DEC);  Serial.print('-');
  Serial.print(now.day(), DEC);  Serial.print(" ");  Serial.print(now.hour(), DEC);  Serial.print(':');
  Serial.print(now.minute(), DEC);  Serial.print(':'); Serial.print(now.second(), DEC);  Serial.println();
}

void setup(void) {
  Serial.begin(115200);
  Serial.println("");
  Serial.println("Device Starting Up. \n");
  delay(1000);
  Wire.begin();
  delay(1000);
  IrReceiver.begin(0);
  printActiveIRProtocols(&Serial);

  
  if (! RTC.begin()){
    Serial.println("Couldn't find RTC");
    Serial.flush();
    abort();
    return;
  }

  bool isSuccess = initSPIFFS();
  if (isSuccess) {
    Serial.println("Initialize Saved WiFi Configuration ...");
    if (readConf("/wifi.config.json", wifiConf)) {
      JsonArray array = wifiConf["saved_ssid"].as<JsonArray>();
      for(JsonObject obj : array) {
          const char* ssid = obj["ssid"];
          const char* password = obj["password"];
          wifiMulti.addAP(ssid, password);
          Serial.print("ssid: ");Serial.print(ssid); Serial.print(" - password: "); Serial.println(password);
      }
    }

    Serial.println("Initialize Pin Out Configuration ...");
    if(readConf("/pinout.config.json", pinoutConf)) {
      JsonObject root = pinoutConf.as<JsonObject>();
      for (JsonPair kv : root) {
          JsonObject pinout = kv.value().as<JsonObject>();
          pinMode(pinout["io_pin"].as<int>(), OUTPUT);
          digitalWrite(pinout["io_pin"].as<int>(), !(pinout["value"].as<boolean>() ? HIGH : LOW));          
      }
    }

    Serial.println("Initialize Common Configuration ...");
    if (readConf("/common.config.json", commonConf)) {
      const char* device_type = commonConf["device_type"]; 
    }
    readConf("/schedules.json", schedulesConf);
  } 
  Serial.print("Connecting to Wifi ...");
  int trying = 0;
  while (wifiMulti.run() != WL_CONNECTED && trying < 10) {
    delay(1000);
    Serial.print(".");
    trying++;
  }
  Serial.println("");
  if (wifiMulti.run() == WL_CONNECTED) {
    Serial.println("");
    Serial.println("WiFi connected");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    Serial.print("chip Id: ");
    Serial.println(String(ESP.getChipId()).c_str());
    
    timeClient.begin();
    setTimeFromNTP(); 
  } else {
    Serial.println("Failed Connected to Wifi");
  }
  
  ArduinoOTA.begin();
  server.on(UriBraces("/api/turn/{}/{}"), HTTP_GET, []() {
    if (!server.authenticate(getAuth("username"), getAuth("password"))) {
      return server.requestAuthentication();
    }
    boolean turnMode = server.pathArg(0) == "on";
    String ioPin = server.pathArg(1);
    
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json", setPinOut(turnMode, ioPin));
  });
  
  server.on("/api/schedules", HTTP_GET, []() {
    if (!server.authenticate(getAuth("username"), getAuth("password"))) {
      return server.requestAuthentication();
    }
    
    String schedulesConfig;
    serializeJson(schedulesConf, schedulesConfig);
    
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json", schedulesConfig);
  });

  server.on("/api/schedules", HTTP_POST, [](){
    DynamicJsonDocument buffDoc(1536);
    String result;
    auto error = deserializeJson(buffDoc, server.arg("plain"));
    if (error) {
      Serial.print(F("- deserializeJson() failed with code "));
      Serial.println(error.c_str());
      result = "{ \"success\": false, \"error\": "+String(error.c_str())+" }";
    } else {
      Serial.print(F("- deserializeJson success "));
      schedulesConf = buffDoc;
      if (updateConfig("/schedules.json", schedulesConf)) {
       result =  "{\"success\": true}";
      } else {
        result =  "{\"success\": false}";
      }
    }     
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json", result);
  });
  server.begin();
}

void loop(void) {
  ArduinoOTA.handle();
  server.handleClient();
  JsonArray arraySchedule = schedulesConf["schedules"].as<JsonArray>();
  DateTime now = RTC.now();
  
  unsigned long current = millis();
  if (IrReceiver.decode()) {
      IrReceiver.resume();
      if ((current - prevTimer) > 100) {
        prevTimer = millis();
        prevCommand = 0;
      }
      if (IrReceiver.decodedIRData.address == 0x80) {
          uint8_t command = IrReceiver.decodedIRData.command;
          unsigned long diff = current - prevTimer;

          if (diff < 100 && prevCommand == command) {
            return;
          }
          String pinName = "";
          boolean prevValue = false;
          if (IrReceiver.decodedIRData.command == 0xA) {
            pinName = "d4";            
          } else if (IrReceiver.decodedIRData.command == 0x1B) {
            pinName = "d5";
          }else if (IrReceiver.decodedIRData.command == 0x1F) {
            pinName = "d6";
          }else if (IrReceiver.decodedIRData.command == 0xC) {
            pinName = "d7";
          }else if (IrReceiver.decodedIRData.command == 0xD) {
            pinName = "all";
            prevValue = true;
          }else if (IrReceiver.decodedIRData.command == 0xE) {
            pinName = "all";
          }

          if(pinName != "") {
            int ioPin = pinoutConf[pinName]["io_pin"];
            if (pinName != "all")
              prevValue = !(pinoutConf[pinName]["value"].as<boolean>());
              
            setPinOut(prevValue, pinName);
            pinName = "";
          }
          
          prevCommand = command;
          prevTimer = millis();
      }
  }
  
  
  for (JsonObject obj: arraySchedule) {
    int sHour = getValueByIndex(obj["time"].as<String>(), ':', 0).toInt();
    int sMinute = getValueByIndex(obj["time"].as<String>(), ':', 1).toInt();

    if (sHour == now.hour() && sMinute == now.minute()) {
      JsonArray pinIds = obj["pin_ids"].as<JsonArray>();
      for (String id : pinIds) {
        JsonObject currentPin = pinoutConf[id].as<JsonObject>();
        if (!currentPin)
          continue;
        
        if (currentPin["value"].as<boolean>() == obj["value"].as<boolean>()){
          continue;
        } else {
          Serial.println("Trigger Condition for pin " + id);
          setPinOut(obj["value"].as<boolean>(), id);
          
          Serial.print("currentPin :");
          serializeJson(currentPin, Serial);
          Serial.print("\nObj :");
          serializeJson(obj, Serial);
        }
      }        
    }
  }
}

    
