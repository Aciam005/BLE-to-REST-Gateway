#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include "esp_heap_caps.h"

#define bleServerName "Echipa 7"

#define SERVICE_UUID "972bfdd4-5ab9-4dce-a615-91699b2802a0"
#define CHARACTERISTIC_UUID "ec721aca-b798-4425-863e-4d4f8b4f02bf"

/*
const char* ssid = "your_SSID";
const char* password = "your_PASSWORD";
*/

const char* apiURL = "http://proiectia.bogdanflorea.ro/api/better-call-saul/characters";
String dealDetailsApiURL = "http://proiectia.bogdanflorea.ro/api/south-park-episodes/episode?id=";

BLECharacteristic characteristic(
  CHARACTERISTIC_UUID,
  BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_NOTIFY
);
BLEDescriptor *characteristicDescriptor = new BLEDescriptor(BLEUUID((uint16_t)0x2902));

bool deviceConnected = false;
const char* teamId = "A38";

void sendInSouthPark(BLECharacteristic* characteristic, const String& data) {
  const size_t southparkSize = 512;
  size_t dataSize = data.length();
  size_t offset = 0;

  while (offset < dataSize) {
    size_t len = min(southparkSize, dataSize - offset);
    String southpark = data.substring(offset, offset + len);
    characteristic->setValue(southpark.c_str());
    characteristic->notify();
    offset += len;
    delay(20);
  }
}

class MyServerCallbacks: public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) {
    deviceConnected = true;
    Serial.println("Device connected");
  };
  void onDisconnect(BLEServer* pServer) {
    deviceConnected = false;
    Serial.println("Device disconnected");
  }
};

class CharacteristicsCallbacks: public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *characteristic) {
    std::string data = characteristic->getValue();
    Serial.println(data.c_str());
    DynamicJsonDocument doc(256);
    DeserializationError error = deserializeJson(doc, data);

    if (error) {
      Serial.print("deserializeJson() failed: ");
      Serial.println(error.c_str());
      return;
    }

    String action = doc["action"];
    Serial.println(action);
    Serial.println(teamId);

