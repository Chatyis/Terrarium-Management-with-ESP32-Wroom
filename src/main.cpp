#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ArduinoJson.h>
#include <AsyncTCP.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include "DHT.h"
#include "ESP32Time.h"
#include "FS.h"
#include "SD.h"
#include "SPI.h"
#include "SPIFFS.h"
#include <Preferences.h>
#include <RtcDS1302.h>

// #define WIFI_SSID "Vectra-WiFi-752523"
// #define WIFI_PASSWORD "nhqd9412am0fmiq8"
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define DHT_PIN 4
// #define RIGHT_BUTTON_PIN 13
#define ADD_BTN 35
#define SUBTRACT_BTN 34
#define TOGGLE_VALUE_BTN 39
#define TOGGLE_MODE_BTN 36
#define DHT_PIN1 27
#define DHT_PIN2 17
#define RELAY_PIN1 14
#define RELAY_PIN2 26
#define RELAY_PIN3 25
#define RELAY_PIN4 32
#define RELAY_PIN5 33
#define RELAY_PIN6 16
#define DHTTYPE DHT22
#define RTC_CLK 15
#define RTC_DAT 13
#define RTC_RST 2

enum mode
{
  overview,
  temperature,
  lighting,
  rtcConfig,
  network
};

enum EditedTimeValue
{
  year,
  month,
  day,
  hour,
  minute
};

struct Button {
	const uint8_t PIN;
	bool pressed;
  unsigned long button_time;
  unsigned long last_button_time;
};

struct HeatingSocket {
  float maxTemperature;
  float currentTemperature;
  float currentHumidity;
  uint8_t relayPin;
  bool enabled;
};

struct Time {
  uint8_t hour;
  uint8_t minute;
};

struct LightningSocket {
  Time timeFrom;
  Time timeTo;
  uint8_t relayPin;
  bool enabled;
};

// Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
DHT dht1 = DHT(DHT_PIN, DHTTYPE);
DHT dht2 = DHT(DHT_PIN1, DHTTYPE);
DHT dht3 = DHT(DHT_PIN2, DHTTYPE);

ThreeWire myWire(RTC_DAT,RTC_CLK,RTC_RST);
RtcDS1302<ThreeWire> Rtc(myWire);

volatile int currentLine = 0;
volatile int currentHeatingSocketIndex = 0;
volatile int currentLightningSocketIndex = 0;
volatile float h = 0;
volatile float t = 0;
bool shouldMeasure = true;
bool isEditingFromHour = true;
bool shouldSave = false;
const char *measurementsFilePath = "/measurements";

Button addButton = {ADD_BTN, false, 0, 0};
Button subtractButton = {SUBTRACT_BTN, false, 0, 0};
Button toggleValueButton = {TOGGLE_VALUE_BTN, false, 0, 0};
Button toggleModeButton = {TOGGLE_MODE_BTN, false, 0, 0};
enum mode currentMode = network;

Preferences preferences;

HeatingSocket heatingSockets[] = {{22.0, 0, 0, RELAY_PIN1, true}, {22.0, 0, 0, RELAY_PIN2, true}, {22.0, 0, 0, RELAY_PIN3, true}};
LightningSocket lightningSockets[] = {{{6,0}, {19,0}, RELAY_PIN4, true}, {{6,0}, {19,0}, RELAY_PIN5, true}, {{6,0}, {19,0}, RELAY_PIN6, true}};
HeatingSocket *currentHeatingSocket = &heatingSockets[currentHeatingSocketIndex];
LightningSocket *currentLightningSocket = &lightningSockets[currentLightningSocketIndex];

hw_timer_t *My_timer = NULL;
hw_timer_t *My_timer1 = NULL;

String espSsid = "Terrarium Management";
String espPassword = "uc4Z9h!z";
String ssid = "";
String password = "";
AsyncWebServer server(80);
String header;
IPAddress apIp;
IPAddress staIp;

EditedTimeValue currentlyEditedTimeValue = year;

