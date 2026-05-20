/**
 * apparatus_basic.ino
 *
 * Minimal OPP2 apparatus example for ESP32.
 *
 * Demonstrates:
 *   - Connecting to an MQTT broker with a Last Will and Testament
 *   - Publishing connection, state, clock, lights, and score messages
 *   - Using the OPP2 serializer and topic builder
 *
 * Hardware: any ESP32 board
 * Broker:   Mosquitto at openpiste.local:1883
 *
 * Dependencies (add to platformio.ini lib_deps):
 *   knolleary/PubSubClient @ ^2.8
 *   bblanchon/ArduinoJson @ ^7.0.0
 *   https://github.com/OpenPiste/opp2-library
 *
 * SPDX-License-Identifier: MIT
 */

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <opp2.h>

// ── Configuration ─────────────────────────────────────────────────────────────

const char* WIFI_SSID     = "your-wifi-ssid";
const char* WIFI_PASSWORD = "your-wifi-password";
const char* BROKER_HOST   = OPP2::DEFAULT_BROKER_HOST;  // "openpiste.local"
const int   BROKER_PORT   = OPP2::DEFAULT_BROKER_PORT;  // 1883
const char* PISTE_ID      = "1";
const char* DEVICE_NAME   = "OpenPiste-ESP32";
const char* FW_VERSION    = "0.1.0";

// ── Globals ───────────────────────────────────────────────────────────────────

WiFiClient   wifiClient;
PubSubClient mqtt(wifiClient);

uint32_t seqCounter   = 0;
uint32_t lastClockMs  = 0;
uint32_t boutStartMs  = 0;
bool     boutRunning  = false;

// JSON payload buffer — sized for the largest message we send
char payload[OPP2::JSON_SIZE_MAX];
char topic[64];

// ── Helpers ───────────────────────────────────────────────────────────────────

uint32_t nextSeq() { return ++seqCounter; }

// Publish a serialized message with the correct topic, QoS, and retain flag.
// Returns true on success.
bool opp2Publish(OPP2::Publisher pub, OPP2::MessageType mt,
                 const char* json, bool retained, uint8_t qos = 1) {
    OPP2::TopicParser::buildFrom(PISTE_ID, pub, mt, topic, sizeof(topic));
    // PubSubClient publish(topic, payload, retained) uses QoS 1 internally
    return mqtt.publish(topic, json, retained);
}

// ── OPP2 message publishers ───────────────────────────────────────────────────

void publishConnection(bool online) {
    OPP2::Connection msg;
    msg.seq    = nextSeq();
    msg.online = online;
    if (online) {
        msg.device_present = true;
        strncpy(msg.device, DEVICE_NAME, sizeof(msg.device) - 1);
        msg.fw_version_present = true;
        strncpy(msg.fw_version, FW_VERSION, sizeof(msg.fw_version) - 1);
    }
    OPP2::Serializer::serialize(msg, payload, sizeof(payload));
    opp2Publish(OPP2::Publisher::APPARATUS, OPP2::MessageType::CONNECTION,
                payload, /*retained=*/true);
    Serial.printf("[OPP2] connection: online=%s\n", online ? "true" : "false");
}

void publishState(OPP2::ApparatusState state) {
    OPP2::ApparatusStateMsg msg;
    msg.seq   = nextSeq();
    msg.state = state;
    OPP2::Serializer::serialize(msg, payload, sizeof(payload));
    opp2Publish(OPP2::Publisher::APPARATUS, OPP2::MessageType::APPARATUS_STATE,
                payload, /*retained=*/true);

    const char* names[] = {"FENCING","HALT","PAUSE","WAITING","ENDING","UNKNOWN"};
    Serial.printf("[OPP2] state: %s\n",
        names[static_cast<int>(state) < 5 ? static_cast<int>(state) : 5]);
}

void publishClock(bool running, uint32_t time_ms) {
    OPP2::Clock msg;
    msg.ts      = OPP2::Timestamp::fromEpochMs(millis()); // use NTP in production
    msg.running = running;
    msg.time_ms = time_ms;
    OPP2::Serializer::serialize(msg, payload, sizeof(payload));
    opp2Publish(OPP2::Publisher::APPARATUS, OPP2::MessageType::CLOCK,
                payload, /*retained=*/true, /*qos=*/0);
}

