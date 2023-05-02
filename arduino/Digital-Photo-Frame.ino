/*
Digital Photo Frame
Authors: Emily Rodrigues, Lazaro Ramos

For ECE-692 @ NJIT Spring 2023
Professor: Jean Walker-Soler

Description: This program will enable the viewing of JPEG photos on
a ST7735 display using an ESP32 as the microcontroller from two
different sources, either a MicroSD card, or a Server serving the
photos as HTTP requests. The program enables the modification of
settings through a companion BLE enabled app. With the Android app
the user can turn on/off the display, adjust the interval in between
photo changes, as well as changing the Wifi credentials, and server
details.

References:
Server and Wifi mode: https://www.instructables.com/Face-Aware-OSD-Photo-Frame/
SD card mode: https://github.com/bitbank2/JPEGDEC/
App: https://community.appinventor.mit.edu/t/ble-esp32-bluetooth-send-receive-arduino-ide-multitouch/1980/49
*/

#include <Arduino_GFX_Library.h>
#include <esp_task_wdt.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <SD.h>
#include "JpegFunc.h"
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLE2902.h>

/* BLE UUIDs */
#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

#define GFX_BL 14 // Backlight pin

bool debug = true;
bool mode_WIFI = true;
bool mode_SD = false;

/* GFX display settings */
Arduino_DataBus *bus = new Arduino_ESP32SPI(33 /* DC */, 32 /* CS */, 5 /* SCK */, 18 /* MOSI */, 19 /* MISO */, VSPI /* spi_num */);
Arduino_GFX *gfx = new Arduino_ST7735(bus, 27 /* RST */, 1 /* rotation */, false /* IPS */,
  128 /* width */, 160 /* height */,
  0 /* col offset 1 */, 0 /* row offset 1 */,
  0 /* col offset 2 */, 0 /* row offset 2 */,
  false /* BGR */); // END GFX Settings

WiFiClient client;
HTTPClient http;

static unsigned long next_show_millis_wifi = 0; // For the millis()
static unsigned long next_show_millis_sd = 0;
unsigned long interval = 1000; // Photo change interval

/* WiFi settings */
char SSID_NAME[33] = "";
char SSID_PASSWORD[64] = "";
char HTTP_HOST[64] = "";                /* Your HTTP photo server host name */
uint16_t HTTP_PORT = 8080;                        /* Your HTTP photo server port */
const char *HTTP_PATH_TEMPLATE = "/OSDPhoto?w=%d&h=%d"; /* Your HTTP photo server URL path template */
const uint16_t HTTP_TIMEOUT = 30000; // in ms, wait a while for server processing

char http_path[1024];

/* Pixel drawing callback */
static int jpegDrawCallback(JPEGDRAW *pDraw) {
  gfx->draw16bitRGBBitmap(pDraw->x, pDraw->y, pDraw->pPixels, pDraw->iWidth, pDraw->iHeight);
  return 1;
}

static int jpegDrawCallbackSD(JPEGDRAW *pDraw) {
  gfx->draw16bitBeRGBBitmap(pDraw->x, pDraw->y, pDraw->pPixels, pDraw->iWidth, pDraw->iHeight);
  return 1;
}

BLEServer* pServer = NULL;
BLECharacteristic* pCharacteristic = NULL;
bool deviceConnected = false;
bool oldDeviceConnected = false;

/* BLE server callback */
class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;
    };

    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
    }
};

String BLE_incoming_value;
String BLE_return_value = "";

/* BLE receiving callback */
class MyCallbacks: public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) {
    std::string value = pCharacteristic->getValue();
    if(value.length() > 0) {
      BLE_incoming_value = "";
      for(int i = 0; i < value.length(); i++) {
        BLE_incoming_value = BLE_incoming_value + value[i];          
      }
      BLE_return_value = BLE_return_value + BLE_incoming_value;
    }
    decodeBluetoothStream(BLE_return_value);
    BLE_return_value = "";
  }
};

/* BLE variables to decode incoming stream */
bool wifi_ssid = false;
bool wifi_pass = false;
bool wifi_host = false;
bool wifi_port = false;
bool found_split = false;
bool stream_is_mode = false;
bool stream_is_interval = false;
bool save_WIFI_settings = false;
String str_wifi_ssid = "";
String str_wifi_pass = "";
String str_wifi_host = "";
String str_wifi_port = "";
String str_interval = "";
uint16_t int_wifi_port = 0;

