#include <WiFi.h>
#include <HTTPClient.h>
#include <DHT.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ArduinoJson.h> 
#include "time.h"
#include "cred.h"

// ----------------- CONFIG -----------------
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
#define SCREEN_ADDRESS 0x3C

#define DHTPIN 16
#define DHTTYPE DHT22
#define TRIG_PIN 4
#define ECHO_PIN 40      
#define BUTTON_PIN 5
#define OLED_SCL 7
#define OLED_SDA 8

// Thresholds matched to Lambda
#define DIST_THRESHOLD 5.0    
#define TEMP_THRESHOLD 32.0   
#define MAX_TEMP_ALERT 38.0   

const char* ssid = "Wokwi-GUEST";
const char* password = "";
// Ensure these paths end with /ingest

const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 28800; 
const int   daylightOffset_sec = 0;

DHT dht(DHTPIN, DHTTYPE);
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

unsigned long lastDHTRead = 0, lastUltrasonic = 0, lastAWSUpdate = 0, lastOLEDUpdate = 0;

// INCREASED SPEED: Uploading every 3 seconds for faster dashboard normalization
const unsigned long AWS_INTERVAL = 2000; 
const unsigned long OLED_INTERVAL = 500; 

bool sensorsEnabled = true, isWorn = false, isAlertActive = false;
bool lastIsWorn = false, lastIsAlertActive = false, lastSensorsEnabled = true; 
float currentTemp = 0;
long currentDistance = 999; 

// ------------------------------------------------

String getTimestamp() {
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)) return "[Time Error]";
  char buf[25];
  strftime(buf, sizeof(buf), "[%H:%M:%S]", &timeinfo);
  return String(buf);
}

void logEvent(String eventName) {
  Serial.println("-----------------------------------");
  Serial.print(getTimestamp());
  Serial.print(" EVENT: ");
  Serial.println(eventName);
  Serial.print("DATA: ");
  Serial.print(currentTemp, 1); Serial.print("C | ");
  Serial.print(currentDistance); Serial.println("cm");
  Serial.println("-----------------------------------");
}

void setup_wifi() {
  Serial.print("Connecting to WiFi");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.println("\nWiFi Connected.");
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
}

void sendToAWS() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(aws_ingest_endpoint);
    http.addHeader("Content-Type", "application/json");

    StaticJsonDocument<200> doc;
    doc["patientId"] = "p001";
    doc["tempC"] = currentTemp;        // Key for Lambda
    doc["distanceCm"] = currentDistance; // Key for Lambda
    doc["humidity"] = 50;
    
    String json;
    serializeJson(doc, json);
    int code = http.POST(json);
    http.end();
  }
}

void updateOLED() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  
  if (isAlertActive) {
    display.setTextSize(2); display.setCursor(0, 10); display.println("!! HOT !!");
    display.setTextSize(1); display.setCursor(0, 40); display.println("REMOVE BRACE");
  } else {
    display.setTextSize(1); display.setCursor(0, 0); display.println("SCOLIO-SENSE");
    display.setCursor(0, 20);
    display.print("Status: "); 
    if (!sensorsEnabled) display.println("OFF");
    else display.println(isWorn ? "WORN" : "NOT WORN");

    display.setCursor(0, 40);
    display.print("Temp: "); display.print(currentTemp, 1); display.println(" C");
    display.print("Dist: "); display.print(currentDistance); display.println(" cm");
  }
  display.display();
}

void setup() {
  Serial.begin(115200);
  pinMode(TRIG_PIN, OUTPUT); pinMode(ECHO_PIN, INPUT); pinMode(BUTTON_PIN, INPUT_PULLUP);
  dht.begin(); Wire.begin(OLED_SDA, OLED_SCL);
  display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS);
  setup_wifi();
}

void loop() {
  unsigned long now = millis();

  if (digitalRead(BUTTON_PIN) == LOW) {
    sensorsEnabled = !sensorsEnabled;
    delay(250);
  }

  if (sensorsEnabled) {
    if (now - lastUltrasonic >= 150) {
      lastUltrasonic = now;
      digitalWrite(TRIG_PIN, LOW); delayMicroseconds(2); digitalWrite(TRIG_PIN, HIGH);
      delayMicroseconds(10); digitalWrite(TRIG_PIN, LOW);
      long duration = pulseIn(ECHO_PIN, HIGH);
      if (duration > 0) currentDistance = duration * 0.034 / 2;
    }
    if (now - lastDHTRead >= 2000) {
      lastDHTRead = now;
      float t = dht.readTemperature();
      if (!isnan(t)) currentTemp = t;
    }

    isWorn = (currentDistance <= DIST_THRESHOLD && currentTemp >= TEMP_THRESHOLD);
    isAlertActive = (currentTemp > MAX_TEMP_ALERT);

    // Faster Cloud Update
    if (now - lastAWSUpdate >= AWS_INTERVAL) { 
      sendToAWS(); 
      lastAWSUpdate = now; 
    }
  }

  // Event Logging for Serial Monitor
  if (sensorsEnabled != lastSensorsEnabled) {
    logEvent(sensorsEnabled ? "SYSTEM ACTIVE" : "SYSTEM STANDBY");
    lastSensorsEnabled = sensorsEnabled;
    updateOLED();
  }
  if (isWorn != lastIsWorn && sensorsEnabled) {
    logEvent(isWorn ? "BRACE PUT ON" : "BRACE REMOVED");
    lastIsWorn = isWorn;
    updateOLED();
  }
  if (isAlertActive != lastIsAlertActive && sensorsEnabled) {
    if (isAlertActive) logEvent("!! HEAT ALERT !!");
    lastIsAlertActive = isAlertActive;
    updateOLED();
  }

  if (now - lastOLEDUpdate >= OLED_INTERVAL) { updateOLED(); lastOLEDUpdate = now; }
}