void moveCursorLineDown();
void clearAndWriteOnOled(String text);
void writeLineOnOled(String text);
void IRAM_ATTR measureTempAndHumidity();
void subtractMinutes(Time *time);
void addMinutes(Time *time);
void updateScreen();
void setupMeasurements();
void setupPins();
void getAndDisplayMeasurements();
void checkIfToggleHeating();
void checkIfToggleLightning();
void handleToggleModeButton();
void handleToggleValueButton();
void handleAddButton();
void toggleHeatingSocket();
void toggleLightningSocket();
void handleButtonsLadder();
void handleSubtractButton();
void resetButtonsPressed();
void handleSavingMeasurements();
void handleButtonAndRefreshScreen(void (*handleButtonCallback)());
void handleToggleCurrentModeSocketButton();
void writeFile(fs::FS &fs, const char *path, const char *message);
void readFile(fs::FS &fs, const char *path);
void appendFile(fs::FS &fs, const char *path, const char *message);
String getMeasurementsList();
void loadConfiguration();
void saveConfiguration();
void saveMeasurements(const String filename);
void initializeSdCard();
void setupSavingMeasurements();
void setupWifi();
void setupAsyncWebServer();
void notFound(AsyncWebServerRequest *request);
void setupSpiffs();
String getTwoPlacesStringFromNumber(uint8_t value);
void subtractRtcTime();
void addRtcTime();
String getCurrentDateString();

void setup() {
  // analogReadResolution(12);
  Serial.begin(921600);
  dht1.begin();
  dht2.begin();
  dht3.begin();
  Rtc.Begin();
  // uint64_t test = RtcDateTime(__DATE__, __TIME__).Unix64Time();
  Serial.println(RtcDateTime(__DATE__, __TIME__).Unix64Time());
  RtcDateTime nowRtc = Rtc.GetDateTime();
  // nowRtc.InitWithDateTimeFormatString
  Serial.print(nowRtc.Unix64Time());
  // Serial.print(__TIME__);
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { 
    Serial.println(F("SSD1306 allocation failed"));
    for(;;);
  }
  delay(2000);

  display.setTextSize(1);
  display.setTextColor(WHITE);

  setupSpiffs();
  setupPins();
  initializeSdCard();
  loadConfiguration();
  setupWifi();
  setupMeasurements();
  setupSavingMeasurements();
  setupAsyncWebServer();
}

void loop() {
  if(shouldMeasure) {
    shouldMeasure = false;
    getAndDisplayMeasurements();
  }

  if(shouldSave) {
    shouldSave = false;
    handleSavingMeasurements();
  }

  if(subtractButton.pressed) {
    subtractButton.pressed = false;
    handleButtonAndRefreshScreen(handleSubtractButton);
    saveConfiguration();
  }

  if(addButton.pressed) {
    addButton.pressed = false;
    handleButtonAndRefreshScreen(handleAddButton);
    saveConfiguration();
  }

  if(toggleValueButton.pressed) {
    toggleValueButton.pressed = false;
    handleButtonAndRefreshScreen(handleToggleValueButton);
  }

  if(toggleModeButton.pressed) {
    toggleModeButton.pressed = false;
    handleButtonAndRefreshScreen(handleToggleModeButton);
  }
}

void setupSpiffs(){
    if(!SPIFFS.begin(true)){
    Serial.println("An Error has occurred while mounting SPIFFS");
    return;
  }

  File file = SPIFFS.open("/text.txt");
  if(!file){
    Serial.println("Failed to open file for reading");
    return;
  }
  
  Serial.println("File Content:");
  while(file.available()){
    Serial.write(file.read());
  }
  file.close();
}

void setupWifi() {
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(espSsid, espPassword);
}

void notFound(AsyncWebServerRequest *request) {
    request->send(404, "text/plain", "Not found");
}

