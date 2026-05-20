/**
 * test_serialize.cpp — Doctest unit tests for opp2_serialize.h
 *
 * Run with: pio test -e native -f test_serialize
 */

#include <doctest/doctest.h>
#include "opp2_types.h"
#include "opp2_topic.h"
#include "opp2_time.h"
#include "opp2_serialize.h"
#include <string.h>

using namespace OPP2;

// ── Helpers ──────────────────────────────────────────────────────────────────

static bool contains(const char* json, const char* fragment) {
    return strstr(json, fragment) != nullptr;
}

// ── lights ────────────────────────────────────────────────────────────────────

TEST_CASE("serialize lights: wire fields") {
    Lights msg;
    msg.seq             = 42;
    msg.ts              = Timestamp::fromEpochMs(1715539200123ULL);
    msg.right.on_target = false;
    msg.right.white     = true;
    msg.left.on_target  = true;
    msg.left.white      = false;

    char buf[256];
    REQUIRE(Serializer::serialize(msg, buf, sizeof(buf)) == SerializeError::OK);
    CHECK(contains(buf, "\"protocol\":\"OPP2\""));
    CHECK(contains(buf, "\"version\":\"1.0\""));
    CHECK(contains(buf, "\"seq\":42"));
    CHECK(contains(buf, "\"green\":false"));
    CHECK(contains(buf, "\"white\":true"));
    CHECK(contains(buf, "\"red\":true"));
}

TEST_CASE("serialize lights: no seq on QoS0 messages") {
    Clock msg;
    msg.ts = Timestamp::fromEpochMs(0);
    msg.time_ms = 60000;
    char buf[256];
    REQUIRE(Serializer::serialize(msg, buf, sizeof(buf)) == SerializeError::OK);
    CHECK(!contains(buf, "\"seq\""));
}

TEST_CASE("serialize lights: buffer too small") {
    Lights msg;
    msg.seq = 1;
    msg.ts  = Timestamp::fromEpochMs(0);
    char tiny[4];
    CHECK(Serializer::serialize(msg, tiny, sizeof(tiny)) == SerializeError::BUFFER_SMALL);
}

TEST_CASE("serialize lights: null buffer") {
    Lights msg;
    CHECK(Serializer::serialize(msg, nullptr, 256) == SerializeError::INVALID_ARG);
}

// ── clock ─────────────────────────────────────────────────────────────────────

TEST_CASE("serialize clock: time string generated from time_ms") {
    Clock msg;
    msg.ts      = Timestamp::fromEpochMs(0);
    msg.running = true;
    msg.time_ms = 89250;

    char buf[256];
    REQUIRE(Serializer::serialize(msg, buf, sizeof(buf)) == SerializeError::OK);
    CHECK(contains(buf, "\"time_ms\":89250"));
    CHECK(contains(buf, "\"time\":\"1:29\""));
    CHECK(!contains(buf, "\"seq\""));
}

TEST_CASE("serialize clock: hundredths below 10s") {
    Clock msg;
    msg.ts = Timestamp::fromEpochMs(0);
    msg.time_ms = 9250;
    char buf[256];
    REQUIRE(Serializer::serialize(msg, buf, sizeof(buf)) == SerializeError::OK);
    CHECK(contains(buf, "\"time\":\"0:09.25\""));
}

// ── connection ────────────────────────────────────────────────────────────────

TEST_CASE("serialize connection: LWT form is minimal") {
    Connection msg;
    msg.online = false;
    char buf[256];
    REQUIRE(Serializer::serialize(msg, buf, sizeof(buf)) == SerializeError::OK);
    CHECK(contains(buf, "\"online\":false"));
    CHECK(!contains(buf, "\"protocol\""));
    CHECK(!contains(buf, "\"seq\""));
    CHECK(!contains(buf, "\"device\""));
}

