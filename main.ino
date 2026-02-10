#include <WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>
#include "cred.h"

// ----------------- PIN CONFIG -----------------
#define DHTPIN 16
#define DHTTYPE DHT22

#define TRIG_PIN 4
#define ECHO_PIN 0

// Detection thresholds
#define DIST_THRESHOLD 5        // cm
#define TEMP_THRESHOLD 28.5     // °C

// ---------------------------------------------

// Objects
WiFiClient espClient;
PubSubClient client(espClient);
DHT dht(DHTPIN, DHTTYPE);

// Timing
unsigned long lastDHTRead = 0;
const unsigned long DHT_INTERVAL = 1000; // fastest safe rate

// Fast temperature smoothing
#define TEMP_SAMPLES 3
float tempBuffer[TEMP_SAMPLES];
int tempIndex = 0;
bool bufferFilled = false;

// Wear state
bool lastIsWorn = false;

// ------------------------------------------------
void setup_wifi() {
  Serial.print("Connecting to WiFi");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");
}

// ------------------------------------------------
void reconnect() {
  while (!client.connected()) {
    if (client.connect("ESP32S2Client", mqtt_user, mqtt_pass)) {
      Serial.println("MQTT connected");
    } else {
      delay(2000);
    }
  }
}

// ------------------------------------------------
void setup() {
  Serial.begin(115200);
  Serial.println("BOOT OK");

  dht.begin();
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  setup_wifi();
  client.setServer(mqtt_server, mqtt_port);
}

// ------------------------------------------------
long readUltrasonicDistanceCM() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  long duration = pulseIn(ECHO_PIN, HIGH, 30000);
  if (duration == 0) return -1;
  return duration * 0.034 / 2;
}

// ------------------------------------------------
float getFastAveragedTemp(float newTemp) {
  tempBuffer[tempIndex++] = newTemp;
  if (tempIndex >= TEMP_SAMPLES) {
    tempIndex = 0;
    bufferFilled = true;
  }

  float sum = 0;
  int count = bufferFilled ? TEMP_SAMPLES : tempIndex;
  for (int i = 0; i < count; i++) sum += tempBuffer[i];
  return sum / count;
}

// ------------------------------------------------
void loop() {
  if (!client.connected()) reconnect();
  client.loop();

  // ---- Read ultrasonic continuously ----
  long distance = readUltrasonicDistanceCM();
  if (distance <= 0) return;

  // ---- Read DHT22 as fast as allowed ----
  if (millis() - lastDHTRead < DHT_INTERVAL) return;
  lastDHTRead = millis();

  float tempC = dht.readTemperature();
  if (isnan(tempC)) return;

  float avgTemp = getFastAveragedTemp(tempC);

  // ---- Wear detection ----
  bool isWorn = (distance < DIST_THRESHOLD && avgTemp > TEMP_THRESHOLD);

  // ---- STATUS-ONLY MQTT UPDATE ----
  if (isWorn != lastIsWorn) {
    client.publish("okjazim/feeds/status", isWorn ? "WORN" : "NOT WORN");

    Serial.println(isWorn ? "Status: WORN" : "Status: NOT WORN");
    Serial.print("Distance: ");
    Serial.print(distance);
    Serial.print(" cm | Temp: ");
    Serial.print(avgTemp);
    Serial.println(" °C");

    lastIsWorn = isWorn;
  }
}