void setupAsyncWebServer() {
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    if(WiFi.status() == WL_CONNECTED){
      request->redirect("/home");
    } else{
      request->send(SPIFFS, "/index.html", "text/html");
    }
    });

    server.on("/connect", HTTP_POST, [](AsyncWebServerRequest *request){
        if (request->hasParam("SSID", true)) {
          ssid = String(request->getParam("SSID",true)->value());
          char _ssid[ssid.length() + 1];
          ssid.toCharArray(_ssid, ssid.length() + 1);
          if(request->hasParam("password",true)){
            password = String(request->getParam("password",true)->value());
            char _password[password.length() + 1];
            password.toCharArray(_password, password.length() + 1);
            const char *test = "asdasd";
            Serial.print(_ssid);
            Serial.print(_password);
            // const char *_ssid = "WuTangLan";
            // const char *_password = "pokaczolopyty";
            WiFi.disconnect();
            WiFi.begin(_ssid, _password);
          }
        } else {
          ssid = "";
          password = "";
        }

        request->redirect("/logging-in");
        // request->send(200, "text/plain", "Hello, POST: " + message); 
    });

    server.on("/Chart.js", HTTP_GET, [](AsyncWebServerRequest *request){
      request->send(SPIFFS, "/Chart.js", "text/javascript");
    });

    server.on("/test", HTTP_GET, [](AsyncWebServerRequest *request){
      request->send(SPIFFS, "/proofOfConcept.html", "text/html");
    });

    server.on("/home", HTTP_GET, [](AsyncWebServerRequest *request){
      request->send(SPIFFS, "/home.html", "text/html");
    });

    server.on("/logging-in", HTTP_GET, [](AsyncWebServerRequest *request){
      request->send(SPIFFS, "/logging-in.html", "text/html");
    });

    server.on("/check-wifi-status", HTTP_GET, [](AsyncWebServerRequest *request){
      const bool isConnected = WiFi.status() == WL_CONNECTED;
      String response = "{\"isLoggedIn\": \"";
      response += isConnected ? "true" : "false";
      response += "\"}";
      request->send(200, "text/json", response); 
    });

    server.on("/measurements-list", HTTP_GET, [](AsyncWebServerRequest *request){
      String measurementsList = getMeasurementsList();
      request->send(200, "application/json", measurementsList); 
    });

    server.on("/measurement", HTTP_GET, [](AsyncWebServerRequest *request){
      String measurementName;
      if (request->hasParam("measurementName")){
        measurementName = request->getParam("measurementName")->value();
      }
      request->send(SD, String(measurementsFilePath)+"/"+measurementName+".txt","application/json"); 
    });

    server.on("/wifi", HTTP_GET, [](AsyncWebServerRequest *request){
      request->send(SPIFFS, "/index.html", "text/html");
    });

    server.onNotFound(notFound);

    server.begin();
}

void handleToggleCurrentModeSocketButton() {
  if(currentMode == temperature){
    toggleHeatingSocket();
  }
  else if(currentMode == lighting){
    toggleLightningSocket();
  }
}

void handleButtonAndRefreshScreen(void (*handleButtonCallback)()){
  handleButtonCallback();
  updateScreen();
}

void handleSubtractButton(){
  if(currentMode == temperature){
    currentHeatingSocket->maxTemperature -= 0.5;
  }
  else if(currentMode == lighting){
    if(isEditingFromHour){
      subtractMinutes(&currentLightningSocket->timeFrom);
    } else{
      subtractMinutes(&currentLightningSocket->timeTo);
    }
  } else if(currentMode == rtcConfig) {
    subtractRtcTime();
  }
}

void handleAddButton() {
  if(currentMode == temperature){
    currentHeatingSocket->maxTemperature += 0.5;
  }
  else if(currentMode == lighting){
    if(isEditingFromHour){
      addMinutes(&currentLightningSocket->timeFrom);
    } else{
      addMinutes(&currentLightningSocket->timeTo);
    }
  } else if(currentMode == rtcConfig) {
    addRtcTime();
  }
}

void addRtcTime(){
  RtcDateTime currentRtcTime = Rtc.GetDateTime();
  uint16_t _year = currentRtcTime.Year();
  uint8_t _month = currentRtcTime.Month();
  uint8_t _dayOfMonth = currentRtcTime.Day();
  uint8_t _hour = currentRtcTime.Hour();
  uint8_t _minute = currentRtcTime.Minute();
  switch(currentlyEditedTimeValue){
    case year:{
      _year += 1;
      break;
    }
    case month:{
      _month += 1;
      break;
    }
    case day:{
      _dayOfMonth += 1;
      break;
    }
    case hour:{
      _hour += 1;
      break;
    }
    case minute:{
      _minute += 1;
      break;
    }
  }
  Rtc.SetDateTime(RtcDateTime(_year, _month, _dayOfMonth, _hour, _minute, 0));
}

