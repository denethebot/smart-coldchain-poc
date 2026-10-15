#include <WiFi.h>
#include <PubSubClient.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <TinyGPSPlus.h>

// -------------------- WIFI / MQTT --------------------
const char* WIFI_SSID = "Wokwi-GUEST";
const char* WIFI_PASS = "";
const char* MQTT_HOST = "broker.hivemq.com";
const int   MQTT_PORT = 1883;
const char* TOPIC     = "shipments/xk29q7/s1/telemetry";

WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);

// -------------------- PINS --------------------
#define TEMP_PIN 15
#define DOOR_PIN 27
#define GPS_RX   16   // ESP32 RX2 <- GPS TX
#define GPS_TX   17   // ESP32 TX2 -> GPS RX
#define GPS_BAUD 9600

OneWire oneWire(TEMP_PIN);
DallasTemperature sensors(&oneWire);

// -------------------- GPS --------------------
TinyGPSPlus gps;
HardwareSerial gpsSerial(2);

float latitude   = 7.8731;    // centre of Sri Lanka until a position arrives
float longitude  = 80.7718;
bool  gpsFix     = false;
int   satellites = 0;

void updateGps() {
  // Always keep feeding the GPS parser
  while (gpsSerial.available() > 0) gps.encode(gpsSerial.read());

  if (gps.location.isValid() && gps.location.age() < 5000) {
    latitude  = gps.location.lat();
    longitude = gps.location.lng();
    gpsFix    = true;
  } else {
    gpsFix = false;
  }
  satellites = gps.satellites.isValid() ? gps.satellites.value() : 0;
}

// -------------------- RISK --------------------
String getRiskLevel(float temperature) {
  if (temperature < 2.0 || temperature > 10.0) return "CRITICAL";
  if (temperature < 2.5 || temperature > 8.0)  return "WARNING";
  return "NORMAL";
}

// -------------------- CONNECTIONS --------------------
void connectWifi() {
  Serial.print("Connecting to WiFi");
  WiFi.begin(WIFI_SSID, WIFI_PASS, 6);
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
  gpsSerial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX, GPS_TX);
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
unsigned long lastPublish = 0;
const unsigned long PUBLISH_INTERVAL = 3000;

void loop() {
  updateGps();                    // must run often

  if (WiFi.status() != WL_CONNECTED) connectWifi();
  if (!mqtt.connected()) connectMqtt();
  mqtt.loop();

  if (millis() - lastPublish < PUBLISH_INTERVAL) return;
  lastPublish = millis();

  sensors.requestTemperatures();
  float temperature = sensors.getTempCByIndex(0);
  if (temperature == DEVICE_DISCONNECTED_C) {
    Serial.println("Temperature sensor not found, skipping reading");
    return;
  }

  const char* doorStatus = (digitalRead(DOOR_PIN) == LOW) ? "OPEN" : "CLOSED";
  String riskLevel = getRiskLevel(temperature);

  char payload[256];
  snprintf(payload, sizeof(payload),
           "{\"ts\":%lu,\"temp_c\":%.2f,\"lat\":%.6f,\"lon\":%.6f,"
           "\"gps_fix\":%s,\"sats\":%d,\"door\":\"%s\",\"risk\":\"%s\"}",
           millis() / 1000, temperature, latitude, longitude,
           gpsFix ? "true" : "false", satellites,
           doorStatus, riskLevel.c_str());

  bool ok = mqtt.publish(TOPIC, payload);

  Serial.println("--------------------------------");
  Serial.print("Temperature: "); Serial.print(temperature); Serial.println(" C");
  Serial.print("Door: ");        Serial.println(doorStatus);
  Serial.print("GPS Fix: ");     Serial.println(gpsFix ? "yes" : "no");
  Serial.print("Latitude: ");    Serial.println(latitude, 6);
  Serial.print("Longitude: ");   Serial.println(longitude, 6);
  Serial.print("Risk Level: ");  Serial.println(riskLevel);
  Serial.print("Published: ");   Serial.println(ok ? "yes" : "FAILED");
  Serial.println("--------------------------------");
}
