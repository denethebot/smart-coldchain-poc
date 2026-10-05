#include <WiFi.h>
#include <PubSubClient.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// -------------------- WIFI / MQTT --------------------
const char* WIFI_SSID = "Wokwi-GUEST";   // Wokwi's virtual WiFi
const char* WIFI_PASS = "";
const char* MQTT_HOST = "broker.hivemq.com";
const int   MQTT_PORT = 1883;
// Must match the topic in your engine. Change xk29q7 to your own random string.
const char* TOPIC     = "shipments/xk29q7/s1/telemetry";

WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);

// -------------------- PIN DEFINITIONS --------------------
#define TEMP_PIN 15
#define DOOR_PIN 27

// -------------------- TEMPERATURE SENSOR --------------------
OneWire oneWire(TEMP_PIN);
DallasTemperature sensors(&oneWire);

// -------------------- SIMULATED GPS --------------------
// Starts at your original coordinates and drifts toward a target,
// so the "shipment" appears to move. Replace with a real GPS later.
float latitude  = 7.2906;
float longitude = 80.6337;
const float TARGET_LAT = 6.9271;   // heading toward Colombo
const float TARGET_LON = 79.8612;
const float STEP = 0.0008;         // degrees per reading

void moveGps() {
  if (latitude  > TARGET_LAT) latitude  -= STEP;
  if (longitude > TARGET_LON) longitude -= STEP;
}

// -------------------- RISK FUNCTION --------------------
String getRiskLevel(float temperature) {
  if (temperature < 2.0 || temperature > 10.0) return "CRITICAL";
  if (temperature < 2.5 || temperature > 8.0)  return "WARNING";
  return "NORMAL";
}

// -------------------- CONNECTIONS --------------------
void connectWifi() {
  Serial.print("Connecting to WiFi");
  WiFi.begin(WIFI_SSID, WIFI_PASS, 6);   // channel 6 for Wokwi
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
  }
  Serial.println(" connected");
}

void connectMqtt() {
  while (!mqtt.connected()) {
    Serial.print("Connecting to MQTT...");
    String clientId = "esp32-s1-" + String(random(0xffff), HEX);
    if (mqtt.connect(clientId.c_str())) {
      Serial.println(" connected");
    } else {
      Serial.print(" failed, rc=");
      Serial.println(mqtt.state());
      delay(2000);
    }
  }
}

// -------------------- SETUP --------------------
void setup() {
  Serial.begin(115200);
  sensors.begin();
  pinMode(DOOR_PIN, INPUT_PULLUP);

  Serial.println("================================");
  Serial.println("VACCINE COLD-CHAIN MONITOR");
  Serial.println("================================");

  connectWifi();
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  connectMqtt();
}

// -------------------- LOOP --------------------
void loop() {
  if (WiFi.status() != WL_CONNECTED) connectWifi();
  if (!mqtt.connected()) connectMqtt();
  mqtt.loop();

  // ---------- Read temperature ----------
  sensors.requestTemperatures();
  float temperature = sensors.getTempCByIndex(0);

  if (temperature == DEVICE_DISCONNECTED_C) {
    Serial.println("Temperature sensor not found, skipping reading");
    delay(2000);
    return;
  }

  // ---------- Read door status ----------
  const char* doorStatus = (digitalRead(DOOR_PIN) == LOW) ? "OPEN" : "CLOSED";

  // ---------- Move GPS, get risk ----------
  moveGps();
  String riskLevel = getRiskLevel(temperature);

  // ---------- Build JSON payload ----------
  char payload[200];
  snprintf(payload, sizeof(payload),
           "{\"ts\":%lu,\"temp_c\":%.2f,\"lat\":%.6f,\"lon\":%.6f,"
           "\"door\":\"%s\",\"risk\":\"%s\"}",
           millis() / 1000, temperature, latitude, longitude,
           doorStatus, riskLevel.c_str());

  // ---------- Publish ----------
  bool ok = mqtt.publish(TOPIC, payload);

  // ---------- Display ----------
  Serial.println("--------------------------------");
  Serial.print("Temperature: "); Serial.print(temperature); Serial.println(" C");
  Serial.print("Door: ");        Serial.println(doorStatus);
  Serial.print("Latitude: ");    Serial.println(latitude, 6);
  Serial.print("Longitude: ");   Serial.println(longitude, 6);
  Serial.print("Risk Level: ");  Serial.println(riskLevel);
  Serial.print("Published: ");   Serial.println(ok ? "yes" : "FAILED");
  Serial.println(payload);
  Serial.println("--------------------------------");

  delay(3000);
}