void subtractRtcTime() {
  RtcDateTime currentRtcTime = Rtc.GetDateTime();
  uint16_t _year = currentRtcTime.Year();
  uint8_t _month = currentRtcTime.Month();
  uint8_t _dayOfMonth = currentRtcTime.Day();
  uint8_t _hour = currentRtcTime.Hour();
  uint8_t _minute = currentRtcTime.Minute();
  switch(currentlyEditedTimeValue){
    case year:{
      _year -= 1;
      break;
    }
    case month:{
      _month -= 1;
      break;
    }
    case day:{
      _dayOfMonth -= 1;
      break;
    }
    case hour:{
      _hour -= 1;
      break;
    }
    case minute:{
      _minute -= 1;
      break;
    }
  }
  Rtc.SetDateTime(RtcDateTime(_year, _month, _dayOfMonth, _hour, _minute, 0));
}

void subtractMinutes(Time* time) {
  if(time->minute>0) {
    time->minute -= 15;
  } else if(time->hour > 0) {
    time->minute = 45;
    time->hour -= 1;
  }
}

void addMinutes(Time* time) {
  if(time->minute<45) {
    time->minute += 15;
  } else if(time->hour < 24) {
    time->minute = 0;
    time->hour += 1;
  }
}

void toggleHeatingSocket() {
  if(currentHeatingSocketIndex < (sizeof(heatingSockets) / sizeof(HeatingSocket)) - 1 ){
    currentHeatingSocketIndex++;
  } else {
    currentHeatingSocketIndex = 0;
  }
  currentHeatingSocket = &heatingSockets[currentHeatingSocketIndex];
}

void toggleLightningSocket() {
  if(currentLightningSocketIndex < (sizeof(heatingSockets) / sizeof(HeatingSocket)) - 1 ){
    currentLightningSocketIndex++;
  } else {
    currentLightningSocketIndex = 0;
  }
  currentLightningSocket = &lightningSockets[currentLightningSocketIndex];
}

void moveCursorLineDown() {
  if(currentLine<60) {
    currentLine += 10;
  } else {
    currentLine = 0;
  }
}

void clearAndWriteOnOled(String text){
  currentLine = 0;
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println(text);
  moveCursorLineDown();
  display.display();
}

void writeLineOnOled(String text){
  display.setCursor(0, currentLine);
  display.println(text);
  moveCursorLineDown();
  display.display();
}

