/**
 * test_dispatcher.cpp — Doctest unit tests for opp2_dispatcher.h
 *
 * Run with: pio test -e native -f test_dispatcher
 */

#include <doctest/doctest.h>
#include "opp2_types.h"
#include "opp2_topic.h"
#include "opp2_time.h"
#include "opp2_serialize.h"
#include "opp2_deserialize.h"
#include "opp2_dispatcher.h"
#include <string.h>
#include <functional>

using namespace OPP2;

// ── Helpers ──────────────────────────────────────────────────────────────────

template<typename T>
static bool toJson(const T& msg, char* buf, size_t buf_size) {
    return Serializer::serialize(msg, buf, buf_size) == SerializeError::OK;
}

static bool makeTopic(const char* piste, Publisher pub, MessageType mt,
                      char* buf, size_t buf_size) {
    return TopicParser::buildFrom(piste, pub, mt, buf, buf_size);
}

// Convenience: build a minimal valid message, serialize it, dispatch it.
// Returns the DispatchResult.
template<typename T>
static DispatchResult fireMsg(Dispatcher& d, const char* piste,
                               Publisher pub, MessageType mt, T& msg) {
    char payload[1024], topic[64];
    if (!toJson(msg, payload, sizeof(payload)))      return DispatchResult::UNKNOWN_TYPE;
    if (!makeTopic(piste, pub, mt, topic, sizeof(topic))) return DispatchResult::UNKNOWN_TOPIC;
    return d.dispatch(topic, payload);
}

// ── Basic routing ─────────────────────────────────────────────────────────────

TEST_CASE("dispatch: lights callback receives correct data") {
    Lights msg; msg.seq = 1; msg.ts = Timestamp::fromEpochMs(0);
    msg.left.on_target = true;

    Dispatcher d; int called = 0;
    d.onLights = [&](const Topic& t, const Lights& m) {
        called++;
        CHECK(strcmp(t.piste_id, "17") == 0);
        CHECK(t.publisher == Publisher::APPARATUS);
        CHECK(m.left.on_target == true);
    };

    CHECK(fireMsg(d, "17", Publisher::APPARATUS, MessageType::LIGHTS, msg)
          == DispatchResult::OK);
    CHECK(called == 1);
}

TEST_CASE("dispatch: no callback returns NO_CALLBACK") {
    Clock msg; msg.ts = Timestamp::fromEpochMs(0); msg.time_ms = 0;
    Dispatcher d; // no onClock registered
    CHECK(fireMsg(d, "1", Publisher::APPARATUS, MessageType::CLOCK, msg)
          == DispatchResult::NO_CALLBACK);
}

TEST_CASE("dispatch: onAny fires before type callback") {
    Score msg; msg.seq = 1;
    msg.right.status = FencerStatus::UNDEFINED;
    msg.left.status  = FencerStatus::UNDEFINED;
    msg.priority     = Priority::NONE;

    Dispatcher d;
    int any_order = 0, score_order = 0, counter = 0;
    d.onAny   = [&](const Topic&, MessageType) { any_order   = ++counter; };
    d.onScore = [&](const Topic&, const Score&){ score_order = ++counter; };

    fireMsg(d, "1", Publisher::APPARATUS, MessageType::SCORE, msg);
    CHECK(any_order   == 1);
    CHECK(score_order == 2);
}

