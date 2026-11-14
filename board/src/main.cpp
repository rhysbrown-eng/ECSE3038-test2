#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <DHTesp.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

#include "secrets.h"

const char *API_URL = "http://iot-rhysbrown.northcentralus.cloudapp.azure.com";

const char *MQTT_NAME = "realrhys";

const char *BROKER_HOST = "135.232.203.144";
const uint16_t BROKER_PORT = 1883;

const unsigned long WIFI_TIMEOUT_MS = 15000;
const unsigned long RETRY_EVERY_MS = 5000;    // wait between reconnect attempts
const int DHT_PIN = 15;                       // the sensor's data wire
const unsigned long SEND_EVERY_MS = 10000;    // one reading every 10 s
const int LED_PIN = 2;                        // the LED, through a 220 ohm resistor

DHTesp dht;
WiFiClient net;                               // the TCP connection to the broker
PubSubClient mqtt(net);                       // MQTT on top of it

String statusTopic;
String settingsTopic;
String stateTopic;

unsigned long lastSend = 0;
unsigned long lastAttempt = 0;
String fanMode = "off";                       // off until the first settings arrive
float maxHumidity = NAN;                      // no limit until the broker gives one
float lastHumidity = NAN;                     // no reading yet
String reportedLed = "";                      // what we last told the broker: "on", "off" or nothing yet

int update_led();

bool connect_wifi() {
  Serial.print("Connecting to ");
  Serial.print(WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD, WIFI_CHANNEL);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - start > WIFI_TIMEOUT_MS) {
      Serial.println();
      Serial.println("No Wi-Fi after 15 s. Trying again in 5 s.");
      return false;
    }
    delay(250);
    Serial.print(".");
  }

  Serial.println(" connected");
  Serial.print("MAC address: ");
  Serial.println(WiFi.macAddress());
  return true;
}

//b1
void send_reading(float humidity) {
  char body[80];
  snprintf(body, sizeof(body), "{\"mac\": \"%s\", \"humidity\": %.1f}",
           WiFi.macAddress().c_str(), humidity);

  HTTPClient http;
  http.begin(String(API_URL) + "/readings");
  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-API-Key", DEVICE_KEY);
  int status = http.POST(body);

  Serial.print("POST ");
  Serial.print(body);
  Serial.print(" -> ");
  Serial.println(status);
  if (status == 401) {
    Serial.println("Unknown key: register this board with POST /fans, then put the key in secrets.h.");
  } else if (status < 0) {
    Serial.println(http.errorToString(status));
  }
  http.end();
}

bool connect_mqtt() {
  String clientId = "esp32-" + String(MQTT_NAME) + "-" + String((uint32_t)ESP.getEfuseMac(), HEX);
  Serial.print("Connecting to broker ");
  Serial.print(BROKER_HOST);
  Serial.print(" as ");
  Serial.println(clientId);

  bool ok = mqtt.connect(clientId.c_str(),
                          statusTopic.c_str(),
                          1,
                          true,
                          "offline"
                        );
  if (!ok) {
    Serial.print("Broker said no, state ");
    Serial.println(mqtt.state());             // -2 = no TCP connection at all
    return false;
  }
  Serial.println("Connected to broker");

  //b2
  mqtt.publish(statusTopic.c_str(), "online", true);

  mqtt.subscribe(settingsTopic.c_str());

  update_led();                               // reports "off" if nothing has been reported yet
  return true;
}

//b3
bool parse_settings(const byte *payload, unsigned int length, String &mode, float &limit) {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload, length);
  if (err) {
    Serial.println("Not JSON.");
    return false;
  }
  if (!doc["mode"].is<const char *>() || !doc["max_humidity"].is<float>()) {
    Serial.println("Field missing or wrong type.");
    return false;
  }
  String newMode = doc["mode"].as<String>();
  if (newMode != "auto" && newMode != "on" && newMode != "off") {
    Serial.println("Bad mode.");
    return false;
  }
  mode = newMode;
  limit = doc["max_humidity"].as<float>();
  return true;
}

void on_message(char *topic, byte *payload, unsigned int length) {
  String message;
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  message.trim();

  String t = topic;

  Serial.print("Got ");
  Serial.print(topic);
  Serial.print(" = ");
  Serial.println(message);

  if (t == settingsTopic) {
      String newMode;
      float newLimit;
      if (parse_settings(payload, length, newMode, newLimit)) {
          fanMode = newMode;
          maxHumidity = newLimit;
          Serial.print("Updated settings: ");
          Serial.print(fanMode);
          Serial.print(", ");
          Serial.println(maxHumidity);
          update_led(); // React immediately as per B4
      } else {
          Serial.println("Bad settings: unchanged.");
      }
  }
}

//b4, b5
int update_led() {
  bool fanOn = false;
  if (fanMode == "on") {
    fanOn = true;
  } else if (fanMode == "auto" && !isnan(maxHumidity) && !isnan(lastHumidity) && lastHumidity > maxHumidity) {
    fanOn = true;
  }

  String led = "off";
  if (fanOn) {
    led = "on";
  }
  if (fanOn) {
    digitalWrite(LED_PIN, HIGH);
  } else {
    digitalWrite(LED_PIN, LOW);
  }

  Serial.print("Mode: ");
  Serial.print(fanMode);
  Serial.print("  Limit: ");
  Serial.print(maxHumidity);
  Serial.print("  Humidity: ");
  Serial.print(lastHumidity);
  Serial.print("  -> LED ");
  Serial.println(led);

  if(led != reportedLed && mqtt.connected()){
    mqtt.publish(stateTopic.c_str(), led.c_str(), true);
    reportedLed = led;
  }
  return 1;
}

void setup() {
  Serial.begin(115200);
  Serial.println("Hello from the ESP32");
  dht.setup(DHT_PIN, DHTesp::DHT22);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  String base = "ecse3038/" + String(MQTT_NAME);
  statusTopic = base + "/status";
  settingsTopic = base + "/fan/settings";
  stateTopic = base + "/fan/state";

  mqtt.setServer(BROKER_HOST, BROKER_PORT);
  mqtt.setCallback(on_message);
  mqtt.setKeepAlive(15);                      // broker gives up on us after ~1.5 x 15 s of silence

  connect_wifi();
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    if (millis() - lastAttempt >= RETRY_EVERY_MS) {
      lastAttempt = millis();
      connect_wifi();
    }
    return;
  }

  // The broker: reconnect if needed, at most one try every 5 s.
  if (mqtt.connected()) {
    mqtt.loop();                              // keepalive pings, and messages to onMessage()
  } else if (millis() - lastAttempt >= RETRY_EVERY_MS) {
    lastAttempt = millis();
    connect_mqtt();
  }

  // Readings every 10 s.
  if (millis() - lastSend < SEND_EVERY_MS) {
    return;
  }
  lastSend = millis();

  float humidity = dht.getHumidity();
  if (isnan(humidity)) {
    Serial.println("No reading from the sensor. Check the three wires.");
    return;
  }
  lastHumidity = humidity;
  send_reading(humidity);
  update_led();
}