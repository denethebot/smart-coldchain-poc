#include "WiFi.h"
#include "PubSubClient.h"
#include "OneWire.h"
#include "DallasTemperature.h"
#include "TinyGPSPlus.h"

const char* WIFI_SSID = "Wokwi-GUEST";
const char* WIFI_PASS = "";
const char* MQTT_HOST = "broker.hivemq.com";
const int   MQTT_PORT = 1883;
const char* TOPIC     = "shipments/xk29q7/s1/telemetry";

const char* SMS_DISPATCHER = "+94 77 000 0001";
const char* SMS_DRIVER     = "+94 77 000 0002";

WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);

#define TEMP_PIN 15
#define DOOR_PIN 27
#define GPS_RX   16
#define GPS_TX   17
#define GPS_BAUD 9600
#define NET_PIN  4
#define LED_PIN  25

OneWire oneWire(TEMP_PIN);
DallasTemperature sensors(&oneWire);

const int BUF_SIZE = 600;
const unsigned long SAMPLE_INTERVAL = 3000;
const int FLUSH_PER_PASS = 10;
const unsigned long LIVE_AGE_S = 5;
const unsigned long RECONNECT_EVERY = 5000;
const unsigned long SMS_REPEAT = 60000;

struct Reading {
  uint32_t ts;
  float temp;
  float lat;
  float lon;
  uint8_t gpsFix;
  uint8_t sats;
  uint8_t door;
  uint8_t risk;
};

enum Risk : uint8_t { RISK_NORMAL = 0, RISK_WARNING = 1, RISK_CRITICAL = 2 };
const char* RISK_NAMES[] = {"NORMAL", "WARNING", "CRITICAL"};

// CRITICAL: < 2.0 or > 8.0
// WARNING : 2.0 - 2.4 and 7.6 - 8.0
// NORMAL  : 2.4 - 7.6
uint8_t getRiskLevel(float temperature) {
  if (2.0 > temperature || temperature > 8.0) return RISK_CRITICAL;
  if (2.4 > temperature || temperature > 7.6) return RISK_WARNING;
  return RISK_NORMAL;
}

TinyGPSPlus gps;
HardwareSerial gpsSerial(2);

float latitude = 7.8731;
float longitude = 80.7718;
bool gpsFix = false;
int satellites = 0;

void updateGps() {
  while (gpsSerial.available() > 0) gps.encode(gpsSerial.read());
  if (gps.location.isValid() && 5000 > gps.location.age()) {
    latitude = gps.location.lat();
    longitude = gps.location.lng();
    gpsFix = true;
  } else {
    gpsFix = false;
  }
  satellites = gps.satellites.isValid() ? gps.satellites.value() : 0;
}

Reading buf[BUF_SIZE];
int head = 0;
int tail = 0;
int count = 0;

int nextIdx(int i) {
  i++;
  if (i >= BUF_SIZE) i = 0;
  return i;
}

void bufferPush(const Reading &r) {
  if (count == BUF_SIZE) {
    tail = nextIdx(tail);
    count--;
    Serial.println("WARNING: buffer full, oldest reading overwritten");
  }
  buf[head] = r;
  head = nextIdx(head);
  count++;
}

uint32_t ageSeconds(const Reading &r) {
  return (millis() / 1000) - r.ts;
}

bool lastSwitch = true;
unsigned long lastTry = 0;

bool networkSwitchOn() {
  return digitalRead(NET_PIN) == HIGH;
}

void goOffline() {
  if (mqtt.connected()) mqtt.disconnect();
  WiFi.disconnect(true);
}

bool ensureConnected() {
  if (WiFi.status() == WL_CONNECTED && mqtt.connected()) return true;
  if (lastTry != 0 && RECONNECT_EVERY > millis() - lastTry) return false;
  lastTry = millis();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.print("Connecting to WiFi");
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS, 6);
    unsigned long t = millis();
    while (WiFi.status() != WL_CONNECTED && 8000 > millis() - t) {
      updateGps();
      delay(250);
      Serial.print(".");
    }
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println(" failed");
      return false;
    }
    Serial.println(" connected");
  }

  if (!mqtt.connected()) {
    Serial.print("Connecting to MQTT...");
    String clientId = "esp32-s1-" + String(random(0xffff), HEX);
    if (mqtt.connect(clientId.c_str())) {
      Serial.println(" connected");
      return true;
    }
    Serial.print(" failed, rc=");
    Serial.println(mqtt.state());
    return false;
  }
  return true;
}

bool publishReading(const Reading &r) {
  uint32_t age = ageSeconds(r);
  bool delayed = age > LIVE_AGE_S;

  char payload[300];
  snprintf(payload, sizeof(payload),
           "{\"ts\":%lu,\"age_s\":%lu,\"delayed\":%s,\"temp_c\":%.2f,\"lat\":%.6f,\"lon\":%.6f,"
           "\"gps_fix\":%s,\"sats\":%d,\"door\":\"%s\",\"risk\":\"%s\"}",
           (unsigned long)r.ts, (unsigned long)age, delayed ? "true" : "false",
           r.temp, r.lat, r.lon,
           r.gpsFix ? "true" : "false", r.sats,
           r.door ? "OPEN" : "CLOSED", RISK_NAMES[r.risk]);

  return mqtt.publish(TOPIC, payload);
}

bool flushing = false;

