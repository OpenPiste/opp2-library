/**
 * test_deserialize.cpp — Doctest unit tests for opp2_deserialize.h
 *
 * Run with: pio test -e native -f test_deserialize
 */

#include <doctest/doctest.h>
#include "opp2_types.h"
#include "opp2_topic.h"
#include "opp2_time.h"
#include "opp2_serialize.h"
#include "opp2_deserialize.h"
#include <string.h>

using namespace OPP2;

// ── Helpers ──────────────────────────────────────────────────────────────────

template<typename T>
static DeserializeError roundtrip(const T& in, T& out) {
    char buf[1024];
    auto err = Serializer::serialize(in, buf, sizeof(buf));
    if (err != SerializeError::OK) return DeserializeError::INVALID_JSON;
    return Deserializer::deserialize(buf, strlen(buf), out);
}

// ── Common error paths ────────────────────────────────────────────────────────

TEST_CASE("deserialize: null buffer") {
    Lights out;
    CHECK(Deserializer::deserialize(nullptr, 0, out) == DeserializeError::BUFFER_NULL);
}

TEST_CASE("deserialize: invalid JSON") {
    Lights out;
    CHECK(Deserializer::deserialize("not json", 8, out) == DeserializeError::INVALID_JSON);
}

TEST_CASE("deserialize: wrong protocol") {
    const char* bad =
        "{\"protocol\":\"WRONG\",\"version\":\"1.0\",\"seq\":1,"
        "\"ts\":0,\"right\":{\"green\":false,\"white\":false},"
        "\"left\":{\"red\":false,\"white\":false}}";
    Lights out;
    CHECK(Deserializer::deserialize(bad, strlen(bad), out) == DeserializeError::WRONG_PROTOCOL);
}

// ── lights ────────────────────────────────────────────────────────────────────

TEST_CASE("deserialize lights: round-trip") {
    Lights in;
    in.seq = 42; in.ts = Timestamp::fromEpochMs(1715539200123ULL);
    in.right.on_target = true; in.right.white = false;
    in.left.on_target  = false; in.left.white  = true;

    Lights out;
    REQUIRE(roundtrip(in, out) == DeserializeError::OK);
    CHECK(out.seq == 42);
    CHECK(out.right.on_target == true);
    CHECK(out.right.white     == false);
    CHECK(out.left.on_target  == false);
    CHECK(out.left.white      == true);
    CHECK(out.ts.source == ClockSource::NTP);
    CHECK(out.ts.value  == 1715539200123ULL);
    CHECK(strcmp(out.protocol, "OPP2") == 0);
    CHECK(strcmp(out.version,  "1.0")  == 0);
}

TEST_CASE("deserialize lights: missing seq returns MISSING_FIELD") {
    const char* no_seq =
        "{\"protocol\":\"OPP2\",\"version\":\"1.0\","
        "\"ts\":0,\"right\":{\"green\":false,\"white\":false},"
        "\"left\":{\"red\":false,\"white\":false}}";
    Lights out;
    CHECK(Deserializer::deserialize(no_seq, strlen(no_seq), out)
          == DeserializeError::MISSING_FIELD);
}

// ── clock ─────────────────────────────────────────────────────────────────────

TEST_CASE("deserialize clock: round-trip") {
    Clock in;
    in.ts = Timestamp::fromEpochMs(0); in.running = true; in.time_ms = 89250;

    Clock out;
    REQUIRE(roundtrip(in, out) == DeserializeError::OK);
    CHECK(out.running == true);
    CHECK(out.time_ms == 89250);
    CHECK(strcmp(out.time, "1:29") == 0);
}

TEST_CASE("deserialize clock: no seq field (QoS 0)") {
    // A valid clock payload has no seq — deserializer must not require it
    const char* payload =
        "{\"protocol\":\"OPP2\",\"version\":\"1.0\","
        "\"ts\":0,\"running\":false,\"time_ms\":60000,\"time\":\"1:00\"}";
    Clock out;
    CHECK(Deserializer::deserialize(payload, strlen(payload), out)
          == DeserializeError::OK);
    CHECK(out.time_ms == 60000);
}

// ── connection ────────────────────────────────────────────────────────────────

TEST_CASE("deserialize connection: LWT payload") {
    const char* lwt = "{\"online\":false}";
    Connection out;
    REQUIRE(Deserializer::deserialize(lwt, strlen(lwt), out) == DeserializeError::OK);
    CHECK(out.online == false);
    CHECK(out.device_present     == false);
    CHECK(out.fw_version_present == false);
}

