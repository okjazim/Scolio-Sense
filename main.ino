#include <WiFi.h>
#include <HTTPClient.h>
#include <DHT.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ----------------- CONFIG -----------------
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
#define SCREEN_ADDRESS 0x3C

#define DHTPIN 16
#define DHTTYPE DHT22
#define TRIG_PIN 4
#define ECHO_PIN 17  
#define BUTTON_PIN 5
#define OLED_SCL 7
#define OLED_SDA 8

#define DIST_THRESHOLD 5
#define TEMP_THRESHOLD 28.5
#define MAX_TEMP_ALERT 32.0

// Objects
DHT dht(DHTPIN, DHTTYPE);
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Timing
unsigned long lastDHTRead = 0;
unsigned long lastUltrasonic = 0;
unsigned long lastAWSUpdate = 0;
unsigned long lastOLEDUpdate = 0;

const unsigned long DHT_INTERVAL = 2000;
const unsigned long ULTRASONIC_INTERVAL = 150;
const unsigned long AWS_INTERVAL = 10000; // Updated to 10s to save on AWS Lambda costs
const unsigned long OLED_INTERVAL = 500; 

// State tracking
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

void setup_wifi() {
  Serial.print("Connecting to WiFi");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Connected.");
}

void sendToAWS(float temp, long dist, bool worn) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(aws_endpoint);
    http.addHeader("Content-Type", "application/json");

    // Construct JSON Payload
    String jsonPayload = "{";
    jsonPayload += "\"temperature\":" + String(temp);
    jsonPayload += ",\"distance\":" + String(dist);
    jsonPayload += ",\"status\":\"" + String(worn ? "WORN" : "NOT WORN") + "\"";
    jsonPayload += "}";

    int httpResponseCode = http.POST(jsonPayload);
    
    if (httpResponseCode > 0) {
      // Success! (Optionally log response for debugging)
    } else {
      Serial.print("AWS Error Code: ");
      Serial.println(httpResponseCode);
    }
    http.end();
  }
}

void updateOLED() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  
  if (sensorsEnabled && isWorn && currentTemp > MAX_TEMP_ALERT) {
    display.setTextSize(2);
    display.setCursor(0, 0);
    display.println("!! HOT !!");
    display.setTextSize(1);
    display.setCursor(0, 30);
    display.println("REMOVE BRACE NOW");
  } else {
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("--- AWS MONITOR ---");
    display.setCursor(0, 15);
    display.print("System: "); display.println(sensorsEnabled ? "ACTIVE" : "OFF");
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
  if (now - lastDHTRead >= DHT_INTERVAL) {
    lastDHTRead = now;
    float t = dht.readTemperature();
    if (!isnan(t)) currentTemp = t;
  }
  isWorn = (currentDistance < DIST_THRESHOLD && currentTemp > TEMP_THRESHOLD);
  isAlertActive = (isWorn && currentTemp > MAX_TEMP_ALERT);
}

void logStatusChange() {
  Serial.println("-------------------------");
  Serial.print("EVENT: ");
  if (sensorsEnabled != lastSensorsEnabled) {
    Serial.println(sensorsEnabled ? "SYSTEM ACTIVATED" : "SYSTEM DEACTIVATED");
  } else if (isAlertActive != lastAlertActive) {
    Serial.println(isAlertActive ? "HEAT ALERT TRIGGERED" : "ALERT CLEARED");
  } else {
    Serial.println(isWorn ? "WORN" : "NOT WORN");
  }
  Serial.print("Data: "); Serial.print(currentTemp, 1); Serial.print("C | ");
  Serial.print(currentDistance); Serial.println("cm");
  Serial.println("-------------------------");
}

void setup() {
  Serial.begin(115200);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  dht.begin();
  Wire.begin(OLED_SDA, OLED_SCL);
  display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS);
  setup_wifi();
}

void loop() {
  unsigned long now = millis();

  // 1. Button Logic
  if (digitalRead(BUTTON_PIN) == LOW) {
    if (now - buttonPressTime > DEBOUNCE_DELAY) {
      sensorsEnabled = !sensorsEnabled;
      buttonPressTime = now;
      if (!sensorsEnabled) { isWorn = false; currentTemp = 0; currentDistance = 999; isAlertActive = false; }
    }
  }

  // 2. Sensor & Logic
  if (sensorsEnabled) {
    handleSensors();
    if (now - lastAWSUpdate >= AWS_INTERVAL) {
      lastAWSUpdate = now;
      sendToAWS(currentTemp, currentDistance, isWorn);
    }
  }

  // 3. Change Detection for Serial Monitor
  if (sensorsEnabled != lastSensorsEnabled || isWorn != lastIsWorn || isAlertActive != lastAlertActive) {
    logStatusChange();
    updateOLED();
    lastSensorsEnabled = sensorsEnabled;
    lastIsWorn = isWorn;
    lastAlertActive = isAlertActive;
  }

  // 4. OLED Refresh
  if (now - lastOLEDUpdate >= OLED_INTERVAL) {
    lastOLEDUpdate = now;
    updateOLED();
  }
}