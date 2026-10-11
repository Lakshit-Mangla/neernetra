/*
 * NeerNetra - ESP32 firmware
 * Libraries (Library Manager): PubSubClient, ArduinoJson, Adafruit ADS1X15,
 *   OneWire, DallasTemperature, Adafruit SSD1306, Adafruit GFX
 * Board: "ESP32 Dev Module"
 */
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <Adafruit_ADS1X15.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Adafruit_SSD1306.h>
#include <time.h>
#include "secrets.h"

#define ONE_WIRE_PIN 4
#define DIVIDER_RATIO 1.5f      // 10k top / 20k bottom divider => multiply back by 1.5
#define SAMPLES 20
#define PUBLISH_MS 15000
const float LAT = 28.8955f, LON = 76.6066f;   // change to your sample location

// pH calibration: measure voltage in pH 4.01 and 9.18 buffer, then update these.
float V_PH4 = 3.05f, V_PH9 = 2.45f;

Adafruit_ADS1115 ads;
OneWire oneWire(ONE_WIRE_PIN);
DallasTemperature tempSensor(&oneWire);
Adafruit_SSD1306 oled(128, 64, &Wire, -1);
WiFiClientSecure net;
PubSubClient mqtt(net);
String topic = String("neernetra/") + DEVICE_ID + "/data";
unsigned long lastPub = 0;
bool adsOk = false;   // FIX: track ADS1115 init status so we don't publish garbage

float readVolts(uint8_t ch) {            // averaged, divider-corrected
  float sum = 0;
  for (int i = 0; i < SAMPLES; i++) { sum += ads.computeVolts(ads.readADC_SingleEnded(ch)); delay(10); }
  return (sum / SAMPLES) * DIVIDER_RATIO;
}

float phFromV(float v) {
  float slope = (9.18f - 4.01f) / (V_PH9 - V_PH4);
  return 4.01f + slope * (v - V_PH4);
}

float tdsFromV(float v, float t) {       // DFRobot-style formula, temp compensated
  float k = 1.0f + 0.02f * (t - 25.0f);
  float vc = v / k;
  return (133.42f * vc * vc * vc - 255.86f * vc * vc + 857.39f * vc) * 0.5f;
}

float ntuFromV(float v) {                // approximate, indicative only
  if (v < 2.5f) return 3000;
  float n = -1120.4f * v * v + 5742.3f * v - 4352.9f;
  return n < 0 ? 0 : n;
}

void connectWifi() {
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connecting to Wi-Fi...");
  while (WiFi.status() != WL_CONNECTED) { delay(300); Serial.print("."); }
  Serial.println("\nWi-Fi connected!");
  Serial.print("IP address: "); Serial.println(WiFi.localIP());

  Serial.println("Waiting for internet time...");
  configTime(19800, 0, "pool.ntp.org", "time.google.com", "time.cloudflare.com"); // 19800 = IST offset in seconds
  int tries = 0;
  while (time(nullptr) < 1700000000 && tries < 60) {
    delay(500);
    Serial.print(".");
    tries++;
  }
  if (time(nullptr) < 1700000000) {
    Serial.println("\nTime sync FAILED, continuing anyway (certs may fail).");
  } else {
    Serial.println("\nTime synchronized!");
  }
}

