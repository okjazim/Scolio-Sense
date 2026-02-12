#include <WiFi.h>
#include <HTTPClient.h>
#include <DHT.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "time.h"

// ----------------- CONFIG -----------------
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
#define SCREEN_ADDRESS 0x3C

// Pin mappings based on Scolio-Sense PDF 
#define DHTPIN 16
#define DHTTYPE DHT22
#define TRIG_PIN 4
#define ECHO_PIN 40      // GPIO6 per documentation 
#define BUTTON_PIN 5
#define OLED_SCL 7
#define OLED_SDA 8

// Thresholds [cite: 115, 117]
#define DIST_THRESHOLD 5
#define TEMP_THRESHOLD 28.5
#define MAX_TEMP_ALERT 32.0

// WiFi & AWS Config [cite: 124, 200]
const char* ssid = "Wokwi-GUEST";
const char* password = "";
const char* aws_endpoint = "https://your-api-id.execute-api.region.amazonaws.com/prod/sensor";

// NTP Settings
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 28800; // UTC+8 (Adjust for your location)
const int   daylightOffset_sec = 0;

// Objects
DHT dht(DHTPIN, DHTTYPE);
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Timing Intervals [cite: 93, 123]
unsigned long lastDHTRead = 0;
unsigned long lastUltrasonic = 0;
unsigned long lastAWSUpdate = 0;
unsigned long lastOLEDUpdate = 0;

const unsigned long DHT_INTERVAL = 2000;
const unsigned long ULTRASONIC_INTERVAL = 150;
const unsigned long AWS_INTERVAL = 10000; 
const unsigned long OLED_INTERVAL = 500; 

// State tracking [cite: 113, 118]
bool sensorsEnabled = true;
bool isWorn = false;
bool lastIsWorn = false;
bool lastSensorsEnabled = true;
bool isAlertActive = false;
bool lastAlertActive = false;

unsigned long buttonPressTime = 0;
const unsigned long DEBOUNCE_DELAY = 250;
float currentTemp = 0;
long currentDistance = 999;

// ------------------------------------------------

String getTimestamp() {
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    return "[Time Error]";
  }
  char timeStringBuff[25]; 
  strftime(timeStringBuff, sizeof(timeStringBuff), "[%Y-%m-%d %H:%M:%S]", &timeinfo);
  return String(timeStringBuff);
}

void setup_wifi() {
  Serial.print("Connecting to WiFi");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Connected.");
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
}

void sendToAWS(float temp, long dist, bool worn) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(aws_endpoint);
    http.addHeader("Content-Type", "application/json");

    // Construct JSON for AWS Lambda [cite: 199, 200]
    String jsonPayload = "{";
    jsonPayload += "\"temperature\":" + String(temp);
    jsonPayload += ",\"distance\":" + String(dist);
    jsonPayload += ",\"status\":\"" + String(worn ? "WORN" : "NOT WORN") + "\"";
    jsonPayload += "}";

    int httpResponseCode = http.POST(jsonPayload);
    http.end();
  }
}

void updateOLED() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  
  // High Priority: Temperature Alert [cite: 136, 138]
  if (sensorsEnabled && isWorn && currentTemp > MAX_TEMP_ALERT) {
    display.setTextSize(2);
    display.setCursor(0, 0);
    display.println("!! HOT !!");
    display.setTextSize(1);
    display.setCursor(0, 30);
    display.println("REMOVE BRACE NOW");
    display.print("Temp: "); display.print(currentTemp, 1); display.println("C");
  } 
  // Standard Monitor View
  else {
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("--- SCOLIO-SENSE ---");
    
    display.setCursor(0, 15);
    display.print("System: "); 
    display.println(sensorsEnabled ? "ACTIVE" : "OFF");

    display.setCursor(0, 30);
    display.print("Status: ");
    if (!sensorsEnabled) display.println("STANDBY");
    else display.println(isWorn ? "WORN" : "NOT WORN");

    display.setCursor(0, 45);
    display.print("T: "); display.print(currentTemp, 1); display.print(" C | ");
    display.print("D: "); display.print(currentDistance); display.println("cm");
  }
  display.display();
}