    if (action == "getNetworks") {
      int n = WiFi.scanNetworks();
      for (int i = 0; i < n; i++) {
        DynamicJsonDocument networkDoc(256);
        JsonObject network = networkDoc.to<JsonObject>();
        network["ssid"] = WiFi.SSID(i);
        network["strength"] = WiFi.RSSI(i);
        network["encryption"] = WiFi.encryptionType(i);
        network["teamId"] = teamId;
        String response;
        serializeJson(network, response);
        sendInSouthPark(characteristic, response);
      }
    } 
    else if (action == "connect") {
      String ssid = doc["ssid"].as<String>();
      String password = doc["password"].as<String>();
      Serial.print("Connecting to SSID: ");
      Serial.println(ssid);

      WiFi.begin(ssid.c_str(), password.c_str());

      int maxRetries = 10;
      while (WiFi.status() != WL_CONNECTED && maxRetries > 0) {
        delay(1000);
        Serial.print(".");
        maxRetries--;
      }
      Serial.println();

      bool connected = (WiFi.status() == WL_CONNECTED);
      Serial.print("Connected: ");
      Serial.println(connected);

      DynamicJsonDocument responseDoc(256);
      responseDoc["ssid"] = ssid;
      responseDoc["connected"] = connected;
      responseDoc["teamId"] = teamId;


      String response;
      serializeJson(responseDoc, response);
      sendInSouthPark(characteristic, response);
    }
    else if (action == "getData") {   //aici este problema. de aceea apare null la image. de comparat cu bflorea_btc_bob_1 linia 169?!

  
      if (WiFi.status() == WL_CONNECTED) {
        HTTPClient http;
        http.begin(apiURL);
        int httpCode = http.GET();

        Serial.print("HTTP Response code: ");
        Serial.println(httpCode);

        int redirectCount = 0;
        while (httpCode == HTTP_CODE_MOVED_PERMANENTLY || httpCode == HTTP_CODE_FOUND || httpCode == HTTP_CODE_SEE_OTHER) {
          if (redirectCount > 5) {
            Serial.println("Too many redirects");
            break;
          }
          String newLocation = http.header("Location");
          Serial.print("Redirecting to: ");
          Serial.println(newLocation);
          http.end();

          if (newLocation.length() > 0) {
            http.begin(newLocation);
            httpCode = http.GET();
            Serial.print("HTTP Response code after redirect: ");
            Serial.println(httpCode);
            redirectCount++;
          } else {
            break;
          }
        }

        if (httpCode > 0) {
          String payload = http.getString();
          Serial.println("HTTP Response payload: ");
          Serial.println(payload);

          if (payload.length() == 0) {
            Serial.println("Received empty payload");
            return;
          }

          DynamicJsonDocument apiResponseDoc(4096);
          DeserializationError error = deserializeJson(apiResponseDoc, payload);

          if (error) {
            Serial.print("deserializeJson() failed: ");
            Serial.println(error.c_str());
            return;
          }

          JsonArray apiResponseArray = apiResponseDoc.as<JsonArray>();

          for (JsonObject apiRecord : apiResponseArray) {
            DynamicJsonDocument recordDoc(4096); // Adjusted memory size
            recordDoc["char_id"] = apiRecord["char_id"];
            recordDoc["name"] = apiRecord["name"];
            recordDoc["img"] = apiRecord["img"];
            recordDoc["teamId"] = teamId;

            String response;
            serializeJson(recordDoc, response);
            Serial.println("RESPONSE " + response);
            sendInSouthPark(characteristic, response);
          }
        } else {
          Serial.print("Error on HTTP request: ");
          Serial.println(httpCode);
        }
        http.end();
      } else {
        Serial.println("WiFi not connected");
      }
      
    } else if (action == "getDetails") {
      if (WiFi.status() == WL_CONNECTED) {
        String id = doc["char_id"].as<String>();
        String detailsUrl = dealDetailsApiURL + id;

        HTTPClient http;
        http.begin(detailsUrl);
        int httpCode = http.GET();

        Serial.print("HTTP Response code: ");
        Serial.println(httpCode);

        int redirectCount = 0;
        while (httpCode == HTTP_CODE_MOVED_PERMANENTLY || httpCode == HTTP_CODE_FOUND || httpCode == HTTP_CODE_SEE_OTHER) {
          if (redirectCount > 5) {
            Serial.println("Too many redirects");
            break;
          }
          String newLocation = http.header("Location");
          Serial.print("Redirecting to: ");
          Serial.println(newLocation);
          http.end(); 

          if (newLocation.length() > 0) {
            http.begin(newLocation);
            httpCode = http.GET();
            Serial.print("HTTP Response code after redirect: ");
            Serial.println(httpCode);
            redirectCount++;
          } else {
            break;
          }
        }

        if (httpCode > 0) {
          String payload = http.getString();
          Serial.println("HTTP Response payload: ");
          Serial.println(payload);

          if (payload.length() == 0) {
            Serial.println("Received empty payload");
            return;
          }

          DynamicJsonDocument apiResponseDoc(4096);
          DeserializationError error = deserializeJson(apiResponseDoc, payload);

          if (error) {
            Serial.print("deserializeJson() failed: ");
            Serial.println(error.c_str());
            return;
          }

          JsonObject apiRecord = apiResponseDoc.as<JsonObject>();
          DynamicJsonDocument responseDoc(4096);
              responseDoc["char_id"] = apiRecord["char_id"];
              responseDoc["name"] = apiRecord["name"]; // schimbat title cu name
              responseDoc["birthday"] = apiRecord["birthday"];
              //occupation
              responseDoc["occupation"] = apiRecord["occupation"];
              responseDoc["img"] = apiRecord["img"]; //schimbat thumb cu image
              responseDoc["status"] = apiRecord["status"];
              responseDoc["nickname"] = apiRecord["nickname"];
              //appearance
              responseDoc["appearance"] = apiRecord["appearance"];
              responseDoc["portrayed"] = apiRecord["portrayed"];
        
          String response;
          serializeJson(responseDoc, response);
          Serial.println("Response payload: ");
          Serial.println(response);
          sendInSouthPark(characteristic, response);
        } else {
          Serial.print("Error on HTTP request: ");
          Serial.println(httpCode);
        }
        http.end();
      } else {
        Serial.println("WiFi not connected");
      }
    }
  }
};

void setup() {
  Serial.begin(115200);
  BLEDevice::init(bleServerName);
  WiFi.mode(WIFI_STA);
  BLEServer *pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());
  BLEService *bleService = pServer->createService(SERVICE_UUID);
  bleService->addCharacteristic(&characteristic);  
  characteristic.addDescriptor(characteristicDescriptor);
  characteristic.setCallbacks(new CharacteristicsCallbacks());
  bleService->start();
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pServer->getAdvertising()->start();
  Serial.println("Waiting for a client connection to notify...");
}

void loop() {
}