void flushBuffer() {
  if (count == 0) return;

  bool backlog = ageSeconds(buf[tail]) > LIVE_AGE_S;
  if (backlog && !flushing) {
    flushing = true;
    Serial.println("================================");
    Serial.print("FLUSH: sending ");
    Serial.print(count);
    Serial.println(" stored readings");
  }

  int sent = 0;
  while (count > 0 && FLUSH_PER_PASS > sent) {
    const Reading &r = buf[tail];
    uint32_t age = ageSeconds(r);
    if (!publishReading(r)) {
      Serial.println("Publish failed, will retry later");
      return;
    }
    if (age > LIVE_AGE_S) {
      Serial.print("  sent stored reading, taken at ");
      Serial.print(r.ts);
      Serial.print("s, waited ");
      Serial.print(age);
      Serial.println("s in buffer");
    }
    tail = nextIdx(tail);
    count--;
    sent++;
    mqtt.loop();
  }

  if (count == 0 && flushing) {
    flushing = false;
    Serial.println("FLUSH complete, buffer empty");
    Serial.println("================================");
  }
}

// ---------------- SIMULATED SMS (used only when there is no data signal) ----------------
int smsActive = 0;   // 0 = normal, 1 = was critical and is now WARNING, 2 = CRITICAL now
unsigned long lastSms = 0;
int smsCount = 0;

void sendSms(const char* label, const Reading &r) {
  char msg[161];
  snprintf(msg, sizeof(msg), "%s Temp %.1fC Door %s Loc %.5f,%.5f",
           label, r.temp, r.door ? "OPEN" : "CLOSED", r.lat, r.lon);
  smsCount++;

  Serial.println();
  Serial.println("########## SMS SENT (no data signal) ##########");
  Serial.print("To      : Dispatcher ");
  Serial.println(SMS_DISPATCHER);
  Serial.print("To      : Driver     ");
  Serial.println(SMS_DRIVER);
  Serial.print("Message : ");
  Serial.println(msg);
  Serial.print("Length  : ");
  Serial.print(strlen(msg));
  Serial.println(" characters");
  Serial.print("SMS no. : ");
  Serial.println(smsCount);
  Serial.println("###############################################");
  Serial.println();
}

void handleSms(const Reading &r) {
  if (r.risk == RISK_CRITICAL) {
    if (smsActive != 2 || millis() - lastSms >= SMS_REPEAT) {
      smsActive = 2;
      lastSms = millis();
      sendSms("CRITICAL", r);
    }
  } else if (r.risk == RISK_WARNING) {
    if (smsActive == 2) {
      smsActive = 1;
      sendSms("IMPROVING (WARNING)", r);
    }
  } else {
    if (smsActive != 0) {
      smsActive = 0;
      sendSms("BACK TO NORMAL", r);
    }
  }
}

void sampleAndStore(bool online) {
  sensors.requestTemperatures();
  float temperature = sensors.getTempCByIndex(0);
  if (temperature == DEVICE_DISCONNECTED_C) {
    Serial.println("Temperature sensor not found, skipping reading");
    return;
  }

  Reading r;
  r.ts = millis() / 1000;
  r.temp = temperature;
  r.lat = latitude;
  r.lon = longitude;
  r.gpsFix = gpsFix ? 1 : 0;
  r.sats = satellites;
  r.door = (digitalRead(DOOR_PIN) == LOW) ? 1 : 0;
  r.risk = getRiskLevel(temperature);

  bufferPush(r);

  Serial.println("--------------------------------");
  Serial.print("Temperature: "); Serial.print(temperature); Serial.println(" C");
  Serial.print("Door: ");        Serial.println(r.door ? "OPEN" : "CLOSED");
  Serial.print("GPS Fix: ");     Serial.println(gpsFix ? "yes" : "no");
  Serial.print("Latitude: ");    Serial.println(latitude, 6);
  Serial.print("Longitude: ");   Serial.println(longitude, 6);
  Serial.print("Risk Level: ");  Serial.println(RISK_NAMES[r.risk]);

  if (online) {
    smsActive = 0;
    Serial.println("Network: ONLINE, sending live");
    Serial.println("--------------------------------");
  } else {
    Serial.print("Network: OFFLINE, BUFFERED. Readings in buffer: ");
    Serial.println(count);
    Serial.println("--------------------------------");
    handleSms(r);
  }
}

void updateLed() {
  if (count > 0) digitalWrite(LED_PIN, (millis() / 300) & 1);
  else digitalWrite(LED_PIN, LOW);
}

void setup() {
  Serial.begin(115200);
  gpsSerial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX, GPS_TX);
  sensors.begin();
  pinMode(DOOR_PIN, INPUT_PULLUP);
  pinMode(NET_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);

  Serial.println("================================");
  Serial.println("VACCINE COLD-CHAIN MONITOR");
  Serial.println("with local data buffering");
  Serial.println("================================");

  lastSwitch = networkSwitchOn();
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setBufferSize(512);
}

unsigned long lastSample = 0;

void loop() {
  updateGps();

  bool sw = networkSwitchOn();
  if (sw != lastSwitch) {
    lastSwitch = sw;
    if (sw) {
      Serial.println(">>> NETWORK ONLINE (switch ON)");
      lastTry = 0;
    } else {
      Serial.println(">>> NETWORK OFFLINE (switch OFF)");
      goOffline();
    }
  }

  bool online = sw && ensureConnected();
  if (online) mqtt.loop();

  if (millis() - lastSample >= SAMPLE_INTERVAL) {
    lastSample = millis();
    sampleAndStore(online);
  }

  if (online) flushBuffer();

  updateLed();
}