void publishLights(bool leftOnTarget, bool rightOnTarget,
                   bool leftWhite = false, bool rightWhite = false) {
    OPP2::Lights msg;
    msg.seq              = nextSeq();
    msg.ts               = OPP2::Timestamp::fromEpochMs(millis());
    msg.left.on_target   = leftOnTarget;
    msg.left.white       = leftWhite;
    msg.right.on_target  = rightOnTarget;
    msg.right.white      = rightWhite;
    OPP2::Serializer::serialize(msg, payload, sizeof(payload));
    opp2Publish(OPP2::Publisher::APPARATUS, OPP2::MessageType::LIGHTS,
                payload, /*retained=*/true);
    Serial.printf("[OPP2] lights: left=%s right=%s\n",
        leftOnTarget ? "ON" : "off", rightOnTarget ? "ON" : "off");
}

void publishScore(int leftScore, int rightScore) {
    OPP2::Score msg;
    msg.seq              = nextSeq();
    msg.left.score       = leftScore;
    msg.left.status      = OPP2::FencerStatus::UNDEFINED;
    msg.right.score      = rightScore;
    msg.right.status     = OPP2::FencerStatus::UNDEFINED;
    msg.priority         = OPP2::Priority::NONE;
    OPP2::Serializer::serialize(msg, payload, sizeof(payload));
    opp2Publish(OPP2::Publisher::APPARATUS, OPP2::MessageType::SCORE,
                payload, /*retained=*/true);
    Serial.printf("[OPP2] score: left=%d right=%d\n", leftScore, rightScore);
}

// ── MQTT ──────────────────────────────────────────────────────────────────────

void setupLWT() {
    // Build the LWT topic and payload before connecting.
    // The broker publishes this automatically if we disconnect unexpectedly.
    char lwtTopic[64];
    OPP2::TopicParser::buildLwtTopic(PISTE_ID, lwtTopic, sizeof(lwtTopic));
    const char* lwtPayload = "{\"online\":false}";
    mqtt.setWill(lwtTopic, lwtPayload, /*retained=*/true, /*qos=*/1);
}

void mqttConnect() {
    setupLWT();
    Serial.printf("Connecting to MQTT broker at %s:%d ...\n",
                  BROKER_HOST, BROKER_PORT);
    while (!mqtt.connected()) {
        String clientId = String("OPP2-") + PISTE_ID + "-" + String(millis());
        if (mqtt.connect(clientId.c_str())) {
            Serial.println("MQTT connected.");
            publishConnection(true);
            publishState(OPP2::ApparatusState::WAITING);
            publishScore(0, 0);
            publishLights(false, false);
        } else {
            Serial.printf("MQTT connect failed, rc=%d — retrying in 5s\n",
                          mqtt.state());
            delay(5000);
        }
    }
}

// ── WiFi ──────────────────────────────────────────────────────────────────────

void wifiConnect() {
    Serial.printf("Connecting to WiFi: %s\n", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.printf("\nWiFi connected. IP: %s\n",
                  WiFi.localIP().toString().c_str());
}

// ── Setup / Loop ──────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n=== OPP2 Apparatus Basic Example ===");

    wifiConnect();
    mqtt.setServer(BROKER_HOST, BROKER_PORT);
    mqtt.setBufferSize(512);  // increase from default 256
    mqttConnect();

    // Simulate: start a bout after 3 seconds
    delay(3000);
    boutRunning  = true;
    boutStartMs  = millis();
    publishState(OPP2::ApparatusState::FENCING);
    Serial.println("Bout started.");
}

void loop() {
    if (!mqtt.connected()) mqttConnect();
    mqtt.loop();

    uint32_t now = millis();

    // Publish clock every second while bout is running
    if (boutRunning && now - lastClockMs >= 1000) {
        lastClockMs = now;
        uint32_t elapsed = now - boutStartMs;
        publishClock(true, elapsed);

        // Simulate: left fencer scores at 5 seconds
        if (elapsed >= 5000 && elapsed < 6000) {
            publishLights(true, false);   // left on-target (red)
            publishScore(1, 0);
        }

        // Simulate: lights off at 6 seconds, halt
        if (elapsed >= 6000 && elapsed < 7000) {
            publishLights(false, false);
            publishState(OPP2::ApparatusState::HALT);
            publishClock(false, elapsed);
            boutRunning = false;
            Serial.println("Halt — touch awarded to left fencer.");
        }
    }

    delay(10);
}
