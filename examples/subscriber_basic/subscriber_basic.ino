/**
 * subscriber_basic.ino
 *
 * Minimal OPP2 subscriber example for ESP32.
 *
 * Demonstrates:
 *   - Subscribing to all messages from all pistes
 *   - Using the OPP2 dispatcher and SystemState mirror
 *   - Printing received messages to Serial
 *
 * Run this alongside apparatus_basic on the same broker to see
 * all published messages arrive and be decoded in real time.
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

// ── Globals ───────────────────────────────────────────────────────────────────

WiFiClient    wifiClient;
PubSubClient  mqtt(wifiClient);

// Dispatcher routes incoming messages to typed callbacks.
OPP2::Dispatcher dispatcher;

// SystemState keeps a live mirror of all retained topics.
// For a multi-piste setup, maintain one per piste_id.
OPP2::SystemState pisteState;

// ── OPP2 callbacks ────────────────────────────────────────────────────────────

void onLights(const OPP2::Topic& t, const OPP2::Lights& msg) {
    Serial.printf("[%s] LIGHTS  left=%s right=%s\n",
        t.piste_id,
        msg.left.on_target  ? "RED " : "off ",
        msg.right.on_target ? "GRN " : "off ");
    if (msg.left.white)  Serial.printf("         left white (off-target)\n");
    if (msg.right.white) Serial.printf("         right white (off-target)\n");
}

void onClock(const OPP2::Topic& t, const OPP2::Clock& msg) {
    Serial.printf("[%s] CLOCK   %s  running=%s\n",
        t.piste_id,
        msg.time,
        msg.running ? "yes" : "no");
}

void onScore(const OPP2::Topic& t, const OPP2::Score& msg) {
    Serial.printf("[%s] SCORE   left=%-2d  right=%-2d  priority=%s\n",
        t.piste_id,
        msg.left.score,
        msg.right.score,
        msg.priority == OPP2::Priority::LEFT  ? "LEFT"  :
        msg.priority == OPP2::Priority::RIGHT ? "RIGHT" : "none");
}

void onState(const OPP2::Topic& t, const OPP2::ApparatusStateMsg& msg) {
    const char* names[] = {"FENCING","HALT","PAUSE","WAITING","ENDING","UNKNOWN"};
    int idx = static_cast<int>(msg.state);
    Serial.printf("[%s] STATE   %s\n",
        t.piste_id, names[idx < 5 ? idx : 5]);
}

void onConnection(const OPP2::Topic& t, const OPP2::Connection& msg) {
    if (msg.online) {
        Serial.printf("[%s] CONNECT online  device=%s  fw=%s\n",
            t.piste_id,
            msg.device_present     ? msg.device     : "unknown",
            msg.fw_version_present ? msg.fw_version : "unknown");
    } else {
        Serial.printf("[%s] CONNECT offline (LWT)\n", t.piste_id);
    }
}

void onControl(const OPP2::Topic& t, const OPP2::Control& msg) {
    const char* side =
        msg.side == OPP2::Side::LEFT  ? " side=left"  :
        msg.side == OPP2::Side::RIGHT ? " side=right" : "";

    // Map command enum to string for display
    const char* cmd = "UNKNOWN";
    switch (msg.command) {
        case OPP2::Command::BEGIN:                cmd = "BEGIN";                 break;
        case OPP2::Command::HALT:                 cmd = "HALT";                  break;
        case OPP2::Command::RESET:                cmd = "RESET";                 break;
        case OPP2::Command::VALIDATE:             cmd = "VALIDATE";              break;
        case OPP2::Command::NEXT:                 cmd = "NEXT";                  break;
        case OPP2::Command::PREV:                 cmd = "PREV";                  break;
        case OPP2::Command::END:                  cmd = "END";                   break;
        case OPP2::Command::ACK:                  cmd = "ACK";                   break;
        case OPP2::Command::NAK:                  cmd = "NAK";                   break;
        case OPP2::Command::MEDICAL:              cmd = "MEDICAL";               break;
        case OPP2::Command::RESERVE:              cmd = "RESERVE";               break;
        case OPP2::Command::VIDEO_REVIEW_REQUEST: cmd = "VIDEO_REVIEW_REQUEST";  break;
        case OPP2::Command::VIDEO_REVIEW_GRANTED: cmd = "VIDEO_REVIEW_GRANTED";  break;
        case OPP2::Command::VIDEO_REVIEW_DENIED:  cmd = "VIDEO_REVIEW_DENIED";   break;
        default: break;
    }
    Serial.printf("[%s] CONTROL %s%s\n", t.piste_id, cmd, side);
}

void onError(const OPP2::Topic& t, OPP2::DispatchResult result,
             OPP2::DeserializeError err) {
    Serial.printf("[%s] ERROR   dispatch=%d deserialize=%d\n",
        t.piste_id[0] ? t.piste_id : "?",
        static_cast<int>(result),
        static_cast<int>(err));
}

// ── MQTT callback ─────────────────────────────────────────────────────────────

void mqttCallback(char* topic, byte* payload, unsigned int length) {
    // Hand directly to the OPP2 dispatcher — it does everything else
    dispatcher.dispatch(topic, reinterpret_cast<const char*>(payload), length);
}

// ── MQTT ──────────────────────────────────────────────────────────────────────

void mqttConnect() {
    Serial.printf("Connecting to MQTT broker at %s:%d ...\n",
                  BROKER_HOST, BROKER_PORT);
    while (!mqtt.connected()) {
        String clientId = String("OPP2-sub-") + String(millis());
        if (mqtt.connect(clientId.c_str())) {
            Serial.println("MQTT connected.");
            // Subscribe to all OPP2 messages from all pistes
            mqtt.subscribe("openpiste/#");
            Serial.println("Subscribed to openpiste/#");
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
    Serial.println("\n=== OPP2 Subscriber Basic Example ===");

    // Register callbacks — only the types you care about
    dispatcher.onLights         = onLights;
    dispatcher.onClock          = onClock;
    dispatcher.onScore          = onScore;
    dispatcher.onApparatusState = onState;
    dispatcher.onConnection     = onConnection;
    dispatcher.onControl        = onControl;
    dispatcher.onError          = onError;

    // Attach SystemState — updated automatically on every retained message
    dispatcher.setSystemState(&pisteState);

    wifiConnect();
    mqtt.setServer(BROKER_HOST, BROKER_PORT);
    mqtt.setBufferSize(512);
    mqtt.setCallback(mqttCallback);
    mqttConnect();

    Serial.println("Listening for OPP2 messages...\n");
}

void loop() {
    if (!mqtt.connected()) mqttConnect();
    mqtt.loop();
}