TEST_CASE("dispatch: all 12 message types routed") {
    Dispatcher d;
    int counts[12] = {};

    d.onLights         = [&](const Topic&, const Lights&)         { counts[0]++; };
    d.onClock          = [&](const Topic&, const Clock&)          { counts[1]++; };
    d.onBladeContact   = [&](const Topic&, const BladeContact&)   { counts[2]++; };
    d.onScore          = [&](const Topic&, const Score&)          { counts[3]++; };
    d.onConnection     = [&](const Topic&, const Connection&)     { counts[4]++; };
    d.onApparatusState = [&](const Topic&, const ApparatusStateMsg&) { counts[5]++; };
    d.onFencers        = [&](const Topic&, const Fencers&)        { counts[6]++; };
    d.onMatch          = [&](const Topic&, const Match&)          { counts[7]++; };
    d.onUW2F           = [&](const Topic&, const UW2F&)           { counts[8]++; };
    d.onMedical        = [&](const Topic&, const Medical&)        { counts[9]++; };
    d.onVideoReview    = [&](const Topic&, const VideoReview&)    { counts[10]++; };
    d.onControl        = [&](const Topic&, const Control&)        { counts[11]++; };

    char payload[1024], topic[64];

    auto fire = [&](Publisher pub, MessageType mt) {
        makeTopic("1", pub, mt, topic, sizeof(topic));
        d.dispatch(topic, payload);
    };

    { Lights m; m.seq=1; m.ts=Timestamp::fromEpochMs(0);
      toJson(m, payload, sizeof(payload)); fire(Publisher::APPARATUS, MessageType::LIGHTS); }

    { Clock m; m.ts=Timestamp::fromEpochMs(0); m.time_ms=0;
      toJson(m, payload, sizeof(payload)); fire(Publisher::APPARATUS, MessageType::CLOCK); }

    { BladeContact m; m.ts=Timestamp::fromMillis(0); m.active=false;
      toJson(m, payload, sizeof(payload)); fire(Publisher::APPARATUS, MessageType::BLADE_CONTACT); }

    { Score m; m.seq=1;
      m.right.status=FencerStatus::UNDEFINED; m.left.status=FencerStatus::UNDEFINED;
      m.priority=Priority::NONE;
      toJson(m, payload, sizeof(payload)); fire(Publisher::APPARATUS, MessageType::SCORE); }

    { Connection m; m.seq=1; m.online=true;
      toJson(m, payload, sizeof(payload)); fire(Publisher::APPARATUS, MessageType::CONNECTION); }

    { ApparatusStateMsg m; m.seq=1; m.state=ApparatusState::WAITING;
      toJson(m, payload, sizeof(payload)); fire(Publisher::APPARATUS, MessageType::APPARATUS_STATE); }

    { Fencers m; m.seq=1;
      m.left.fencer.present=true; strcpy(m.left.fencer.id,"1");
      strcpy(m.left.fencer.name,"A"); strcpy(m.left.fencer.nation,"FRA");
      m.right.fencer.present=true; strcpy(m.right.fencer.id,"2");
      strcpy(m.right.fencer.name,"B"); strcpy(m.right.fencer.nation,"ITA");
      toJson(m, payload, sizeof(payload)); fire(Publisher::SOFTWARE, MessageType::FENCERS); }

    { Match m; m.seq=1; m.weapon=Weapon::EPEE; m.type=MatchType::INDIVIDUAL;
      strcpy(m.competition,"c"); m.phase_type=PhaseType::DE;
      strcpy(m.phase,"1"); strcpy(m.poule,"A"); m.match_num=1; m.round=1;
      toJson(m, payload, sizeof(payload)); fire(Publisher::SOFTWARE, MessageType::MATCH); }

    { UW2F m; m.seq=1; m.time_ms=0;
      toJson(m, payload, sizeof(payload)); fire(Publisher::APPARATUS, MessageType::UW2F); }

    { Medical m; m.seq=1; m.active=false; m.side=Side::LEFT;
      toJson(m, payload, sizeof(payload)); fire(Publisher::APPARATUS, MessageType::MEDICAL); }

    { VideoReview m; m.seq=1; m.left.remaining=2; m.right.remaining=2;
      toJson(m, payload, sizeof(payload)); fire(Publisher::APPARATUS, MessageType::VIDEO_REVIEW); }

    { Control m; m.seq=1; m.ts=Timestamp::fromEpochMs(0); m.command=Command::BEGIN;
      toJson(m, payload, sizeof(payload)); fire(Publisher::REMOTE, MessageType::CONTROL); }

    for (int i = 0; i < 12; i++) CHECK(counts[i] == 1);
}

// ── SystemState mirror ────────────────────────────────────────────────────────

TEST_CASE("dispatch: SystemState updated on retained messages") {
    SystemState state;
    Dispatcher d; d.setSystemState(&state);

    Lights msg; msg.seq = 7; msg.ts = Timestamp::fromEpochMs(0);
    msg.right.on_target = true;
    fireMsg(d, "1", Publisher::APPARATUS, MessageType::LIGHTS, msg);
    CHECK(state.lights.seq == 7);
    CHECK(state.lights.right.on_target == true);
}

TEST_CASE("dispatch: blade_contact not mirrored in SystemState") {
    SystemState state;
    Dispatcher d; d.setSystemState(&state);
    // Just verify dispatch succeeds — blade_contact has no SystemState field
    BladeContact msg; msg.ts = Timestamp::fromMillis(100); msg.active = true;
    auto r = fireMsg(d, "1", Publisher::APPARATUS, MessageType::BLADE_CONTACT, msg);
    CHECK(r == DispatchResult::NO_CALLBACK);
}

