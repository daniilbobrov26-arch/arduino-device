#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <LittleFS.h>

// ===== WIFI =====
const char* WIFI_SSID = "realm";
const char* WIFI_PASSWORD = "123456789";

// ===== MQTT =====
const char* MQTT_HOST = "185.200.241.15";
const int MQTT_PORT = 1883;

const char* MQTT_CLIENT_ID = "esp8266-client";
const char* MQTT_COMMAND_TOPIC = "iot/devices/commands";
const char* MQTT_STATUS_TOPIC = "iot/devices/status";

// ===== RELAYS =====
const char* RELAY_1_UUID = "relay-1";
const int RELAY_1_PIN = D1;

const char* RELAY_2_UUID = "relay-2";
const int RELAY_2_PIN = D2;

// Если реле включается LOW, поставь true
const bool RELAY_ACTIVE_LOW = false;

WiFiClient espClient;
PubSubClient mqtt(espClient);

bool relay1State = false;
bool relay2State = falseф;

void writeRelay(int pin, bool state) {
  if (RELAY_ACTIVE_LOW) {
    digitalWrite(pin, state ? LOW : HIGH);
  } else {
    digitalWrite(pin, state ? HIGH : LOW);
  }
}

void saveState() {
  File file = LittleFS.open("/state.json", "w");

  if (!file) {
    Serial.println("Failed to open state file for writing");
    return;
  }

  StaticJsonDocument<256> doc;

  doc[RELAY_1_UUID] = relay1State;
  doc[RELAY_2_UUID] = relay2State;

  serializeJson(doc, file);
  file.close();
}

void loadState() {
  if (!LittleFS.exists("/state.json")) {
    return;
  }

  File file = LittleFS.open("/state.json", "r");

  if (!file) {
    Serial.println("Failed to open state file for reading");
    return;
  }

  StaticJsonDocument<256> doc;
  DeserializationError error = deserializeJson(doc, file);

  if (error) {
    Serial.println("Failed to parse state file");
    file.close();
    return;
  }

  relay1State = doc[RELAY_1_UUID] | false;
  relay2State = doc[RELAY_2_UUID] | false;

  file.close();
}

void applyStates() {
  writeRelay(RELAY_1_PIN, relay1State);
  writeRelay(RELAY_2_PIN, relay2State);
}

void publishStatus(const char* uuid, bool status) {
  StaticJsonDocument<256> doc;

  doc["uuid"] = uuid;
  doc["status"] = status;

  char buffer[256];
  serializeJson(doc, buffer);

  mqtt.publish(MQTT_STATUS_TOPIC, buffer, true);
}

void handleCommand(const String& uuid, const String& action) {
  bool state = action == "on";

  if (uuid == RELAY_1_UUID) {
    relay1State = state;
    writeRelay(RELAY_1_PIN, state);
    publishStatus(RELAY_1_UUID, state);
    saveState();
    return;
  }

  if (uuid == RELAY_2_UUID) {
    relay2State = state;
    writeRelay(RELAY_2_PIN, state);
    publishStatus(RELAY_2_UUID, state);
    saveState();
    return;
  }

  Serial.println("Unknown relay UUID: " + uuid);
}

void callback(char* topic, byte* payload, unsigned int length) {
  String msg;

  for (unsigned int i = 0; i < length; i++) {
    msg += (char)payload[i];
  }

  Serial.print("MQTT message: ");
  Serial.println(msg);

  StaticJsonDocument<256> doc;
  DeserializationError error = deserializeJson(doc, msg);

  if (error) {
    Serial.println("Invalid JSON");
    return;
  }

  String uuid = doc["uuid"] | "";
  String action = doc["action"] | "";

  if (uuid == "" || action == "") {
    Serial.println("Missing uuid or action");
    return;
  }

  if (action != "on" && action != "off") {
    Serial.println("Invalid action");
    return;
  }

  handleCommand(uuid, action);
}

void connectWiFi() {
  Serial.print("Connecting to WiFi");

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
  }

  Serial.println();
  Serial.print("WiFi connected. IP: ");
  Serial.println(WiFi.localIP());
}

void reconnectMqtt() {
  while (!mqtt.connected()) {
    Serial.print("Connecting to MQTT... ");

    if (mqtt.connect(MQTT_CLIENT_ID)) {
      Serial.println("connected");

      mqtt.subscribe(MQTT_COMMAND_TOPIC);

      publishStatus(RELAY_1_UUID, relay1State);
      publishStatus(RELAY_2_UUID, relay2State);
    } else {
      Serial.print("failed, rc=");
      Serial.println(mqtt.state());

      delay(5000);
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);

  pinMode(RELAY_1_PIN, OUTPUT);
  pinMode(RELAY_2_PIN, OUTPUT);

  if (!LittleFS.begin()) {
    Serial.println("LittleFS mount failed");
  }

  loadState();
  applyStates();

  connectWiFi();

  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(callback);
}

void loop() {
  if (!mqtt.connected()) {
    reconnectMqtt();
  }

  mqtt.loop();
}