void decodeBluetoothStream(String incoming) {
  for (int i = 0; i < incoming.length(); i++) {
    // Start of BLE mode stream
    if(incoming[i] == '[') {
      stream_is_mode = true;
      continue;
    }
    if(stream_is_mode) {
      if(incoming[i] == 's') {
        mode_WIFI = false;
        mode_SD = true;
        stream_is_mode = false;
      }
      if(incoming[i] == 'w') {
        mode_WIFI = true;
        mode_SD = false;
        stream_is_mode = false;
      }
      if(incoming[i] == 'o') {
        digitalWrite(GFX_BL, HIGH);
        stream_is_mode = false;
      }
      if(incoming[i] == 'f') {
        digitalWrite(GFX_BL, LOW);
        stream_is_mode = false;
      }
    } // End of BLE mode stream

    // Start of BLE interval stream
    if(incoming[i] == '{') {
      str_interval = "";
      stream_is_interval = true;
      continue;
    }
    if(stream_is_interval) {
      if(incoming[i] != '}') {
        str_interval.concat(incoming[i]);
      }
      else {
        stream_is_interval = false;
        interval = str_interval.toInt() * 1000;
        if(debug) {
          Serial.print("Obtained new interval value: ");
          Serial.println(interval);
        }
        break;
      }
    } // End of BLE interval stream

    // Start of BLE settings stream
    if(incoming[i] == '<') {
      str_wifi_ssid = "";
      str_wifi_pass = "";
      str_wifi_host = "";
      str_wifi_port = "";

      wifi_ssid = true;
      continue;
    }
    // Split in BLE stream
    if(incoming[i] == '?') {
      found_split = true;
      if(wifi_ssid) {
        found_split = false;
        wifi_ssid = false;
        wifi_pass = true;
        continue;
      }
      if(wifi_pass) {
        found_split = false;
        wifi_pass = false;
        wifi_host = true;
        continue;
      }
      if(wifi_host) {
        found_split = false;
        wifi_host = false;
        wifi_port = true;
        continue;
      }
    } 
    // Last character
    if(incoming[i] == '>') {
      wifi_port = false;
      found_split = false;
      mode_WIFI = false;
      save_WIFI_settings = true;
      if(debug) {
        Serial.print("BLE Incoming Settings: WIFI_SSID: ");
        Serial.print(str_wifi_ssid);
        Serial.print(", WIFI_pass: ");
        Serial.print(str_wifi_pass);
        Serial.print(", WIFI_host: ");
        Serial.print(str_wifi_host);
        Serial.print(", WIFI_port: ");
        Serial.println(str_wifi_port);
      }
    }
    // Assigning BLE settings stream components to variables
    if(wifi_ssid) {
      str_wifi_ssid.concat(incoming[i]);
    }
    if(wifi_pass) {
      str_wifi_pass.concat(incoming[i]);
    }
    if(wifi_host) {
      str_wifi_host.concat(incoming[i]);
    }
    if(wifi_port) {
      str_wifi_port.concat(incoming[i]);
    }
  } // End of BLE settings stream
} // END decode BLE stream function

/* Function to save Wifi settings */
void saveWifiSettings() {
  if(debug){Serial.println("IN saveWifiSettings(): Before assigning SSID");}
  str_wifi_ssid.toCharArray(SSID_NAME, sizeof(SSID_NAME));
  if(debug){Serial.println("IN saveWifiSettings(): Before assigning Password");}
  str_wifi_pass.toCharArray(SSID_PASSWORD, sizeof(SSID_PASSWORD));
  if(debug){Serial.println("IN saveWifiSettings(): Before assigning Host");}
  str_wifi_host.toCharArray(HTTP_HOST, sizeof(HTTP_HOST));
  if(debug){Serial.println("IN saveWifiSettings(): Before assigning Port");}
  HTTP_PORT = str_wifi_port.toInt();

  if(debug) {
    Serial.print("Saved WIFI Settings: SSID: ");
    Serial.print(SSID_NAME);
    Serial.print(" Password: ");
    Serial.print(SSID_PASSWORD);
    Serial.print(" HOST: ");
    Serial.print(HTTP_HOST);
    Serial.print(" PORT: ");
    Serial.println(HTTP_PORT);
  }
  mode_WIFI = true;
  save_WIFI_settings = false;
} // END save Wifi settings function

