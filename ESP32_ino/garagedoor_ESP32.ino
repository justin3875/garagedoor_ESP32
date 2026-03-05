#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>

/* ================= 多 WiFi 設定 ================= */

struct WiFiCredential {
  const char* ssid;
  const char* password;
};

WiFiCredential wifiList[] = {
  {"asus-CBD2", "0931085693"},
  {"Dong17",    "999999999"}
};

const int WIFI_COUNT = sizeof(wifiList) / sizeof(wifiList[0]);

/* ================= MQTT 設定 ================= */

const char* MQTT_HOST = "5bd40b3a78d745bbbc7771290f59d786.s1.eu.hivemq.cloud";
const int   MQTT_PORT = 8883;
const char* MQTT_USER = "justin3875";
const char* MQTT_PASS = "Justin6266";

const char* TOPIC_CMD    = "garage/door/cmd";
const char* TOPIC_STATUS = "garage/door/status";

/* ================= 硬體設定 ================= */

#define RELAY_OPEN_PIN   26
#define RELAY_CLOSE_PIN  27
#define WIFI_LED_PIN     2
#define PRESS_TIME       500

/* ================= 物件 ================= */

WiFiClientSecure secureClient;
PubSubClient mqttClient(secureClient);

/* ================= 狀態變數 ================= */

String clientId;

// WiFi
int currentWiFiIndex = 0;
bool wifiConnecting = false;
unsigned long wifiAttemptTime = 0;

// MQTT
unsigned long lastMqttAttempt = 0;

// Relay
bool relayActive = false;
int  activeRelayPin = -1;
unsigned long relayStartTime = 0;

// LED
bool flashActive = false;
int  flashStep = 0;
unsigned long flashTimer = 0;
unsigned long breathTimer = 0;

/* ================= LED 控制 ================= */

void setLed(int brightness) {
  analogWrite(WIFI_LED_PIN, brightness);
}

void handleLED() {

  unsigned long now = millis();

  if (flashActive) {
    if (now - flashTimer >= 120) {
      flashTimer = now;
      setLed((flashStep % 2 == 0) ? 255 : 0);
      if (++flashStep >= 8) flashActive = false;
    }
    return;
  }

  if (WiFi.status() == WL_CONNECTED) {
    setLed(255);
  } else {
    if (now - breathTimer >= 15) {
      breathTimer = now;
      static int b = 0, d = 5;
      setLed(b);
      b += d;
      if (b <= 0 || b >= 255) d = -d;
    }
  }
}

/* ================= WiFi 備援連線 ================= */

void connectToWiFi(int index) {

  Serial.print("嘗試連接 WiFi: ");
  Serial.println(wifiList[index].ssid);

  WiFi.disconnect(true);
  WiFi.begin(wifiList[index].ssid, wifiList[index].password);

  wifiConnecting = true;
  wifiAttemptTime = millis();
}

void manageWiFi() {

  if (WiFi.status() == WL_CONNECTED) {
    wifiConnecting = false;
    return;
  }

  unsigned long now = millis();

  if (!wifiConnecting) {
    connectToWiFi(currentWiFiIndex);
    return;
  }

  if (now - wifiAttemptTime > 8000) {

    Serial.println("連線失敗，切換下一個基地台");

    currentWiFiIndex++;
    if (currentWiFiIndex >= WIFI_COUNT)
      currentWiFiIndex = 0;

    wifiConnecting = false;
  }
}

/* ================= MQTT Callback ================= */

void mqttCallback(char* topic, byte* payload, unsigned int length) {

  String msg = "";
  for (unsigned int i = 0; i < length; i++)
    msg += (char)payload[i];

  msg.trim();
  Serial.println("收到指令: " + msg);

  if (relayActive) return;

  int targetPin = -1;
  if (msg == "OPEN")  targetPin = RELAY_OPEN_PIN;
  if (msg == "CLOSE") targetPin = RELAY_CLOSE_PIN;

  if (targetPin != -1) {
    digitalWrite(targetPin, HIGH);
    relayActive = true;
    activeRelayPin = targetPin;
    relayStartTime = millis();

    flashActive = true;
    flashStep = 0;
    flashTimer = millis();
  }
}

/* ================= MQTT 管理（含 LWT） ================= */

void manageMQTT() {

  if (WiFi.status() != WL_CONNECTED) return;

  if (!mqttClient.connected()) {

    unsigned long now = millis();

    if (now - lastMqttAttempt > 5000) {

      lastMqttAttempt = now;

      Serial.print("嘗試連接 MQTT...");

      bool connected = mqttClient.connect(
        clientId.c_str(),
        MQTT_USER,
        MQTT_PASS,
        TOPIC_STATUS,
        1,
        true,
        "OFFLINE"
      );

      if (connected) {

        Serial.println("成功！");
        mqttClient.subscribe(TOPIC_CMD);

        mqttClient.publish(TOPIC_STATUS, "ONLINE", true);

      } else {
        Serial.print("失敗，錯誤碼=");
        Serial.println(mqttClient.state());
      }
    }

  } else {
    mqttClient.loop();
  }
}

/* ================= Setup ================= */

void setup() {

  Serial.begin(115200);

  pinMode(RELAY_OPEN_PIN, OUTPUT);
  pinMode(RELAY_CLOSE_PIN, OUTPUT);
  digitalWrite(RELAY_OPEN_PIN, LOW);
  digitalWrite(RELAY_CLOSE_PIN, LOW);

  pinMode(WIFI_LED_PIN, OUTPUT);

  WiFi.mode(WIFI_STA);

  secureClient.setInsecure();  // 若要正式安全，改用 setCACert()

  mqttClient.setServer(MQTT_HOST, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);

  uint64_t chipid = ESP.getEfuseMac();
  clientId = "ESP32_DOOR_" + String((uint32_t)(chipid >> 32), HEX);

  connectToWiFi(0);
}

/* ================= Loop ================= */

void loop() {

  unsigned long now = millis();

  // Relay 自動釋放
  if (relayActive) {
    if (now - relayStartTime >= PRESS_TIME) {
      digitalWrite(activeRelayPin, LOW);
      relayActive = false;
      Serial.println("Relay 已釋放");
    }
  }

  manageWiFi();
  manageMQTT();
  handleLED();
}