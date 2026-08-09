// XIAO ESP32C6 - grounding team MQTT broker: LED control + A0 sensor publish
// Topics are namespaced under "grounding/<DEVICE_NAME>/..." - see SKILL.md.
#include <WiFi.h>
#include <PubSubClient.h>
#include "arduino_secrets.h"

#define LED_PIN 15 // 내장 LED, active-low (LOW = 켜짐, HIGH = 꺼짐)
#define TOPIC_BASE "grounding"

WiFiClient espClient;
PubSubClient mqtt(espClient);

char topicLedSet[64];
char topicLedState[64];
char topicSensor[64];
char topicStatus[64];

bool ledOn = false;

void setLed(bool on) {
  ledOn = on;
  digitalWrite(LED_PIN, on ? LOW : HIGH);
  mqtt.publish(topicLedState, on ? "on" : "off", true); // retained
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String msg;
  for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];

  if (strcmp(topic, topicLedSet) == 0) {
    if (msg == "on") setLed(true);
    else if (msg == "off") setLed(false);
    else if (msg == "toggle") setLed(!ledOn);
  }
}

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false); // 모뎀 슬립으로 MQTT 소켓이 끊기는 것 방지
  WiFi.begin(SECRET_SSID, SECRET_PASS);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("WiFi connected, IP: ");
  Serial.println(WiFi.localIP());
}

void connectMQTT() {
  while (!mqtt.connected()) {
    Serial.print("Connecting to grounding MQTT broker...");
    // LWT: 비정상 종료 시 브로커가 대신 "offline"을 retained로 발행
    if (mqtt.connect(DEVICE_NAME, topicStatus, 0, true, "offline")) {
      Serial.println("connected");
      mqtt.publish(topicStatus, "online", true);
      mqtt.subscribe(topicLedSet);
      mqtt.publish(topicLedState, ledOn ? "on" : "off", true);
    } else {
      Serial.print("failed, rc=");
      Serial.print(mqtt.state());
      Serial.println(" retrying in 2s");
      delay(2000);
    }
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH); // 꺼진 상태로 시작

  snprintf(topicLedSet, sizeof(topicLedSet), TOPIC_BASE "/%s/led/set", DEVICE_NAME);
  snprintf(topicLedState, sizeof(topicLedState), TOPIC_BASE "/%s/led/state", DEVICE_NAME);
  snprintf(topicSensor, sizeof(topicSensor), TOPIC_BASE "/%s/sensor/a0", DEVICE_NAME);
  snprintf(topicStatus, sizeof(topicStatus), TOPIC_BASE "/%s/status", DEVICE_NAME);

  connectWiFi();

  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(mqttCallback);
  connectMQTT();
}

unsigned long lastSensorPublish = 0;

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
  }
  if (!mqtt.connected()) {
    connectMQTT();
  }
  mqtt.loop();

  unsigned long now = millis();
  if (now - lastSensorPublish >= 2000) {
    lastSensorPublish = now;
    int raw = analogRead(A0);
    uint32_t mv = analogReadMilliVolts(A0);
    char payload[48];
    snprintf(payload, sizeof(payload), "{\"raw\":%d,\"mv\":%lu}", raw, (unsigned long)mv);
    mqtt.publish(topicSensor, payload);
  }
}