TEST_CASE("serialize connection: online form has all fields") {
    Connection msg;
    msg.seq = 1; msg.online = true;
    msg.device_present = true;     strcpy(msg.device,     "OpenPiste-ESP32");
    msg.fw_version_present = true; strcpy(msg.fw_version, "1.0.0");
    char buf[256];
    REQUIRE(Serializer::serialize(msg, buf, sizeof(buf)) == SerializeError::OK);
    CHECK(contains(buf, "\"online\":true"));
    CHECK(contains(buf, "\"device\":\"OpenPiste-ESP32\""));
    CHECK(contains(buf, "\"fw_version\":\"1.0.0\""));
}

TEST_CASE("serialize connection: optional fields omitted when absent") {
    Connection msg;
    msg.seq = 1; msg.online = true;
    // device and fw_version not set
    char buf[256];
    REQUIRE(Serializer::serialize(msg, buf, sizeof(buf)) == SerializeError::OK);
    CHECK(!contains(buf, "\"device\""));
    CHECK(!contains(buf, "\"fw_version\""));
}

// ── score ─────────────────────────────────────────────────────────────────────

TEST_CASE("serialize score: all status values round-trip to wire strings") {
    const FencerStatus statuses[] = {
        FencerStatus::UNDEFINED, FencerStatus::VICTORY, FencerStatus::DEFEAT,
        FencerStatus::ABANDONMENT, FencerStatus::EXCLUSION, FencerStatus::DNS
    };
    const char* wire[] = { "\"U\"","\"V\"","\"D\"","\"A\"","\"E\"","\"DNS\"" };

    for (size_t i = 0; i < 6; i++) {
        Score msg; msg.seq = 1;
        msg.right.status = statuses[i];
        msg.left.status  = FencerStatus::UNDEFINED;
        msg.priority     = Priority::NONE;
        char buf[512];
        REQUIRE(Serializer::serialize(msg, buf, sizeof(buf)) == SerializeError::OK);
        CHECK(contains(buf, wire[i]));
    }
}

TEST_CASE("serialize score: UNKNOWN status returns INVALID_ARG") {
    Score msg; msg.seq = 1;
    msg.right.status = FencerStatus::UNKNOWN;
    msg.left.status  = FencerStatus::UNDEFINED;
    msg.priority     = Priority::NONE;
    char buf[512];
    CHECK(Serializer::serialize(msg, buf, sizeof(buf)) == SerializeError::INVALID_ARG);
}

TEST_CASE("serialize score: UNKNOWN priority returns INVALID_ARG") {
    Score msg; msg.seq = 1;
    msg.right.status = FencerStatus::UNDEFINED;
    msg.left.status  = FencerStatus::UNDEFINED;
    msg.priority     = Priority::UNKNOWN;
    char buf[512];
    CHECK(Serializer::serialize(msg, buf, sizeof(buf)) == SerializeError::INVALID_ARG);
}

// ── match ─────────────────────────────────────────────────────────────────────

TEST_CASE("serialize match: scheduled omitted when not present") {
    Match msg; msg.seq = 1;
    msg.weapon = Weapon::EPEE; msg.type = MatchType::INDIVIDUAL;
    strcpy(msg.competition, "test");
    msg.phase_type = PhaseType::DE;
    strcpy(msg.phase, "1"); strcpy(msg.poule, "A");
    msg.match_num = 1; msg.round = 1;
    // scheduled_present = false (default)
    char buf[512];
    REQUIRE(Serializer::serialize(msg, buf, sizeof(buf)) == SerializeError::OK);
    CHECK(!contains(buf, "\"scheduled\""));
}

TEST_CASE("serialize match: scheduled included when present") {
    Match msg; msg.seq = 1;
    msg.weapon = Weapon::FOIL; msg.type = MatchType::TEAM;
    strcpy(msg.competition, "test");
    msg.phase_type = PhaseType::POOL;
    strcpy(msg.phase, "1"); strcpy(msg.poule, "A");
    msg.match_num = 1; msg.round = 1;
    strcpy(msg.scheduled, "14:30"); msg.scheduled_present = true;
    char buf[512];
    REQUIRE(Serializer::serialize(msg, buf, sizeof(buf)) == SerializeError::OK);
    CHECK(contains(buf, "\"scheduled\":\"14:30\""));
}

