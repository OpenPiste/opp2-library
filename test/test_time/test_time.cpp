/**
 * test_time.cpp — Doctest unit tests for opp2_time.h
 *
 * Run with: pio test -e native -f test_time
 */

#include <doctest/doctest.h>
#include "opp2_time.h"
#include <string.h>

using namespace OPP2;

// ── formatMs ─────────────────────────────────────────────────────────────────

TEST_CASE("formatMs: zero") {
    char buf[12];
    REQUIRE(TimeFormat::formatMs(0, buf, sizeof(buf)) == true);
    CHECK(strcmp(buf, "0:00.00") == 0);
}

TEST_CASE("formatMs: below 10 seconds — hundredths included") {
    char buf[12];
    REQUIRE(TimeFormat::formatMs(9250,  buf, sizeof(buf)) == true);
    CHECK(strcmp(buf, "0:09.25") == 0);

    REQUIRE(TimeFormat::formatMs(500,   buf, sizeof(buf)) == true);
    CHECK(strcmp(buf, "0:00.50") == 0);

    REQUIRE(TimeFormat::formatMs(9990,  buf, sizeof(buf)) == true);
    CHECK(strcmp(buf, "0:09.99") == 0);

    REQUIRE(TimeFormat::formatMs(1000,  buf, sizeof(buf)) == true);
    CHECK(strcmp(buf, "0:01.00") == 0);
}

TEST_CASE("formatMs: exactly 10 seconds — no hundredths") {
    char buf[12];
    REQUIRE(TimeFormat::formatMs(10000, buf, sizeof(buf)) == true);
    CHECK(strcmp(buf, "0:10") == 0);
}

TEST_CASE("formatMs: above 10 seconds — no hundredths") {
    char buf[12];
    REQUIRE(TimeFormat::formatMs(89250, buf, sizeof(buf)) == true);
    CHECK(strcmp(buf, "1:29") == 0);

    REQUIRE(TimeFormat::formatMs(60000, buf, sizeof(buf)) == true);
    CHECK(strcmp(buf, "1:00") == 0);

    REQUIRE(TimeFormat::formatMs(180000, buf, sizeof(buf)) == true);
    CHECK(strcmp(buf, "3:00") == 0);

    REQUIRE(TimeFormat::formatMs(599000, buf, sizeof(buf)) == true);
    CHECK(strcmp(buf, "9:59") == 0);
}

TEST_CASE("formatMs: sub-second precision") {
    char buf[12];
    // 250ms → 0:00.25
    REQUIRE(TimeFormat::formatMs(250, buf, sizeof(buf)) == true);
    CHECK(strcmp(buf, "0:00.25") == 0);

    // 10ms → 0:00.01
    REQUIRE(TimeFormat::formatMs(10, buf, sizeof(buf)) == true);
    CHECK(strcmp(buf, "0:00.01") == 0);
}

TEST_CASE("formatMs: null buffer returns false") {
    CHECK(TimeFormat::formatMs(1000, nullptr, 12) == false);
}

TEST_CASE("formatMs: buffer too small returns false") {
    char tiny[4];
    CHECK(TimeFormat::formatMs(1000, tiny, sizeof(tiny)) == false);
}

// ── parseToMs ────────────────────────────────────────────────────────────────

TEST_CASE("parseToMs: M:SS format") {
    uint32_t ms = 0;
    REQUIRE(TimeFormat::parseToMs("1:29", ms) == true);
    CHECK(ms == 89000);

    REQUIRE(TimeFormat::parseToMs("0:00", ms) == true);
    CHECK(ms == 0);

    REQUIRE(TimeFormat::parseToMs("3:00", ms) == true);
    CHECK(ms == 180000);

    REQUIRE(TimeFormat::parseToMs("9:59", ms) == true);
    CHECK(ms == 599000);
}

TEST_CASE("parseToMs: M:SS.cc format") {
    uint32_t ms = 0;
    REQUIRE(TimeFormat::parseToMs("0:09.25", ms) == true);
    CHECK(ms == 9250);

    REQUIRE(TimeFormat::parseToMs("0:00.50", ms) == true);
    CHECK(ms == 500);

    REQUIRE(TimeFormat::parseToMs("0:00.00", ms) == true);
    CHECK(ms == 0);

    REQUIRE(TimeFormat::parseToMs("0:01.00", ms) == true);
    CHECK(ms == 1000);
}

TEST_CASE("parseToMs: invalid inputs") {
    uint32_t ms = 0;
    CHECK(TimeFormat::parseToMs(nullptr,   ms) == false);
    CHECK(TimeFormat::parseToMs("",        ms) == false);
    CHECK(TimeFormat::parseToMs("129",     ms) == false);  // no colon
    CHECK(TimeFormat::parseToMs("1:60",    ms) == false);  // seconds > 59
    CHECK(TimeFormat::parseToMs("abc",     ms) == false);
}

// ── Round-trip ────────────────────────────────────────────────────────────────

TEST_CASE("round-trip: format then parse") {
    const uint32_t values[] = { 0, 250, 1000, 9990, 10000, 60000, 89000, 180000, 599000 };
    char buf[12];
    for (uint32_t v : values) {
        REQUIRE(TimeFormat::formatMs(v, buf, sizeof(buf)) == true);
        uint32_t recovered = 0;
        REQUIRE(TimeFormat::parseToMs(buf, recovered) == true);
        // Round-trip is lossless only when v is a multiple of 10ms (centisecond boundary).
        // All test values above are centisecond-aligned; any sub-10ms remainder is
        // lost in the formatted string (by spec design).
        uint32_t v_cs = (v / 10) * 10;  // truncate to centisecond boundary
        CHECK(recovered == v_cs);
    }
}

// ── populateTimeStr ───────────────────────────────────────────────────────────

TEST_CASE("populateTimeStr: writes to buffer") {
    char buf[12] = {};
    TimeFormat::populateTimeStr(89250, buf, sizeof(buf));
    CHECK(strcmp(buf, "1:29") == 0);
}

TEST_CASE("populateTimeStr: writes fallback on tiny buffer") {
    char buf[5] = {};
    TimeFormat::populateTimeStr(0, buf, sizeof(buf));
    // Should write "0:00" — 4 chars + null fits in 5 bytes
    CHECK(strcmp(buf, "0:00") == 0);
}