void updateScreen() {
  switch(currentMode){
    case overview:{
      clearAndWriteOnOled(getCurrentDateString());
      writeLineOnOled(String(heatingSockets[0].currentHumidity) + "|" + String(heatingSockets[1].currentHumidity) + "|" + String(heatingSockets[2].currentHumidity) + " %");
      writeLineOnOled(String(heatingSockets[0].currentTemperature) + "|" + String(heatingSockets[1].currentTemperature) + "|" + String(heatingSockets[2].currentTemperature) + " *C");
      writeLineOnOled(String(heatingSockets[0].maxTemperature) + "|" + String(heatingSockets[1].maxTemperature) + "|" + String(heatingSockets[2].maxTemperature) + " *C");
      writeLineOnOled(getTwoPlacesStringFromNumber(lightningSockets[0].timeFrom.hour) + ":" + getTwoPlacesStringFromNumber(lightningSockets[0].timeFrom.minute) + "|" + getTwoPlacesStringFromNumber(lightningSockets[1].timeFrom.hour) + ":" + getTwoPlacesStringFromNumber(lightningSockets[1].timeFrom.minute) + "|" + getTwoPlacesStringFromNumber(lightningSockets[2].timeFrom.hour) + ":" + getTwoPlacesStringFromNumber(lightningSockets[2].timeFrom.minute));
      writeLineOnOled(getTwoPlacesStringFromNumber(lightningSockets[0].timeTo.hour) + ":" + getTwoPlacesStringFromNumber(lightningSockets[0].timeTo.minute) + "|" + getTwoPlacesStringFromNumber(lightningSockets[1].timeTo.hour) + ":" + getTwoPlacesStringFromNumber(lightningSockets[1].timeTo.minute) + "|" + getTwoPlacesStringFromNumber(lightningSockets[2].timeTo.hour) + ":" + getTwoPlacesStringFromNumber(lightningSockets[2].timeTo.minute));
      break;
    }
    case temperature:{
      clearAndWriteOnOled(getCurrentDateString());
      writeLineOnOled("SET TEMPERATURES");
      writeLineOnOled("Current socket: " + String(currentHeatingSocketIndex));
      writeLineOnOled("Max Temp: " + String(currentHeatingSocket->maxTemperature) + "*C");
      break;
    }
    case lighting:{
      clearAndWriteOnOled(getCurrentDateString());
      writeLineOnOled("SET LIGHT TIME");
      writeLineOnOled("Current socket: " + String(currentLightningSocketIndex));
      writeLineOnOled(String(isEditingFromHour?"  ":"") + "From: " + getTwoPlacesStringFromNumber(currentLightningSocket->timeFrom.hour) + ":" + getTwoPlacesStringFromNumber(currentLightningSocket->timeFrom.minute));
      writeLineOnOled(String(!isEditingFromHour?"  ":"") + "To: " + getTwoPlacesStringFromNumber(currentLightningSocket->timeTo.hour) + ":" + getTwoPlacesStringFromNumber(currentLightningSocket->timeTo.minute));
      break;
    }
    case rtcConfig:{
      RtcDateTime currentTimeRead = Rtc.GetDateTime();
      clearAndWriteOnOled(getCurrentDateString());
      writeLineOnOled("SET TIME");
      if(currentlyEditedTimeValue < hour){
        writeLineOnOled(String(currentlyEditedTimeValue==year?"  ":"")+"Year: "+ String(currentTimeRead.Year()));
      }
      writeLineOnOled(String(currentlyEditedTimeValue==month?"  ":"")+"Month: "+ String(currentTimeRead.Month()));
      writeLineOnOled(String(currentlyEditedTimeValue==day?"  ":"")+"Day: "+ String(currentTimeRead.Day()));
      writeLineOnOled(String(currentlyEditedTimeValue==hour?"  ":"")+"Hour: "+ String(currentTimeRead.Hour()));
      writeLineOnOled(String(currentlyEditedTimeValue==minute?"  ":"")+"Minute: "+ String(currentTimeRead.Minute()));
      break;
    }
    case network:{
      apIp = WiFi.softAPIP();
      staIp = WiFi.localIP();
      clearAndWriteOnOled(getCurrentDateString());
      //ADD logic that displayed if online/offline, says to connect and setup wifi.
      writeLineOnOled("AP IP: "+apIp.toString());
      writeLineOnOled("STA IP: "+staIp.toString());
      break;
    }
  }
}

String getCurrentDateString()
{
    char datestring[26];
    RtcDateTime now = Rtc.GetDateTime();
    snprintf_P(datestring, 
            countof(datestring),
            PSTR("%02u/%02u/%04u %02u:%02u"),
            now.Day(),
            now.Month(),
            now.Year(),
            now.Hour(),
            now.Minute());
  return datestring;
}

String getTwoPlacesStringFromNumber(uint8_t value){
  return value < 10 ? "0" + String(value) : String(value);
}

void checkIfToggleHeating() {
  for (uint8_t i = 0; i < sizeof(heatingSockets) / sizeof(HeatingSocket); i++){
    heatingSockets[i].maxTemperature < heatingSockets[i].currentTemperature ? heatingSockets[i].enabled = false : heatingSockets[i].enabled = true;
    digitalWrite(heatingSockets[i].relayPin, heatingSockets[i].enabled ? LOW : HIGH);
  }
}

