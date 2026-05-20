/**
 * main.cpp — OPP2 subscriber basic example (Linux)
 *
 * Subscribes to all OPP2 messages from all pistes and prints them
 * to stdout in a human-readable format.
 *
 * Demonstrates:
 *   - Subscribing to openpiste/# using the mosquitto C library
 *   - Using the OPP2 dispatcher and SystemState mirror
 *   - Handling all 12 message types
 *
 * Build:
 *   mkdir build && cd build && cmake .. && make
 *
 * Run:
 *   ./subscriber_basic
 *
 * Run alongside apparatus_basic (in another terminal) to see messages arrive.
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

static const char* BROKER_HOST = "openpiste.local";
static const int   BROKER_PORT = 1883;
static const int   KEEPALIVE   = 60;

// ── Globals ───────────────────────────────────────────────────────────────────

static OPP2::Dispatcher  dispatcher;
static OPP2::SystemState pisteState;   // live mirror of all retained topics
static volatile bool     running = true;

// ── Signal handler ────────────────────────────────────────────────────────────

static void onSignal(int) { running = false; }

// ── OPP2 callbacks ────────────────────────────────────────────────────────────

static void onLights(const OPP2::Topic& t, const OPP2::Lights& msg) {
    printf("[%s] LIGHTS   left=%-4s  right=%-4s",
        t.piste_id,
        msg.left.on_target  ? "RED"  : "off",
        msg.right.on_target ? "GRN"  : "off");
    if (msg.left.white)  printf("  left-white");
    if (msg.right.white) printf("  right-white");
    printf("  (seq=%u)\n", msg.seq);
}

static void onClock(const OPP2::Topic& t, const OPP2::Clock& msg) {
    printf("[%s] CLOCK    %s  running=%s\n",
        t.piste_id,
        msg.time,
        msg.running ? "yes" : "no");
}

static void onBladeContact(const OPP2::Topic& t, const OPP2::BladeContact& msg) {
    printf("[%s] BLADE    active=%s  ts=%llu\n",
        t.piste_id,
        msg.active ? "yes" : "no",
        (unsigned long long)msg.ts.value);
}

static void onScore(const OPP2::Topic& t, const OPP2::Score& msg) {
    printf("[%s] SCORE    left=%-2d  right=%-2d  priority=%s  (seq=%u)\n",
        t.piste_id,
        msg.left.score,
        msg.right.score,
        msg.priority == OPP2::Priority::LEFT  ? "LEFT"  :
        msg.priority == OPP2::Priority::RIGHT ? "RIGHT" : "none",
        msg.seq);
}

static void onConnection(const OPP2::Topic& t, const OPP2::Connection& msg) {
    if (msg.online) {
        printf("[%s] CONNECT  online   device=%-20s  fw=%s\n",
            t.piste_id,
            msg.device_present     ? msg.device     : "unknown",
            msg.fw_version_present ? msg.fw_version : "unknown");
    } else {
        printf("[%s] CONNECT  offline  (LWT or explicit disconnect)\n",
            t.piste_id);
    }
}

static void onApparatusState(const OPP2::Topic& t,
                              const OPP2::ApparatusStateMsg& msg) {
    const char* names[] = {"FENCING","HALT","PAUSE","WAITING","ENDING","UNKNOWN"};
    int idx = static_cast<int>(msg.state);
    printf("[%s] STATE    %s  (seq=%u)\n",
        t.piste_id, names[idx < 5 ? idx : 5], msg.seq);
}

static void onFencers(const OPP2::Topic& t, const OPP2::Fencers& msg) {
    printf("[%s] FENCERS  left=%-20s [%s]  right=%-20s [%s]\n",
        t.piste_id,
        msg.left.fencer.name,   msg.left.fencer.nation,
        msg.right.fencer.name,  msg.right.fencer.nation);
    if (msg.referee.present)
        printf("           referee=%s [%s]\n",
            msg.referee.name, msg.referee.nation);
}

static void onMatch(const OPP2::Topic& t, const OPP2::Match& msg) {
    const char* weapon =
        msg.weapon == OPP2::Weapon::FOIL  ? "foil"  :
        msg.weapon == OPP2::Weapon::EPEE  ? "epee"  :
        msg.weapon == OPP2::Weapon::SABRE ? "sabre" : "unknown";
    printf("[%s] MATCH    %s  competition=%s  phase=%s  round=%u\n",
        t.piste_id, weapon, msg.competition, msg.phase, msg.round);
}

static void onUW2F(const OPP2::Topic& t, const OPP2::UW2F& msg) {
    printf("[%s] UW2F     %s  left-P=%u  right-P=%u\n",
        t.piste_id, msg.time, msg.left.p_card, msg.right.p_card);
}

static void onMedical(const OPP2::Topic& t, const OPP2::Medical& msg) {
    if (msg.active) {
        printf("[%s] MEDICAL  active  side=%s  remaining=%s\n",
            t.piste_id,
            msg.side == OPP2::Side::LEFT ? "left" : "right",
            msg.remaining);
    } else {
        printf("[%s] MEDICAL  cleared\n", t.piste_id);
    }
}

static void onVideoReview(const OPP2::Topic& t, const OPP2::VideoReview& msg) {
    printf("[%s] VIDEO    left: %d remaining (%u calls)  "
           "right: %d remaining (%u calls)\n",
        t.piste_id,
        msg.left.remaining,  msg.left.call_count,
        msg.right.remaining, msg.right.call_count);
}

static void onControl(const OPP2::Topic& t, const OPP2::Control& msg) {
    const char* cmd = "UNKNOWN";
    switch (msg.command) {
        case OPP2::Command::BEGIN:                cmd = "BEGIN";                break;
        case OPP2::Command::HALT:                 cmd = "HALT";                 break;
        case OPP2::Command::RESET:                cmd = "RESET";                break;
        case OPP2::Command::VALIDATE:             cmd = "VALIDATE";             break;
        case OPP2::Command::NEXT:                 cmd = "NEXT";                 break;
        case OPP2::Command::PREV:                 cmd = "PREV";                 break;
        case OPP2::Command::END:                  cmd = "END";                  break;
        case OPP2::Command::ACK:                  cmd = "ACK";                  break;
        case OPP2::Command::NAK:                  cmd = "NAK";                  break;
        case OPP2::Command::MEDICAL:              cmd = "MEDICAL";              break;
        case OPP2::Command::RESERVE:              cmd = "RESERVE";              break;
        case OPP2::Command::VIDEO_REVIEW_REQUEST: cmd = "VIDEO_REVIEW_REQUEST"; break;
        case OPP2::Command::VIDEO_REVIEW_GRANTED: cmd = "VIDEO_REVIEW_GRANTED"; break;
        case OPP2::Command::VIDEO_REVIEW_DENIED:  cmd = "VIDEO_REVIEW_DENIED";  break;
        default: break;
    }
    const char* side =
        msg.side == OPP2::Side::LEFT  ? " side=left"  :
        msg.side == OPP2::Side::RIGHT ? " side=right" : "";
    const char* pub =
        t.publisher == OPP2::Publisher::APPARATUS ? "apparatus" :
        t.publisher == OPP2::Publisher::SOFTWARE  ? "software"  :
        t.publisher == OPP2::Publisher::REMOTE    ? "remote"    : "unknown";
    printf("[%s] CONTROL  %-24s from=%s%s\n",
        t.piste_id, cmd, pub, side);
}

static void onError(const OPP2::Topic& t, OPP2::DispatchResult result,
                    OPP2::DeserializeError err) {
    fprintf(stderr, "[%s] ERROR    dispatch=%d  deserialize=%d\n",
        t.piste_id[0] ? t.piste_id : "?",
        static_cast<int>(result),
        static_cast<int>(err));
}

// ── Mosquitto callbacks ───────────────────────────────────────────────────────

static void onConnect(struct mosquitto* m, void*, int rc) {
    if (rc == 0) {
        printf("Connected to broker.\n");
        printf("Subscribing to openpiste/#...\n\n");
        mosquitto_subscribe(m, nullptr, "openpiste/#", 1);
    } else {
        fprintf(stderr, "Connect failed: %s\n", mosquitto_connack_string(rc));
    }
}

static void onMessage(struct mosquitto*, void*,
                      const struct mosquitto_message* msg) {
    if (!msg->payload || msg->payloadlen == 0) return;
    dispatcher.dispatch(msg->topic,
                        static_cast<const char*>(msg->payload),
                        static_cast<size_t>(msg->payloadlen));
}

static void onDisconnect(struct mosquitto*, void*, int rc) {
    if (rc != 0)
        fprintf(stderr, "Unexpected disconnect (rc=%d)\n", rc);
}

// ── Main ──────────────────────────────────────────────────────────────────────

int main() {
    signal(SIGINT,  onSignal);
    signal(SIGTERM, onSignal);

    printf("=== OPP2 Subscriber Basic Example (Linux) ===\n");
    printf("Broker: %s:%d\n\n", BROKER_HOST, BROKER_PORT);

    // Register all callbacks
    dispatcher.onLights          = onLights;
    dispatcher.onClock           = onClock;
    dispatcher.onBladeContact    = onBladeContact;
    dispatcher.onScore           = onScore;
    dispatcher.onConnection      = onConnection;
    dispatcher.onApparatusState  = onApparatusState;
    dispatcher.onFencers         = onFencers;
    dispatcher.onMatch           = onMatch;
    dispatcher.onUW2F            = onUW2F;
    dispatcher.onMedical         = onMedical;
    dispatcher.onVideoReview     = onVideoReview;
    dispatcher.onControl         = onControl;
    dispatcher.onError           = onError;

    // Attach SystemState — updated automatically on every retained message
    dispatcher.setSystemState(&pisteState);

    mosquitto_lib_init();

    struct mosquitto* mosq = mosquitto_new(nullptr, true, nullptr);
    if (!mosq) {
        fprintf(stderr, "Failed to create mosquitto instance\n");
        return 1;
    }

    mosquitto_connect_callback_set(mosq,    onConnect);
    mosquitto_message_callback_set(mosq,    onMessage);
    mosquitto_disconnect_callback_set(mosq, onDisconnect);

    int rc = mosquitto_connect(mosq, BROKER_HOST, BROKER_PORT, KEEPALIVE);
    if (rc != MOSQ_ERR_SUCCESS) {
        fprintf(stderr, "Could not connect to %s:%d — %s\n",
                BROKER_HOST, BROKER_PORT, mosquitto_strerror(rc));
        mosquitto_destroy(mosq);
        mosquitto_lib_cleanup();
        return 1;
    }

    mosquitto_loop_start(mosq);  // background thread for MQTT I/O

    printf("Listening for OPP2 messages. Press Ctrl+C to stop.\n\n");

    while (running) {
        sleep(1);
    }

    printf("\nShutting down...\n");
    mosquitto_loop_stop(mosq, true);
    mosquitto_destroy(mosq);
    mosquitto_lib_cleanup();

    return 0;
}
