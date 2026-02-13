#include <WiFi.h>
#include <HTTPClient.h>
#include <DHT.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ArduinoJson.h> 
#include "time.h"

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

// Thresholds
#define DIST_THRESHOLD 5.0    
#define TEMP_THRESHOLD 32.0   
#define MAX_TEMP_ALERT 38.0   

const char* ssid = "Wokwi-GUEST";
const char* password = "";

// BERLIN TIME CONFIGURATION
// Central European Time (CET) is UTC+1. Central European Summer Time (CEST) is UTC+2.
const char* ntpServer = "de.pool.ntp.org";
const char* TZ_INFO = "CET-1CEST,M3.5.0,M10.5.0/3"; // Berlin specific POSIX timezone string

DHT dht(DHTPIN, DHTTYPE);
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

unsigned long lastDHTRead = 0, lastUltrasonic = 0, lastAWSUpdate = 0, lastOLEDUpdate = 0;
const unsigned long AWS_INTERVAL = 2000; 
const unsigned long OLED_INTERVAL = 500; 

// State variables
bool sensorsEnabled = true, isWorn = false, isAlertActive = false;
bool lastIsWorn = false, lastIsAlertActive = false, lastSensorsEnabled = true; 
float currentTemp = 0;
float currentHumidity = 0; // Added humidity variable
long currentDistance = 999; 

// ------------------------------------------------

String getTimestamp() {
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)) return "[Time Error]";
  char buf[25];
  // Format: Hour:Minute:Second
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
  Serial.print(currentHumidity, 1); Serial.print("%H | "); // Added to serial monitor
  Serial.print(currentDistance); Serial.println("cm");
  Serial.println("-----------------------------------");
}

void setup_wifi() {
  Serial.print("Connecting to WiFi");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.println("\nWiFi Connected.");
  
  // Set timezone for Berlin
  configTzTime(TZ_INFO, ntpServer); 
}

void sendToAWS() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(aws_ingest_endpoint);
    http.addHeader("Content-Type", "application/json");

    StaticJsonDocument<200> doc;
    doc["patientId"] = "p001";
    doc["tempC"] = currentTemp;
    doc["distanceCm"] = currentDistance;
    doc["humidity"] = currentHumidity; // Now sending real data instead of 50
    
    String json;
    serializeJson(doc, json);
    http.POST(json);
    http.end();
  }
}

void updateOLED() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  
  if (isAlertActive) {
    display.setTextSize(2); display.setCursor(0, 10); display.println("!! HOT !!");
  } else {
    display.setTextSize(1); display.setCursor(0, 0); 
    display.println("SCOLIO-SENSE (BERLIN)"); // UI indicator
    
    display.setCursor(0, 15);
    display.print("Status: "); 
    if (!sensorsEnabled) display.println("OFF");
    else display.println(isWorn ? "WORN" : "NOT WORN");

    display.setCursor(0, 30);
    display.print("Temp: "); display.print(currentTemp, 1); display.println(" C");
    display.print("Hum:  "); display.print(currentHumidity, 1); display.println(" %");
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
    // Distance Sensor
    if (now - lastUltrasonic >= 150) {
      lastUltrasonic = now;
      digitalWrite(TRIG_PIN, LOW); delayMicroseconds(2); digitalWrite(TRIG_PIN, HIGH);
      delayMicroseconds(10); digitalWrite(TRIG_PIN, LOW);
      long duration = pulseIn(ECHO_PIN, HIGH);
      if (duration > 0) currentDistance = duration * 0.034 / 2;
    }
    
    // DHT22 Sensor (Temp & Humidity)
    if (now - lastDHTRead >= 2000) {
      lastDHTRead = now;
      float t = dht.readTemperature();
      float h = dht.readHumidity(); // Fetching Humidity
      if (!isnan(t)) currentTemp = t;
      if (!isnan(h)) currentHumidity = h; // Updating Humidity variable
    }

    isWorn = (currentDistance <= DIST_THRESHOLD && currentTemp >= TEMP_THRESHOLD);
    isAlertActive = (currentTemp > MAX_TEMP_ALERT);

    if (now - lastAWSUpdate >= AWS_INTERVAL) { 
      sendToAWS(); 
      lastAWSUpdate = now; 
    }
  }

  // Event Logging Logic
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