void checkIfToggleLightning() {
  for (uint8_t i = 0; i < sizeof(lightningSockets) / sizeof(LightningSocket); i++){
    if(lightningSockets[i].timeFrom.hour < Rtc.GetDateTime().Hour() && lightningSockets[i].timeTo.hour > Rtc.GetDateTime().Hour()){
      lightningSockets[i].enabled = true;
    } else if (lightningSockets[i].timeFrom.hour == Rtc.GetDateTime().Hour()) {
        if(lightningSockets[i].timeFrom.minute <= Rtc.GetDateTime().Minute()) {
          lightningSockets[i].enabled = true;
        } else {
          lightningSockets[i].enabled = false;
        }
    } else if (lightningSockets[i].timeTo.hour == Rtc.GetDateTime().Hour()) {
        if(lightningSockets[i].timeTo.minute >= Rtc.GetDateTime().Minute()){
          lightningSockets[i].enabled = true;
        } 
        else{
          lightningSockets[i].enabled = false;
        }
    } else {
      lightningSockets[i].enabled = false;
    }
    digitalWrite(lightningSockets[i].relayPin, lightningSockets[i].enabled ? LOW : HIGH);
  }
}

void getAndDisplayMeasurements() {
  h = dht1.readHumidity();
  float tpercent = dht1.readTemperature();

  if (isnan(h) || isnan(t)) {
    Serial.println(F("Failed to read from DHT sensor!"));
    return;
  }
  
  t = dht1.computeHeatIndex(tpercent, h, false);
  heatingSockets[0].currentHumidity = h;
  heatingSockets[0].currentTemperature = t;
  Serial.println("index 0: "+String(heatingSockets[0].currentHumidity) +" "+ String(heatingSockets[0].currentTemperature));
  h = dht2.readHumidity();
  tpercent = dht2.readTemperature();

  if (isnan(h) || isnan(t)) {
    Serial.println(F("Failed to read from DHT sensor!"));
    return;
  }

  t = dht2.computeHeatIndex(tpercent, h, false);
  heatingSockets[1].currentHumidity = h;
  heatingSockets[1].currentTemperature = t;
Serial.println("index 1: "+String(heatingSockets[1].currentHumidity) +" "+ String(heatingSockets[1].currentTemperature));
  h = dht3.readHumidity();
  tpercent = dht3.readTemperature();

  if (isnan(h) || isnan(t)) {
    Serial.println(F("Failed to read from DHT sensor!"));
    return;
  }

  t = dht3.computeHeatIndex(tpercent, h, false);
  heatingSockets[2].currentHumidity = h;
  heatingSockets[2].currentTemperature = t;
Serial.println("index 2: "+String(heatingSockets[2].currentHumidity) +" "+ String(heatingSockets[2].currentTemperature));

  checkIfToggleHeating();
  checkIfToggleLightning();
  updateScreen();
}

void IRAM_ATTR measureAndDisplay() {
  shouldMeasure = true;
}

void IRAM_ATTR triggerSaveMeasurements() {
  shouldSave = true;
}

void handleSavingMeasurements() {
  Serial.printf(" \n \n INTERRUPTED \n \n");
  // TODO UNCOMMENT THIS PART TO SAVE
  saveMeasurements("/" + String(Rtc.GetDateTime().Day()) + "-" + String(Rtc.GetDateTime().Month()) + "-" + String(Rtc.GetDateTime().Year()) + ".txt");
}

void handleToggleModeButton(){
  switch(currentMode){
    case overview: {
      currentMode = temperature;
      break;
    }
    case temperature: {
      currentMode = lighting;
      break;
    }
    case lighting: {
      currentMode = rtcConfig;
      break;
    } 
    case rtcConfig: {
      currentMode = network;
      break;
    }
    case network: {
      currentMode = overview;
      break;
    }
  }
  isEditingFromHour = true;
}

void handleToggleValueButton(){
  if(currentMode == temperature){
    toggleHeatingSocket();
  }
  else if(currentMode == lighting){
    if(isEditingFromHour){
      isEditingFromHour = false;
    } else {
      toggleLightningSocket();
      isEditingFromHour = true;
    }
  } else if(currentMode == rtcConfig) {
    switch(currentlyEditedTimeValue){
      case year: {
        currentlyEditedTimeValue = month;
        break;
      }
      case month: {
        currentlyEditedTimeValue = day;
        break;
      }
      case day: {
        currentlyEditedTimeValue = hour;
        break;
      }
      case hour: {
        currentlyEditedTimeValue = minute;
        break;
      }
      case minute: {
        currentlyEditedTimeValue = year;
        break;
      }
    }
  }
}