void handleSensors() {
  unsigned long now = millis();

  // Ultrasonic distance sampling [cite: 93]
  if (now - lastUltrasonic >= ULTRASONIC_INTERVAL) {
    lastUltrasonic = now;
    digitalWrite(TRIG_PIN, LOW);
    delayMicroseconds(2);
    digitalWrite(TRIG_PIN, HIGH);
    delayMicroseconds(10);
    digitalWrite(TRIG_PIN, LOW);
    long duration = pulseIn(ECHO_PIN, HIGH, 25000);
    if (duration > 0) currentDistance = duration * 0.034 / 2;
  }

  // Temperature sampling (DHT22 stabilized) [cite: 94, 191]
  if (now - lastDHTRead >= DHT_INTERVAL) {
    lastDHTRead = now;
    float t = dht.readTemperature();
    if (!isnan(t)) currentTemp = t;
  }

  // Wear detection logic [cite: 118]
  isWorn = (currentDistance < DIST_THRESHOLD && currentTemp > TEMP_THRESHOLD);
  isAlertActive = (isWorn && currentTemp > MAX_TEMP_ALERT);
}

void logStatusChange() {
  Serial.println("-------------------------");
  Serial.print(getTimestamp()); 
  Serial.print(" EVENT: ");
  
  if (sensorsEnabled != lastSensorsEnabled) {
    Serial.println(sensorsEnabled ? "SYSTEM ACTIVE" : "SYSTEM STANDBY");
  } else if (isAlertActive != lastAlertActive) {
    Serial.println(isAlertActive ? "HEAT ALERT TRIGGERED" : "ALERT CLEARED");
  } else {
    Serial.println(isWorn ? "STATUS: WORN" : "STATUS: NOT WORN");
  }
  
  Serial.print("Data: "); Serial.print(currentTemp, 1); Serial.print("C | ");
  Serial.print(currentDistance); Serial.println("cm");
  Serial.println("-------------------------");
}

void setup() {
  Serial.begin(115200);
  
  // Pin modes per hardware table 
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  dht.begin();
  Wire.begin(OLED_SDA, OLED_SCL);
  
  if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println("OLED Init Failed");
    for(;;);
  }
  
  display.clearDisplay();
  display.display();
  
  setup_wifi();
}

void loop() {
  unsigned long now = millis();

  // 1. Button Toggle (Active/Standby) 
  if (digitalRead(BUTTON_PIN) == LOW) {
    if (now - buttonPressTime > DEBOUNCE_DELAY) {
      sensorsEnabled = !sensorsEnabled;
      buttonPressTime = now;
      if (!sensorsEnabled) { 
        isWorn = false; 
        currentTemp = 0; 
        currentDistance = 999; 
        isAlertActive = false; 
      }
    }
  }

  // 2. Core Logic [cite: 89]
  if (sensorsEnabled) {
    handleSensors();

    // Periodic Cloud Update [cite: 124, 201]
    if (now - lastAWSUpdate >= AWS_INTERVAL) {
      lastAWSUpdate = now;
      sendToAWS(currentTemp, currentDistance, isWorn);
    }
  }

  // 3. Event-Based Change Detection (Serial + OLED Update) [cite: 96, 101]
  if (sensorsEnabled != lastSensorsEnabled || isWorn != lastIsWorn || isAlertActive != lastAlertActive) {
    logStatusChange();
    updateOLED();
    lastSensorsEnabled = sensorsEnabled;
    lastIsWorn = isWorn;
    lastAlertActive = isAlertActive;
  }

  // 4. Regular OLED Refresh [cite: 125]
  if (now - lastOLEDUpdate >= OLED_INTERVAL) {
    lastOLEDUpdate = now;
    updateOLED();
  }
}