void setup() {
  Serial.begin(115200);

  #ifdef GFX_EXTRA_PRE_INIT
    GFX_EXTRA_PRE_INIT();
  #endif

  gfx->begin();
  gfx->fillScreen(BLACK);

  #ifdef GFX_BL
    pinMode(GFX_BL, OUTPUT);
    digitalWrite(GFX_BL, HIGH);
  #endif

  // BLE Initialization settings
  BLEDevice::init("Digital Photo Frame");
  // Create the BLE Server
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  // Create the BLE Service
  BLEService *pService = pServer->createService(SERVICE_UUID);

  // Create a BLE Characteristic
  pCharacteristic = pService->createCharacteristic(
                      CHARACTERISTIC_UUID,
                      BLECharacteristic::PROPERTY_READ   |
                      BLECharacteristic::PROPERTY_WRITE  |
                      BLECharacteristic::PROPERTY_INDICATE
                    );

  pCharacteristic->setCallbacks(new MyCallbacks());

  // Start the service
  pService->start();

  // Start advertising
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(false);
  pAdvertising->setMinPreferred(0x0);  // set value to 0x00 to not advertise this parameter
  BLEDevice::startAdvertising();
} // END setup

void checkBLEConnection() {
  // disconnecting
  if (!deviceConnected && oldDeviceConnected) {
      delay(500); // give the bluetooth stack the chance to get things ready
      pServer->startAdvertising(); // restart advertising
      oldDeviceConnected = deviceConnected;
  }
  // connecting
  if (deviceConnected && !oldDeviceConnected) {
      // do stuff here on connecting
      oldDeviceConnected = deviceConnected;
  }
}

/* Wifi setup: Initializes Wifi Mode */
void WIFI_setup()
{
  gfx->setCursor(0, 0);
  gfx->fillScreen(BLACK);
  gfx->println("WIFI MODE");
  delay(1000);

  WiFi.mode(WIFI_STA);
  WiFi.begin(SSID_NAME, SSID_PASSWORD);

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(1000);
    gfx->fillScreen(BLACK);
    gfx->setCursor(0, 0);
    Serial.print("Connecting to WIFI");
    gfx->println("Connecting to WIFI");
    delay(1500);
    break;
  }

  if(WiFi.status() != WL_CONNECTED) {
    gfx->setCursor(0, 0);
    gfx->fillScreen(BLACK);
    gfx->println("ERROR:");
    gfx->println("WIFI credentials incorrect");
  }

  // setup http_path query value with LCD dimension
  sprintf(http_path, HTTP_PATH_TEMPLATE, gfx->width(), gfx->height());

  // set WDT timeout a little bit longer than HTTP timeout
  esp_task_wdt_init((HTTP_TIMEOUT / 1000) + 1, true);
  enableLoopWDT();

} // END WIFI Setup

/* SD Setup: Initializes SD mode */
void SD_setup() {
  gfx->setCursor(0, 0);
  gfx->fillScreen(BLACK);
  gfx->print("SD CARD MODE");
  delay(1000);
  SD.begin(15);
  if(!SD.begin(15)) {
    gfx->setCursor(0, 0);
    gfx->fillScreen(BLACK);
    gfx->println("ERROR:");
    gfx->println("Cannot load SD Card");
  }
}