void setupMeasurements() {
  My_timer = timerBegin(0, 80, true);
  timerAttachInterrupt(My_timer, &measureAndDisplay, true);
  timerAlarmWrite(My_timer, 2000000, true);
  timerAlarmEnable(My_timer);
}

void setupSavingMeasurements() {
  My_timer1 = timerBegin(1, 80, true);
  timerAttachInterrupt(My_timer1, &triggerSaveMeasurements, true);
  timerAlarmWrite(My_timer1, 15 * 60 * 1000 * 1000, true);
  // timerAlarmWrite(My_timer1, 30 * 1000 * 1000, true);
  timerAlarmEnable(My_timer1);
}

void readFile(fs::FS &fs, const char * path){
  Serial.printf("Reading file: %s\n", path);

  File file = fs.open(path);
  if(!file){
    Serial.println("Failed to open file for reading");
    return;
  }

  Serial.print("Read from file: ");
  while(file.available()){
    Serial.write(file.read());
  }
  file.close();
}

void writeFile(fs::FS &fs, const char * path, const char * message){
  Serial.printf("Writing file: %s\n", path);

  File file = fs.open(path, FILE_WRITE);
  if(!file){
    Serial.println("Failed to open file for writing");
    return;
  }
  if(file.print(message)){
    Serial.println("File written");
  } else {
    Serial.println("Write failed");
  }
  file.close();
}

void appendFile(fs::FS &fs, const char * path, const char * message){
  Serial.printf("Appending to file: %s\n", path);

  File file = fs.open(path, FILE_APPEND);
  if(!file){
    Serial.println("Failed to open file for appending");
    return;
  }
  if(file.print(message)){
      Serial.println("Message appended");
  } else {
    Serial.println("Append failed");
  }
  file.close();
}

void initializeSdCard(){
  // SD.end();
  if(!SD.begin(5)){
    Serial.println("Card Mount Failed");
    return;
  }
  uint8_t cardType = SD.cardType();

  if(cardType == CARD_NONE){
    Serial.println("No SD card attached");
    return;
  }

  Serial.print("SD Card Type: ");
  if(cardType == CARD_MMC){
    Serial.println("MMC");
  } else if(cardType == CARD_SD){
    Serial.println("SDSC");
  } else if(cardType == CARD_SDHC){
    Serial.println("SDHC");
  } else {
    Serial.println("UNKNOWN");
  }

  uint64_t cardSize = SD.cardSize() / (1024 * 1024);
  Serial.printf("SD Card Size: %lluMB\n", cardSize);
}

void loadConfiguration() {
  preferences.end();
  preferences.begin("terrariumSys", false);
  for (int i = 0; i < 3;i++){
    heatingSockets[i].maxTemperature = preferences.getFloat(String("maxTemperature"+(i+1)).c_str(), 22.0);
    lightningSockets[i].timeFrom.hour = preferences.getInt(String("FromHours"+(i+1)).c_str(), 6);
    lightningSockets[i].timeFrom.minute = preferences.getInt(String("FromMinutes"+(i+1)).c_str(), 0);
    lightningSockets[i].timeTo.hour = preferences.getInt(String("ToHours"+(i+1)).c_str(), 19);
    lightningSockets[i].timeTo.minute = preferences.getInt(String("ToMinutes"+(i+1)).c_str(), 0);
  }
  preferences.end();
}

void saveConfiguration(){
  preferences.end();
  preferences.begin("terrariumSys", false);
  for (int i = 0; i < 3;i++){
    preferences.putFloat(String("maxTemperature"+(i+1)).c_str(), heatingSockets[i].maxTemperature);
    preferences.putInt(String("FromHours"+(i+1)).c_str(), lightningSockets[i].timeFrom.hour);
    preferences.putInt(String("FromMinutes"+(i+1)).c_str(), lightningSockets[i].timeFrom.minute);
    preferences.putInt(String("ToHours"+(i+1)).c_str(), lightningSockets[i].timeTo.hour);
    preferences.putInt(String("ToMinutes"+(i+1)).c_str(), lightningSockets[i].timeTo.minute);
  }
  preferences.end();
}

void createDir(fs::FS &fs, const char * path){
  Serial.printf("Creating Dir: %s\n", path);
  if(fs.mkdir(path)){
    Serial.println("Dir created");
  } else {
    Serial.println("mkdir failed");
  }
}