// ── medical ───────────────────────────────────────────────────────────────────

TEST_CASE("serialize medical: timer fields omitted when inactive") {
    Medical msg; msg.seq = 1;
    msg.active = false; msg.side = Side::RIGHT;
    char buf[256];
    REQUIRE(Serializer::serialize(msg, buf, sizeof(buf)) == SerializeError::OK);
    CHECK(!contains(buf, "\"duration_ms\""));
    CHECK(!contains(buf, "\"remaining_ms\""));
    CHECK(!contains(buf, "\"remaining\""));
}

TEST_CASE("serialize medical: timer fields present when active") {
    Medical msg; msg.seq = 1;
    msg.active = true; msg.side = Side::LEFT;
    msg.duration_ms = 300000; msg.remaining_ms = 247000;
    char buf[256];
    REQUIRE(Serializer::serialize(msg, buf, sizeof(buf)) == SerializeError::OK);
    CHECK(contains(buf, "\"duration_ms\":300000"));
    CHECK(contains(buf, "\"remaining_ms\":247000"));
    CHECK(contains(buf, "\"remaining\":\"4:07\""));
}

TEST_CASE("serialize medical: NONE side returns INVALID_ARG") {
    Medical msg; msg.seq = 1; msg.active = false;
    msg.side = Side::NONE;
    char buf[256];
    CHECK(Serializer::serialize(msg, buf, sizeof(buf)) == SerializeError::INVALID_ARG);
}

// ── video_review ─────────────────────────────────────────────────────────────

TEST_CASE("serialize video_review: granted absent when PENDING") {
    VideoReview msg; msg.seq = 1;
    msg.left.remaining  = 1;
    msg.left.call_count = 1;
    msg.left.calls[0].id      = 1;
    msg.left.calls[0].round   = 1;
    msg.left.calls[0].time_ms = 89250;
    msg.left.calls[0].granted = CallResolution::PENDING;
    msg.right.remaining = 2;
    char buf[512];
    REQUIRE(Serializer::serialize(msg, buf, sizeof(buf)) == SerializeError::OK);
    CHECK(!contains(buf, "\"granted\""));
}

TEST_CASE("serialize video_review: granted:false when DENIED") {
    VideoReview msg; msg.seq = 1;
    msg.left.remaining  = 1;
    msg.left.call_count = 1;
    msg.left.calls[0].granted = CallResolution::DENIED;
    msg.right.remaining = 2;
    char buf[512];
    REQUIRE(Serializer::serialize(msg, buf, sizeof(buf)) == SerializeError::OK);
    CHECK(contains(buf, "\"granted\":false"));
}

// ── control ───────────────────────────────────────────────────────────────────

TEST_CASE("serialize control: optional fields absent when not set") {
    Control msg; msg.seq = 1;
    msg.ts      = Timestamp::fromEpochMs(0);
    msg.command = Command::BEGIN;
    // side = NONE (default), duration_present = false (default)
    char buf[256];
    REQUIRE(Serializer::serialize(msg, buf, sizeof(buf)) == SerializeError::OK);
    CHECK(contains(buf, "\"command\":\"BEGIN\""));
    CHECK(!contains(buf, "\"side\""));
    CHECK(!contains(buf, "\"duration\""));
}

TEST_CASE("serialize control: UNKNOWN command returns INVALID_ARG") {
    Control msg; msg.seq = 1;
    msg.ts = Timestamp::fromEpochMs(0);
    msg.command = Command::UNKNOWN;
    char buf[256];
    CHECK(Serializer::serialize(msg, buf, sizeof(buf)) == SerializeError::INVALID_ARG);
}
