/**
 * test_topic.cpp — Doctest unit tests for opp2_topic.h
 *
 * Run with: pio test -e native -f test_topic
 */

#include <doctest/doctest.h>
#include "opp2_types.h"
#include "opp2_topic.h"
#include <string.h>

using namespace OPP2;

// ── TopicParser::parse ────────────────────────────────────────────────────────

TEST_CASE("parse: valid apparatus/lights") {
    Topic t;
    REQUIRE(TopicParser::parse("openpiste/17/apparatus/lights", t) == true);
    CHECK(strcmp(t.piste_id, "17") == 0);
    CHECK(t.publisher    == Publisher::APPARATUS);
    CHECK(t.message_type == MessageType::LIGHTS);
}

TEST_CASE("parse: valid software/score") {
    Topic t;
    REQUIRE(TopicParser::parse("openpiste/podium/software/score", t) == true);
    CHECK(strcmp(t.piste_id, "podium") == 0);
    CHECK(t.publisher    == Publisher::SOFTWARE);
    CHECK(t.message_type == MessageType::SCORE);
}

TEST_CASE("parse: valid remote/control") {
    Topic t;
    REQUIRE(TopicParser::parse("openpiste/rouge/remote/control", t) == true);
    CHECK(t.publisher    == Publisher::REMOTE);
    CHECK(t.message_type == MessageType::CONTROL);
}

TEST_CASE("parse: state topic maps to APPARATUS_STATE") {
    Topic t;
    REQUIRE(TopicParser::parse("openpiste/1/apparatus/state", t) == true);
    CHECK(t.message_type == MessageType::APPARATUS_STATE);
}

TEST_CASE("parse: all message types") {
    const char* types[] = {
        "lights","clock","blade_contact","score","connection",
        "state","fencers","match","uw2f","medical","video_review","control"
    };
    const MessageType expected[] = {
        MessageType::LIGHTS, MessageType::CLOCK, MessageType::BLADE_CONTACT,
        MessageType::SCORE,  MessageType::CONNECTION, MessageType::APPARATUS_STATE,
        MessageType::FENCERS,MessageType::MATCH, MessageType::UW2F,
        MessageType::MEDICAL,MessageType::VIDEO_REVIEW, MessageType::CONTROL
    };
    char topic_str[64];
    for (size_t i = 0; i < 12; i++) {
        snprintf(topic_str, sizeof(topic_str), "openpiste/1/apparatus/%s", types[i]);
        Topic t;
        REQUIRE(TopicParser::parse(topic_str, t) == true);
        CHECK(t.message_type == expected[i]);
    }
}

TEST_CASE("parse: unknown publisher returns false but keeps piste_id") {
    Topic t;
    bool ok = TopicParser::parse("openpiste/17/future_publisher/lights", t);
    CHECK(ok == false);
    CHECK(strcmp(t.piste_id, "17") == 0);
    CHECK(t.publisher    == Publisher::UNKNOWN);
    CHECK(t.message_type == MessageType::LIGHTS);
}

TEST_CASE("parse: unknown message type returns false") {
    Topic t;
    bool ok = TopicParser::parse("openpiste/17/apparatus/future_message", t);
    CHECK(ok == false);
    CHECK(t.publisher    == Publisher::APPARATUS);
    CHECK(t.message_type == MessageType::UNKNOWN);
}

TEST_CASE("parse: wrong prefix returns false") {
    Topic t;
    CHECK(TopicParser::parse("wrongprefix/17/apparatus/lights", t) == false);
    CHECK(t.piste_id[0] == '\0');
}

TEST_CASE("parse: too few segments") {
    Topic t;
    CHECK(TopicParser::parse("openpiste/17/apparatus", t) == false);
    CHECK(TopicParser::parse("openpiste/17", t)           == false);
    CHECK(TopicParser::parse("openpiste", t)              == false);
    CHECK(TopicParser::parse("", t)                       == false);
}

TEST_CASE("parse: null pointer") {
    Topic t;
    CHECK(TopicParser::parse(nullptr, t) == false);
}

TEST_CASE("parse: topic too long") {
    // piste_id at or beyond PISTE_ID_MAX should fail
    char topic_str[256];
    // Generate a piste_id of exactly PISTE_ID_MAX characters (too long)
    memset(topic_str, 0, sizeof(topic_str));
    strcpy(topic_str, "openpiste/");
    for (size_t i = 0; i < PISTE_ID_MAX; i++) strcat(topic_str, "x");
    strcat(topic_str, "/apparatus/lights");
    Topic t;
    CHECK(TopicParser::parse(topic_str, t) == false);
}

// ── TopicParser::build ────────────────────────────────────────────────────────

TEST_CASE("build: round-trip parse→build") {
    const char* original = "openpiste/17/apparatus/lights";
    Topic t;
    REQUIRE(TopicParser::parse(original, t) == true);

    char buf[64];
    REQUIRE(TopicParser::build(t, buf, sizeof(buf)) == true);
    CHECK(strcmp(buf, original) == 0);
}

TEST_CASE("build: buildFrom components") {
    char buf[64];
    REQUIRE(TopicParser::buildFrom("podium", Publisher::SOFTWARE,
                                   MessageType::SCORE, buf, sizeof(buf)) == true);
    CHECK(strcmp(buf, "openpiste/podium/software/score") == 0);
}

TEST_CASE("build: state message topic uses wire name 'state'") {
    char buf[64];
    REQUIRE(TopicParser::buildFrom("1", Publisher::APPARATUS,
                                   MessageType::APPARATUS_STATE, buf, sizeof(buf)) == true);
    CHECK(strcmp(buf, "openpiste/1/apparatus/state") == 0);
}

TEST_CASE("build: buffer too small returns false") {
    char tiny[10];
    CHECK(TopicParser::buildFrom("17", Publisher::APPARATUS,
                                 MessageType::LIGHTS, tiny, sizeof(tiny)) == false);
}

TEST_CASE("build: UNKNOWN publisher returns false") {
    char buf[64];
    CHECK(TopicParser::buildFrom("17", Publisher::UNKNOWN,
                                 MessageType::LIGHTS, buf, sizeof(buf)) == false);
}

TEST_CASE("build: LWT topic helper") {
    char buf[64];
    REQUIRE(TopicParser::buildLwtTopic("17", buf, sizeof(buf)) == true);
    CHECK(strcmp(buf, "openpiste/17/apparatus/connection") == 0);
}