// MQTT state codes (PubSubClient):
//  -4 MQTT_CONNECTION_TIMEOUT      -3 MQTT_CONNECTION_LOST
//  -2 MQTT_CONNECT_FAILED (TLS/TCP handshake failed - check endpoint/certs)
//  -1 MQTT_DISCONNECTED
//   1 MQTT_CONNECT_BAD_PROTOCOL     2 MQTT_CONNECT_BAD_CLIENT_ID
//   3 MQTT_CONNECT_UNAVAILABLE      4 MQTT_CONNECT_BAD_CREDENTIALS
//   5 MQTT_CONNECT_UNAUTHORIZED (policy/certificate not allowed - check IoT policy)
void connectMqtt() {
  while (!mqtt.connected()) {
    Serial.print("Free heap: "); Serial.println(ESP.getFreeHeap());

    // Step 1: raw TCP+TLS connect, separate from MQTT, to see exactly where it stalls
    Serial.print("Raw TLS connect to "); Serial.print(AWS_ENDPOINT); Serial.print(":8883 ...");
    unsigned long t0 = millis();
    bool tlsOk = net.connect(AWS_ENDPOINT, 8883);
    Serial.print(tlsOk ? " OK" : " FAILED");
    Serial.print(" ("); Serial.print(millis() - t0); Serial.println(" ms)");

    if (!tlsOk) {
      Serial.println("-> TCP/TLS handshake itself failed or timed out. Likely port 8883 blocked, or cert/key/CA mismatch.");
      delay(3000);
      continue;
    }
    net.stop();  // close this test connection, let PubSubClient open its own

    // Step 2: real MQTT connect
    Serial.print("MQTT connecting as "); Serial.print(DEVICE_ID); Serial.print(" ...");
    t0 = millis();
    if (mqtt.connect(DEVICE_ID)) {
      Serial.println(" connected!");
    } else {
      Serial.print(" failed, rc="); Serial.print(mqtt.state());
      Serial.print(" ("); Serial.print(millis() - t0); Serial.println(" ms)");
      delay(2000);
    }
  }
}

void show(float ph, float tds, float ntu, float t) {
  oled.clearDisplay(); oled.setTextSize(1); oled.setTextColor(WHITE); oled.setCursor(0, 0);
  oled.printf("pH   : %.2f\nTDS  : %.0f mg/L\nTurb : %.1f NTU\nTemp : %.1f C", ph, tds, ntu, t);
  oled.display();
}

void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22);
  ads.setGain(GAIN_ONE);                  // +-4.096V

  adsOk = ads.begin();                    // FIX: store result instead of discarding it
  if (!adsOk) {
    Serial.println("ERROR: ADS1115 not detected! Sensor readings will be skipped until fixed.");
  } else {
    Serial.println("ADS1115 detected successfully!");
  }

  tempSensor.begin();
  oled.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  connectWifi();
  net.setCACert(AWS_CA);
  net.setCertificate(AWS_CERT);
  net.setPrivateKey(AWS_KEY);
  net.setHandshakeTimeout(30);     // give slow networks more time for TLS handshake
  mqtt.setServer(AWS_ENDPOINT, 8883);
  mqtt.setBufferSize(512);
  mqtt.setSocketTimeout(30);       // default is 15s, bump it up
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) connectWifi();
  connectMqtt();
  mqtt.loop();

  if (millis() - lastPub < PUBLISH_MS) return;

  // FIX: don't read/publish garbage if the ADC never initialized.
  // Retries ads.begin() each cycle in case it was a transient wiring/power issue.
  if (!adsOk) {
    Serial.println("Skipping read: ADS1115 not available, retrying init...");
    adsOk = ads.begin();
    lastPub = millis();
    return;
  }

  Serial.println("Starting sensor read");

  lastPub = millis();

  tempSensor.requestTemperatures();

  float t = tempSensor.getTempCByIndex(0);
  if (t < -50) t = 25;                    // sensor missing fallback

  float vph = readVolts(0), vtds = readVolts(1), vntu = readVolts(2);

  float ph  = phFromV(vph);
  float tds = tdsFromV(vtds, t);
  float ntu = ntuFromV(vntu);
  Serial.printf("V: ph=%.3f tds=%.3f turb=%.3f\n", vph, vtds, vntu);  // use for calibration
  show(ph, tds, ntu, t);

  StaticJsonDocument<300> doc;
  doc["device_id"] = DEVICE_ID;
  doc["ts"] = (long)time(nullptr);
  doc["ph"] = ph; doc["tds"] = tds; doc["turbidity"] = ntu; doc["temp"] = t;
  doc["lat"] = LAT; doc["lon"] = LON;
  char buf[320];
  size_t n = serializeJson(doc, buf);

  // FIX: publish once, reuse the result instead of calling mqtt.publish() twice
  bool ok = mqtt.publish(topic.c_str(), (uint8_t*)buf, n);
  Serial.println(ok ? "Publish success" : "Publish failed");
}
