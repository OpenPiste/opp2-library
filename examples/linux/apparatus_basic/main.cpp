/**
 * main.cpp — OPP2 apparatus basic example (Linux)
 *
 * Simulates a minimal scoring apparatus on Linux using the mosquitto
 * C library and the OPP2 library.
 *
 * Demonstrates:
 *   - Connecting to an MQTT broker with a Last Will and Testament
 *   - Publishing connection, state, clock, lights, and score messages
 *   - Using the OPP2 serializer and topic builder
 *
 * Build:
 *   mkdir build && cd build && cmake .. && make
 *
 * Run:
 *   ./apparatus_basic
 *
 * Dependencies:
 *   libmosquitto-dev   (sudo apt install libmosquitto-dev)
 *   ArduinoJson        (fetched automatically by CMake via FetchContent)
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <mosquitto.h>
#include "opp2.h"

// ── Configuration ─────────────────────────────────────────────────────────────

static const char* BROKER_HOST  = "openpiste.local";
static const int   BROKER_PORT  = 1883;
static const char* PISTE_ID     = "1";
static const char* DEVICE_NAME  = "OpenPiste-Linux";
static const char* FW_VERSION   = "0.1.0";
static const int   KEEPALIVE    = 60;

// ── Globals ───────────────────────────────────────────────────────────────────

static struct mosquitto* mosq      = nullptr;
static uint32_t          seqCounter = 0;
static volatile bool     running   = true;

static char payload[OPP2::JSON_SIZE_MAX];
static char topic[64];

// ── Signal handler ────────────────────────────────────────────────────────────

static void onSignal(int) { running = false; }

// ── Helpers ───────────────────────────────────────────────────────────────────

static uint32_t nextSeq() { return ++seqCounter; }

// Get current time in milliseconds (monotonic clock)
static uint64_t nowMs() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + ts.tv_nsec / 1000000ULL;
}

// Get current UTC epoch in milliseconds (for NTP timestamps)
static uint64_t epochMs() {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + ts.tv_nsec / 1000000ULL;
}

// Publish with QoS 1, retained by default
static void opp2Publish(OPP2::Publisher pub, OPP2::MessageType mt,
                         const char* json, bool retained, int qos = 1) {
    OPP2::TopicParser::buildFrom(PISTE_ID, pub, mt, topic, sizeof(topic));
    int rc = mosquitto_publish(mosq, nullptr, topic,
                               (int)strlen(json), json, qos, retained);
    if (rc != MOSQ_ERR_SUCCESS) {
        fprintf(stderr, "publish failed: %s\n", mosquitto_strerror(rc));
    }
}

// ── OPP2 message publishers ───────────────────────────────────────────────────

static void publishConnection(bool online) {
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
                payload, true);
    printf("[OPP2] connection: online=%s\n", online ? "true" : "false");
}

static void publishState(OPP2::ApparatusState state) {
    OPP2::ApparatusStateMsg msg;
    msg.seq   = nextSeq();
    msg.state = state;
    OPP2::Serializer::serialize(msg, payload, sizeof(payload));
    opp2Publish(OPP2::Publisher::APPARATUS, OPP2::MessageType::APPARATUS_STATE,
                payload, true);

    const char* names[] = {"FENCING","HALT","PAUSE","WAITING","ENDING","UNKNOWN"};
    int idx = static_cast<int>(state);
    printf("[OPP2] state: %s\n", names[idx < 5 ? idx : 5]);
}

static void publishClock(bool running_flag, uint32_t time_ms) {
    OPP2::Clock msg;
    msg.ts      = OPP2::Timestamp::fromEpochMs(epochMs());
    msg.running = running_flag;
    msg.time_ms = time_ms;
    OPP2::Serializer::serialize(msg, payload, sizeof(payload));
    opp2Publish(OPP2::Publisher::APPARATUS, OPP2::MessageType::CLOCK,
                payload, true, 0);  // QoS 0 for clock
}

static void publishLights(bool leftOnTarget, bool rightOnTarget,
                           bool leftWhite = false, bool rightWhite = false) {
    OPP2::Lights msg;
    msg.seq             = nextSeq();
    msg.ts              = OPP2::Timestamp::fromEpochMs(epochMs());
    msg.left.on_target  = leftOnTarget;
    msg.left.white      = leftWhite;
    msg.right.on_target = rightOnTarget;
    msg.right.white     = rightWhite;
    OPP2::Serializer::serialize(msg, payload, sizeof(payload));
    opp2Publish(OPP2::Publisher::APPARATUS, OPP2::MessageType::LIGHTS,
                payload, true);
    printf("[OPP2] lights: left=%s right=%s\n",
        leftOnTarget  ? "ON " : "off",
        rightOnTarget ? "ON " : "off");
}

static void publishScore(int leftScore, int rightScore) {
    OPP2::Score msg;
    msg.seq          = nextSeq();
    msg.left.score   = leftScore;
    msg.left.status  = OPP2::FencerStatus::UNDEFINED;
    msg.right.score  = rightScore;
    msg.right.status = OPP2::FencerStatus::UNDEFINED;
    msg.priority     = OPP2::Priority::NONE;
    OPP2::Serializer::serialize(msg, payload, sizeof(payload));
    opp2Publish(OPP2::Publisher::APPARATUS, OPP2::MessageType::SCORE,
                payload, true);
    printf("[OPP2] score: left=%d right=%d\n", leftScore, rightScore);
}

// ── Mosquitto callbacks ───────────────────────────────────────────────────────

static void onConnect(struct mosquitto*, void*, int rc) {
    if (rc == 0) {
        printf("Connected to broker.\n");
        publishConnection(true);
        publishState(OPP2::ApparatusState::WAITING);
        publishScore(0, 0);
        publishLights(false, false);
    } else {
        fprintf(stderr, "Connect failed: %s\n", mosquitto_connack_string(rc));
    }
}

static void onDisconnect(struct mosquitto*, void*, int rc) {
    if (rc != 0) {
        fprintf(stderr, "Unexpected disconnect (rc=%d) — reconnecting...\n", rc);
    }
}

// ── Main ──────────────────────────────────────────────────────────────────────

int main() {
    signal(SIGINT,  onSignal);
    signal(SIGTERM, onSignal);

    printf("=== OPP2 Apparatus Basic Example (Linux) ===\n");
    printf("Broker: %s:%d  Piste: %s\n\n", BROKER_HOST, BROKER_PORT, PISTE_ID);

    mosquitto_lib_init();
    mosq = mosquitto_new(nullptr, true, nullptr);
    if (!mosq) {
        fprintf(stderr, "Failed to create mosquitto instance\n");
        return 1;
    }

    // Set Last Will and Testament before connecting
    char lwtTopic[64];
    OPP2::TopicParser::buildLwtTopic(PISTE_ID, lwtTopic, sizeof(lwtTopic));
    const char* lwtPayload = "{\"online\":false}";
    mosquitto_will_set(mosq, lwtTopic, (int)strlen(lwtPayload),
                       lwtPayload, 1, true);

    mosquitto_connect_callback_set(mosq, onConnect);
    mosquitto_disconnect_callback_set(mosq, onDisconnect);

    int rc = mosquitto_connect(mosq, BROKER_HOST, BROKER_PORT, KEEPALIVE);
    if (rc != MOSQ_ERR_SUCCESS) {
        fprintf(stderr, "Could not connect to %s:%d — %s\n",
                BROKER_HOST, BROKER_PORT, mosquitto_strerror(rc));
        mosquitto_destroy(mosq);
        mosquitto_lib_cleanup();
        return 1;
    }

    mosquitto_loop_start(mosq);  // starts a background thread for MQTT I/O

    // Wait for connection to establish
    sleep(1);

    // ── Simulate a bout ───────────────────────────────────────────────────────

    printf("\nStarting bout simulation...\n");
    publishState(OPP2::ApparatusState::FENCING);

    uint64_t boutStart = nowMs();
    uint64_t lastClock = 0;
    bool     scored    = false;
    bool     halted    = false;

    while (running) {
        uint64_t now     = nowMs();
        uint32_t elapsed = (uint32_t)(now - boutStart);

        // Publish clock every second
        if (now - lastClock >= 1000) {
            lastClock = now;
            publishClock(true, elapsed);
        }

        // At 5 seconds: left fencer scores
        if (!scored && elapsed >= 5000) {
            scored = true;
            publishLights(true, false);
            publishScore(1, 0);
        }

        // At 6 seconds: lights off, halt
        if (!halted && elapsed >= 6000) {
            halted  = true;
            running = false;
            publishLights(false, false);
            publishState(OPP2::ApparatusState::HALT);
            publishClock(false, elapsed);
            printf("\nHalt — touch awarded to left fencer.\n");
        }

        usleep(10000);  // 10ms
    }

    // Clean disconnect — publishes online=false ourselves (not LWT)
    sleep(1);  // let final messages flush
    publishConnection(false);
    sleep(1);

    mosquitto_loop_stop(mosq, true);
    mosquitto_destroy(mosq);
    mosquitto_lib_cleanup();

    printf("Done.\n");
    return 0;
}