TEST_CASE("deserialize connection: online with optional fields") {
    Connection in;
    in.seq = 1; in.online = true;
    in.device_present = true;     strcpy(in.device,     "OpenPiste-ESP32");
    in.fw_version_present = true; strcpy(in.fw_version, "1.0.0");

    Connection out;
    REQUIRE(roundtrip(in, out) == DeserializeError::OK);
    CHECK(out.online == true);
    CHECK(out.device_present == true);
    CHECK(strcmp(out.device,     "OpenPiste-ESP32") == 0);
    CHECK(strcmp(out.fw_version, "1.0.0")           == 0);
}

TEST_CASE("deserialize connection: optional fields absent when not serialized") {
    Connection in;
    in.seq = 1; in.online = true;
    // device and fw_version not set

    Connection out;
    REQUIRE(roundtrip(in, out) == DeserializeError::OK);
    CHECK(out.device_present     == false);
    CHECK(out.fw_version_present == false);
}

// ── fencers ───────────────────────────────────────────────────────────────────

TEST_CASE("deserialize fencers: optional coach absent") {
    Fencers in; in.seq = 1;
    in.left.fencer.present = true;
    strcpy(in.left.fencer.id, "1"); strcpy(in.left.fencer.name, "A");
    strcpy(in.left.fencer.nation, "FRA");
    in.right.fencer.present = true;
    strcpy(in.right.fencer.id, "2"); strcpy(in.right.fencer.name, "B");
    strcpy(in.right.fencer.nation, "ITA");
    // coaches absent, referee absent, video_official absent

    Fencers out;
    REQUIRE(roundtrip(in, out) == DeserializeError::OK);
    CHECK(out.left.coach.present       == false);
    CHECK(out.right.coach.present      == false);
    CHECK(out.referee.present          == false);
    CHECK(out.video_official.present   == false);
}

TEST_CASE("deserialize fencers: coach present when serialized") {
    Fencers in; in.seq = 1;
    in.left.fencer.present = true;
    strcpy(in.left.fencer.id, "1"); strcpy(in.left.fencer.name, "A");
    strcpy(in.left.fencer.nation, "FRA");
    in.left.coach.present = true;
    strcpy(in.left.coach.name, "CoachA"); strcpy(in.left.coach.nation, "FRA");
    in.right.fencer.present = true;
    strcpy(in.right.fencer.id, "2"); strcpy(in.right.fencer.name, "B");
    strcpy(in.right.fencer.nation, "ITA");

    Fencers out;
    REQUIRE(roundtrip(in, out) == DeserializeError::OK);
    CHECK(out.left.coach.present == true);
    CHECK(strcmp(out.left.coach.name, "CoachA") == 0);
}

// ── uw2f ─────────────────────────────────────────────────────────────────────

TEST_CASE("deserialize uw2f: time_ms fallback from time string") {
    // Payload has only 'time', no 'time_ms' — spec allows this
    const char* str_only =
        "{\"protocol\":\"OPP2\",\"version\":\"1.0\",\"seq\":1,"
        "\"time\":\"1:00\","
        "\"right\":{\"p_card\":0},\"left\":{\"p_card\":0}}";
    UW2F out;
    REQUIRE(Deserializer::deserialize(str_only, strlen(str_only), out)
            == DeserializeError::OK);
    CHECK(out.time_ms == 60000);
    CHECK(strcmp(out.time, "1:00") == 0);
}

TEST_CASE("deserialize uw2f: missing both time fields returns MISSING_FIELD") {
    const char* no_time =
        "{\"protocol\":\"OPP2\",\"version\":\"1.0\",\"seq\":1,"
        "\"right\":{\"p_card\":0},\"left\":{\"p_card\":0}}";
    UW2F out;
    CHECK(Deserializer::deserialize(no_time, strlen(no_time), out)
          == DeserializeError::MISSING_FIELD);
}

// ── medical ───────────────────────────────────────────────────────────────────

TEST_CASE("deserialize medical: active round-trip") {
    Medical in; in.seq = 1;
    in.active = true; in.side = Side::LEFT;
    in.duration_ms = 300000; in.remaining_ms = 247000;

    Medical out;
    REQUIRE(roundtrip(in, out) == DeserializeError::OK);
    CHECK(out.active       == true);
    CHECK(out.side         == Side::LEFT);
    CHECK(out.duration_ms  == 300000);
    CHECK(out.remaining_ms == 247000);
    CHECK(strcmp(out.remaining, "4:07") == 0);
}