String getMeasurementsList(){
  String fileList;
  Serial.printf("Listing directory: %s\n", measurementsFilePath);

  File root = SD.open(measurementsFilePath);
  if(!root){
    Serial.println("Failed to open directory");
    return "";
  }
  if(!root.isDirectory()){
    Serial.println("Not a directory");
    return "";
  }

  File file = root.openNextFile();
  String fileName = "";
  fileList += "[";
  while(file){
    fileName = String(file.name());
    fileList += "\""+fileName.substring(0,fileName.length()-4)+"\"";
    file = root.openNextFile();
    if(file){
      fileList += ",";
    }
  }
  fileList += "]";
  return fileList;
}

void saveMeasurements(const String filename) {
  // Delete existing file, otherwise the configuration is appended to the file
  // Open file for writing
  if(!SD.exists(measurementsFilePath)){
    createDir(SD, measurementsFilePath);
  }

  File file = SD.open(String(measurementsFilePath)+filename, FILE_APPEND);
  if (!file) {
    Serial.println(F("Failed to create file"));
    return;
  }
  // file.write(',');
  // Allocate a temporary JsonDocument
  // Don't forget to change the capacity to match your requirements.
  // Use https://arduinojson.org/assistant to compute the capacity.
  StaticJsonDocument<512> doc;
  if(file.size()>0){
    file.print(',');
  }
  // Set the values in the document
  for (int i = 0; i < 3;i++){
    doc["currentTemperature"][i] = String(heatingSockets[i].currentTemperature);
    doc["currentHumidity"][i] = String(heatingSockets[i].currentHumidity);
    doc["date"] = Rtc.GetDateTime().Unix64Time();
  }

  Serial.printf("Current epoch: %d", Rtc.GetDateTime().Unix64Time());

  // Serialize JSON to file
  if (serializeJson(doc, file) == 0) {
    Serial.println(F("Failed to write to file"));
  }
  Serial.println(F("Saved"));

  // Close the file
  file.close();
}

void IRAM_ATTR addButtonClicked() {
    addButton.button_time = millis();
    if (addButton.button_time - addButton.last_button_time > 250)
    {
      addButton.pressed = true;
      addButton.last_button_time = addButton.button_time;
    }
}

void IRAM_ATTR subtractButtonClicked() {
    subtractButton.button_time = millis();
    if (subtractButton.button_time - subtractButton.last_button_time > 250)
    {
      subtractButton.pressed = true;
      subtractButton.last_button_time = subtractButton.button_time;
    }
}

void IRAM_ATTR toggleValueButtonClicked() {
    toggleValueButton.button_time = millis();
    if (toggleValueButton.button_time - toggleValueButton.last_button_time > 250)
    {
      toggleValueButton.pressed = true;
      toggleValueButton.last_button_time = toggleValueButton.button_time;
    }
}

void IRAM_ATTR toggleModeButtonClicked() {
    toggleModeButton.button_time = millis();
    if (toggleModeButton.button_time - toggleModeButton.last_button_time > 250)
    {
      toggleModeButton.pressed = true;
      toggleModeButton.last_button_time = toggleModeButton.button_time;
    }
}

void setupPins() {
  pinMode(addButton.PIN, INPUT_PULLUP);
  pinMode(subtractButton.PIN, INPUT_PULLUP);
  pinMode(toggleValueButton.PIN, INPUT_PULLUP);
  pinMode(toggleModeButton.PIN, INPUT_PULLUP);

  attachInterrupt(addButton.PIN, addButtonClicked, FALLING);
  attachInterrupt(subtractButton.PIN, subtractButtonClicked, FALLING);
  attachInterrupt(toggleValueButton.PIN, toggleValueButtonClicked, FALLING);
  attachInterrupt(toggleModeButton.PIN, toggleModeButtonClicked, FALLING);

  for (uint8_t i = 0; i < sizeof(heatingSockets) / sizeof(HeatingSocket); i++){
    pinMode(heatingSockets[i].relayPin, OUTPUT);
    pinMode(lightningSockets[i].relayPin, OUTPUT);
  }
}