TEST_CASE("dispatch: state accumulates across multiple messages") {
    SystemState state;
    Dispatcher d; d.setSystemState(&state);

    Score score; score.seq = 1;
    score.right.status = FencerStatus::UNDEFINED;
    score.left.status  = FencerStatus::UNDEFINED;
    score.priority     = Priority::NONE;

    score.right.score = 1; score.seq = 1;
    fireMsg(d, "1", Publisher::APPARATUS, MessageType::SCORE, score);
    CHECK(state.score.right.score == 1);

    score.right.score = 2; score.seq = 2;
    fireMsg(d, "1", Publisher::APPARATUS, MessageType::SCORE, score);
    CHECK(state.score.right.score == 2);
    CHECK(state.score.seq == 2);
}

// ── Error handling ────────────────────────────────────────────────────────────

TEST_CASE("dispatch: unknown topic") {
    Dispatcher d;
    int err_count = 0;
    d.onError = [&](const Topic&, DispatchResult r, DeserializeError) {
        if (r == DispatchResult::UNKNOWN_TOPIC) err_count++;
    };
    CHECK(d.dispatch("not/opp2/at/all", "{}") == DispatchResult::UNKNOWN_TOPIC);
    CHECK(err_count == 1);
}

TEST_CASE("dispatch: unknown message type — forward compat, no error callback [dispatch][errors]") {
    Dispatcher d;
    int err_count = 0;
    d.onError = [&](const Topic&, DispatchResult, DeserializeError) { err_count++; };
    CHECK(d.dispatch("openpiste/1/apparatus/future_type", "{}")
          == DispatchResult::UNKNOWN_TYPE);
    CHECK(err_count == 0);
}

TEST_CASE("dispatch: bad JSON payload triggers error callback") {
    Dispatcher d;
    DeserializeError captured = DeserializeError::OK;
    d.onError = [&](const Topic&, DispatchResult, DeserializeError e) { captured = e; };

    char topic[64];
    makeTopic("1", Publisher::APPARATUS, MessageType::LIGHTS, topic, sizeof(topic));
    CHECK(d.dispatch(topic, "{ bad json }") == DispatchResult::DESERIALIZE_ERROR);
    CHECK(captured == DeserializeError::INVALID_JSON);
}

TEST_CASE("dispatch: lastDeserializeError accessible after error") {
    Dispatcher d;
    char topic[64];
    makeTopic("1", Publisher::APPARATUS, MessageType::LIGHTS, topic, sizeof(topic));
    d.dispatch(topic, "not json");
    CHECK(d.lastDeserializeError() == DeserializeError::INVALID_JSON);
    CHECK(d.lastDispatchResult()   == DispatchResult::DESERIALIZE_ERROR);
}

// ── LWT ──────────────────────────────────────────────────────────────────────

TEST_CASE("dispatch: LWT payload dispatches as Connection") {
    Dispatcher d;
    bool online_seen = true;
    d.onConnection = [&](const Topic&, const Connection& msg) {
        online_seen = msg.online;
    };

    char topic[64];
    makeTopic("17", Publisher::APPARATUS, MessageType::CONNECTION, topic, sizeof(topic));
    CHECK(d.dispatch(topic, "{\"online\":false}") == DispatchResult::OK);
    CHECK(online_seen == false);
}

// ── Piste ID threading ────────────────────────────────────────────────────────

TEST_CASE("dispatch: piste_id threaded correctly to callback") {
    Clock msg; msg.ts = Timestamp::fromEpochMs(0); msg.time_ms = 30000;
    Dispatcher d;
    char last_piste[32] = {};
    d.onClock = [&](const Topic& t, const Clock&) {
        strncpy(last_piste, t.piste_id, sizeof(last_piste)-1);
    };

    char payload[256];
    toJson(msg, payload, sizeof(payload));

    char topic[64];
    makeTopic("rouge", Publisher::APPARATUS, MessageType::CLOCK, topic, sizeof(topic));
    d.dispatch(topic, payload);
    CHECK(strcmp(last_piste, "rouge") == 0);

    makeTopic("vert", Publisher::APPARATUS, MessageType::CLOCK, topic, sizeof(topic));
    d.dispatch(topic, payload);
    CHECK(strcmp(last_piste, "vert") == 0);
}