/* Wifi loop to obtain a new image from the server every 'interval' */
void WIFI_loop()
{
  if(debug){Serial.println("START WIFI_loop()");}
  WIFI_setup();
  int count_error = 0;
  while(mode_WIFI) {
    checkBLEConnection();
    if (millis() < next_show_millis_wifi) {
      delay(1000);
    }
    else {
      if (WiFi.status() == WL_CONNECTED) {
        int jpeg_result = 0;
        
        http.setTimeout(HTTP_TIMEOUT);

        http.begin(client, HTTP_HOST, HTTP_PORT, http_path);
        int httpCode = http.GET();

        // HTTP header has been send and Server response header has been handled
        if (httpCode <= 0) {
          count_error++;
          if(count_error == 1) {
            gfx->setCursor(0, 0);
            gfx->fillScreen(BLACK);
            gfx->println("ERROR: ");
            gfx->println("Cannot reach server");
          }
        }
        else {
          if (httpCode != 200) {
            delay(5000);
          }
          else {
            // get lenght of document(is - 1 when Server sends no Content - Length header)
            int len = http.getSize();

            if (len <= 0) {
              if(debug){Serial.printf("[HTTP] Unknown content size: %d\n", len);}
            }
            else {
              unsigned long start = millis();

              uint8_t *buf = (uint8_t *)malloc(len);
              if (buf) {
                static WiFiClient *http_stream = http.getStreamPtr();
                jpeg_result = jpegOpenHttpStreamWithBuffer(http_stream, buf, len, jpegDrawCallback);

                if (jpeg_result) {
                  jpeg_result = jpegDraw("", jpegDrawCallbackSD, false /* useBigEndian */,
                    0 /* x */, 0 /* y */, gfx->width() /* widthLimit */, gfx->height() /* heightLimit */,
                    true /* drawMode */);
                }
                free(buf);
              }
              else {
                // get tcp stream
                static WiFiClient *http_stream = http.getStreamPtr();
                jpeg_result = jpegOpenHttpStream(http_stream, len, jpegDrawCallback);

                if (jpeg_result) {
                  jpeg_result = jpegDraw("", jpegDrawCallbackSD, false /* useBigEndian */,
                    0 /* x */, 0 /* y */,gfx->width() /* widthLimit */,gfx->height() /* heightLimit */,
                    true /* drawMode */);
                }
              }
            }
          }
        }
        http.end();
          if (jpeg_result) {
            next_show_millis_wifi = ((millis() / interval) + 1) * interval; // next minute
          }
      }
    }
    feedLoopWDT(); // Notify WDT still working
  }
  if(debug){Serial.println("END WIFI_loop()");}
}

/* SD loop to obtain a new image from the SD Card every 'interval' */
void SD_loop()
{
  if(debug){Serial.println("START SD_loop()");}
  SD_setup();

  bool rendering = false;
  while(mode_SD) {
    int filecount = 0;
    File dir = SD.open("/");

    while(mode_SD) {
      checkBLEConnection();
      if (millis() < next_show_millis_sd) {
      delay(1000);
      }
      else {
        rendering = false;
        File entry = dir.openNextFile();
        
        if (!entry) {
          break;
        }

        if (entry.isDirectory() == false) {
          rendering = true;
          const char *name = entry.name();
          const int len = strlen(name);

          if (len > 3 && strcasecmp(name + len - 3, "JPG") == 0) {
            char lead[2] = "/";
            name = strcat(lead, name);
            gfx->fillScreen(BLACK);
            int place_holder = jpegDraw(name, jpegDrawCallbackSD, true /* useBigEndian */,
              0 /* x */, 0 /* y */, gfx->width() /* widthLimit */, gfx->height() /* heightLimit */,
              false /* drawMode */);
            filecount = filecount + 1;
          }
        }
        entry.close();
        if(!rendering) { // Unable to render, so break
          break;
        }
        next_show_millis_sd = ((millis() / interval) + 1) * interval; // next minute
      }
    }
  }
  if(debug){Serial.println("END SD_loop()");}
}

/* Main program loop: Running through either SD loop or WIFI loop */
void loop() {
  while(mode_WIFI) {
    if(debug){Serial.println("START loop() WIFI_MODE");}
    WIFI_loop();
    if(debug){Serial.println("IN loop() WIFI_MODE: After exiting WIFI_loop()");}
    WiFi.disconnect(true,true);
    if(debug){Serial.println("IN loop() WIFI_MODE: After disconnecting WIFI");}
    disableLoopWDT();
    if(debug){Serial.println("IN loop() WIFI_MODE: After disabling WDT");}
    if(save_WIFI_settings) {
      if(debug){Serial.println("IN loop() WIFI_MODE: Before saveWifiSettings()");}
      saveWifiSettings();
    }
    if(debug){Serial.println("END loop() WIFI_MODE");}
  }
  while(mode_SD) {
    if(debug){Serial.println("START loop() SD_MODE");}
    SD_loop();
    SD.end();
    if(debug){Serial.println("END loop() SD_MODE");}
  }
}