TEST_CASE("deserialize medical: inactive — timer fields zero") {
    Medical in; in.seq = 1; in.active = false; in.side = Side::RIGHT;

    Medical out;
    REQUIRE(roundtrip(in, out) == DeserializeError::OK);
    CHECK(out.active      == false);
    CHECK(out.side        == Side::RIGHT);
    CHECK(out.remaining_ms == 0);
}

// ── video_review ─────────────────────────────────────────────────────────────

TEST_CASE("deserialize video_review: granted absent means PENDING") {
    VideoReview in; in.seq = 1;
    in.left.remaining  = 1; in.left.call_count = 1;
    in.left.calls[0].id = 1; in.left.calls[0].round = 1;
    in.left.calls[0].time_ms = 0;
    in.left.calls[0].granted = CallResolution::PENDING;
    in.right.remaining = 2;

    VideoReview out;
    REQUIRE(roundtrip(in, out) == DeserializeError::OK);
    CHECK(out.left.calls[0].granted == CallResolution::PENDING);
}

TEST_CASE("deserialize video_review: full call history") {
    VideoReview in; in.seq = 1;
    in.left.remaining  = 0; in.left.call_count = 2;
    in.left.calls[0].id=1; in.left.calls[0].round=1; in.left.calls[0].time_ms=89250; in.left.calls[0].granted=CallResolution::DENIED;
    in.left.calls[1].id=2; in.left.calls[1].round=2; in.left.calls[1].time_ms=45000; in.left.calls[1].granted=CallResolution::GRANTED;
    in.right.remaining = 2;

    VideoReview out;
    REQUIRE(roundtrip(in, out) == DeserializeError::OK);
    CHECK(out.left.call_count == 2);
    CHECK(out.left.calls[0].granted == CallResolution::DENIED);
    CHECK(out.left.calls[1].granted == CallResolution::GRANTED);
    CHECK(out.left.calls[1].time_ms == 45000);
}

// ── forward compatibility ─────────────────────────────────────────────────────

TEST_CASE("deserialize: unknown apparatus state → UNKNOWN enum, no error [deserialize][forward_compat]") {
    const char* future =
        "{\"protocol\":\"OPP2\",\"version\":\"1.1\",\"seq\":1,\"state\":\"Z\"}";
    ApparatusStateMsg out;
    CHECK(Deserializer::deserialize(future, strlen(future), out)
          == DeserializeError::OK);
    CHECK(out.state == ApparatusState::UNKNOWN);
}

TEST_CASE("deserialize: unknown command → UNKNOWN enum, no error [deserialize][forward_compat]") {
    const char* future =
        "{\"protocol\":\"OPP2\",\"version\":\"1.1\",\"seq\":1,"
        "\"ts\":0,\"command\":\"FUTURE_CMD\"}";
    Control out;
    CHECK(Deserializer::deserialize(future, strlen(future), out)
          == DeserializeError::OK);
    CHECK(out.command == Command::UNKNOWN);
}

TEST_CASE("deserialize: unknown weapon → UNKNOWN enum, no error [deserialize][forward_compat]") {
    const char* future =
        "{\"protocol\":\"OPP2\",\"version\":\"1.1\",\"seq\":1,"
        "\"weapon\":\"X\",\"type\":\"I\",\"competition\":\"test\","
        "\"phase_type\":\"DE\",\"phase\":\"1\",\"poule\":\"A\","
        "\"match\":1,\"round\":1}";
    Match out;
    CHECK(Deserializer::deserialize(future, strlen(future), out)
          == DeserializeError::OK);
    CHECK(out.weapon == Weapon::UNKNOWN);
}

// ── timestamp ─────────────────────────────────────────────────────────────────

TEST_CASE("deserialize: NTP timestamp decoded correctly") {
    Lights in; in.seq = 1; in.ts = Timestamp::fromEpochMs(1715539200123ULL);
    Lights out;
    REQUIRE(roundtrip(in, out) == DeserializeError::OK);
    CHECK(out.ts.source == ClockSource::NTP);
    CHECK(out.ts.value  == 1715539200123ULL);
}

TEST_CASE("deserialize: SESSION timestamp decoded correctly") {
    BladeContact in;
    in.ts     = Timestamp::fromMillis(12345);
    in.active = false;
    BladeContact out;
    REQUIRE(roundtrip(in, out) == DeserializeError::OK);
    CHECK(out.ts.source == ClockSource::SESSION);
    CHECK(out.ts.value  == 12345ULL);
}
