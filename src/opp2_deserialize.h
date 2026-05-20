/**
 * @file opp2_deserialize.h
 * @brief OPP2 message deserializer — JSON string → struct.
 *
 * Converts incoming MQTT JSON payloads into OPP2 message structs.
 * Designed to be the mirror of opp2_serialize.h.
 *
 * Dependency: ArduinoJson v6 or v7 (same as opp2_serialize.h).
 *
 * Design principles:
 *
 *   Forward compatibility (Section 23):
 *     Unknown enum string values deserialize to their UNKNOWN variant rather
 *     than causing an error. This allows a receiver running an older firmware
 *     to accept messages from a sender running a newer version.
 *
 *   Optional fields (Section 20.2):
 *     Missing optional fields leave the struct field at its default value and
 *     set the companion _present flag to false. The deserializer never fails
 *     on a missing optional field.
 *
 *   Mandatory fields:
 *     Missing mandatory fields on QoS 1 messages return MISSING_FIELD.
 *     The caller may choose to accept or discard the message.
 *
 *   LWT payload (Section 4.6):
 *     {"online": false} with no other fields is accepted as a valid
 *     Connection message with online=false and all other fields at defaults.
 *
 *   Time string fallback (Sections 16, 17):
 *     UW2F and Medical require at least one of time_ms or time (formatted
 *     string). If time_ms is absent but time is present, the deserializer
 *     parses the string and populates time_ms. Both are always populated
 *     in the output struct regardless of which was present in the payload.
 *
 *   Version field:
 *     The version field is read and stored in the struct but not enforced
 *     by the deserializer. Version-specific behaviour is the caller's
 *     responsibility (Section 20.3).
 *
 * Error handling:
 *   All deserialize() functions return a DeserializeError.
 *   OK            — success.
 *   INVALID_JSON  — payload is not valid JSON or is empty.
 *   MISSING_FIELD — a mandatory field was absent.
 *   WRONG_PROTOCOL— protocol field is present but not "OPP2".
 *   BUFFER_NULL   — buf pointer was null.
 *
 * Thread safety:
 *   All functions are stateless and re-entrant.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "opp2_types.h"
#include "opp2_topic.h"
#include "opp2_time.h"
#include <ArduinoJson.h>
#include <string.h>

namespace OPP2 {

// ============================================================================
// Error type
// ============================================================================

enum class DeserializeError : uint8_t {
    OK,
    INVALID_JSON,    ///< Payload is not valid JSON or is empty
    MISSING_FIELD,   ///< A mandatory field was absent
    WRONG_PROTOCOL,  ///< 'protocol' field present but value is not "OPP2"
    BUFFER_NULL,     ///< buf pointer was null
};

// ============================================================================
// Internal helpers
// ============================================================================

namespace detail {

// --- Safe string copy -------------------------------------------------------

/// Copy a JsonVariant string value into a fixed char array.
/// If the variant is null/missing, dst is zeroed. Always null-terminates.
template<size_t N>
inline void copyStr(const char* src, char (&dst)[N]) {
    if (!src) { memset(dst, 0, N); return; }
    strncpy(dst, src, N - 1);
    dst[N - 1] = '\0';
}

// --- Common field parsing ---------------------------------------------------

/// Parse protocol + version from a JsonObject.
/// Returns WRONG_PROTOCOL if protocol is present but wrong.
/// Returns OK (not MISSING_FIELD) if protocol is absent — LWT compatibility.
inline DeserializeError parseCommon(JsonObjectConst obj,
                                    char* protocol_dst, size_t protocol_size,
                                    char* version_dst,  size_t version_size)
{
    const char* proto = obj["protocol"];
    if (proto) {
        if (strcmp(proto, PROTOCOL_ID) != 0) return DeserializeError::WRONG_PROTOCOL;
        strncpy(protocol_dst, proto, protocol_size - 1);
        protocol_dst[protocol_size - 1] = '\0';
    }
    const char* ver = obj["version"];
    if (ver) {
        strncpy(version_dst, ver, version_size - 1);
        version_dst[version_size - 1] = '\0';
    }
    return DeserializeError::OK;
}

/// Parse the seq field. Returns MISSING_FIELD if absent.
inline DeserializeError parseSeq(JsonObjectConst obj, uint32_t& seq) {
    if (!obj["seq"].is<uint32_t>()) return DeserializeError::MISSING_FIELD;
    seq = obj["seq"].as<uint32_t>();
    return DeserializeError::OK;
}

/// Parse a timestamp from a uint64 wire value.
inline Timestamp parseTimestamp(JsonObjectConst obj, const char* key) {
    if (!obj[key].is<uint64_t>()) return Timestamp{};  // defaults to SESSION, value=0
    return Timestamp::decode(obj[key].as<uint64_t>());
}

// --- Enum parsers -----------------------------------------------------------

inline ApparatusState parseApparatusState(const char* s) {
    if (!s) return ApparatusState::UNKNOWN;
    if (strcmp(s, "F") == 0) return ApparatusState::FENCING;
    if (strcmp(s, "H") == 0) return ApparatusState::HALT;
    if (strcmp(s, "P") == 0) return ApparatusState::PAUSE;
    if (strcmp(s, "W") == 0) return ApparatusState::WAITING;
    if (strcmp(s, "E") == 0) return ApparatusState::ENDING;
    return ApparatusState::UNKNOWN;
}

inline FencerStatus parseFencerStatus(const char* s) {
    if (!s) return FencerStatus::UNKNOWN;
    if (strcmp(s, "U")   == 0) return FencerStatus::UNDEFINED;
    if (strcmp(s, "V")   == 0) return FencerStatus::VICTORY;
    if (strcmp(s, "D")   == 0) return FencerStatus::DEFEAT;
    if (strcmp(s, "A")   == 0) return FencerStatus::ABANDONMENT;
    if (strcmp(s, "E")   == 0) return FencerStatus::EXCLUSION;
    if (strcmp(s, "DNS") == 0) return FencerStatus::DNS;
    return FencerStatus::UNKNOWN;
}

inline Priority parsePriority(const char* s) {
    if (!s) return Priority::UNKNOWN;
    if (strcmp(s, "N") == 0) return Priority::NONE;
    if (strcmp(s, "R") == 0) return Priority::RIGHT;
    if (strcmp(s, "L") == 0) return Priority::LEFT;
    return Priority::UNKNOWN;
}

inline Weapon parseWeapon(const char* s) {
    if (!s) return Weapon::UNKNOWN;
    if (strcmp(s, "F") == 0) return Weapon::FOIL;
    if (strcmp(s, "E") == 0) return Weapon::EPEE;
    if (strcmp(s, "S") == 0) return Weapon::SABRE;
    return Weapon::UNKNOWN;
}

inline MatchType parseMatchType(const char* s) {
    if (!s) return MatchType::UNKNOWN;
    if (strcmp(s, "I") == 0) return MatchType::INDIVIDUAL;
    if (strcmp(s, "T") == 0) return MatchType::TEAM;
    return MatchType::UNKNOWN;
}

inline PhaseType parsePhaseType(const char* s) {
    if (!s) return PhaseType::UNKNOWN;
    if (strcmp(s, "pool")           == 0) return PhaseType::POOL;
    if (strcmp(s, "DE")             == 0) return PhaseType::DE;
    if (strcmp(s, "repechage")      == 0) return PhaseType::REPECHAGE;
    if (strcmp(s, "classification") == 0) return PhaseType::CLASSIFICATION;
    return PhaseType::UNKNOWN;
}

inline Command parseCommand(const char* s) {
    if (!s) return Command::UNKNOWN;
    if (strcmp(s, "NEXT")                 == 0) return Command::NEXT;
    if (strcmp(s, "PREV")                 == 0) return Command::PREV;
    if (strcmp(s, "END")                  == 0) return Command::END;
    if (strcmp(s, "MEDICAL")              == 0) return Command::MEDICAL;
    if (strcmp(s, "RESERVE")              == 0) return Command::RESERVE;
    if (strcmp(s, "VIDEO_REVIEW_REQUEST") == 0) return Command::VIDEO_REVIEW_REQUEST;
    if (strcmp(s, "ACK")                  == 0) return Command::ACK;
    if (strcmp(s, "NAK")                  == 0) return Command::NAK;
    if (strcmp(s, "VIDEO_REVIEW_GRANTED") == 0) return Command::VIDEO_REVIEW_GRANTED;
    if (strcmp(s, "VIDEO_REVIEW_DENIED")  == 0) return Command::VIDEO_REVIEW_DENIED;
    if (strcmp(s, "BEGIN")                == 0) return Command::BEGIN;
    if (strcmp(s, "HALT")                 == 0) return Command::HALT;
    if (strcmp(s, "RESET")                == 0) return Command::RESET;
    if (strcmp(s, "VALIDATE")             == 0) return Command::VALIDATE;
    return Command::UNKNOWN;
}

inline Side parseSide(const char* s) {
    if (!s) return Side::NONE;
    if (strcmp(s, "left")  == 0) return Side::LEFT;
    if (strcmp(s, "right") == 0) return Side::RIGHT;
    return Side::NONE;
}

// --- Sub-struct parsers -----------------------------------------------------

/// Parse a Person block from a JsonObjectConst.
/// Returns false if the object is null/missing (caller treats as absent).
inline bool parsePerson(JsonVariantConst vobj, Person& out) {
    JsonObjectConst obj = vobj.as<JsonObjectConst>();
    if (obj.isNull()) { out.present = false; return false; }
    out.present = true;
    copyStr(obj["id"],     out.id);
    copyStr(obj["name"],   out.name);
    copyStr(obj["nation"], out.nation);
    return true;
}

/// Parse a ScoreState from a JsonObjectConst.
/// Returns MISSING_FIELD if mandatory fields are absent.
inline DeserializeError parseScoreSide(JsonVariantConst vobj, ScoreState& out) {
    JsonObjectConst obj = vobj.as<JsonObjectConst>();
    if (obj.isNull()) return DeserializeError::MISSING_FIELD;
    if (!obj["score"].is<int16_t>()) return DeserializeError::MISSING_FIELD;
    out.score       = obj["score"].as<int16_t>();
    out.status      = parseFencerStatus(obj["status"]);
    out.yellow_card = obj["yellow_card"] | false;
    out.red_cards   = obj["red_cards"]   | (uint8_t)0;
    out.black_card  = obj["black_card"]  | false;
    return DeserializeError::OK;
}

/// Parse time_ms and time string, with fallback in each direction.
/// At least one of time_ms or time must be present (UW2F, Medical spec).
/// Returns MISSING_FIELD if neither is present.
inline DeserializeError parseTimePair(JsonObjectConst obj,
                                      const char* ms_key,
                                      const char* str_key,
                                      uint32_t& time_ms_out,
                                      char* time_str_out,
                                      size_t time_str_size)
{
    bool has_ms  = obj[ms_key].is<uint32_t>();
    bool has_str = obj[str_key].is<const char*>();

    if (!has_ms && !has_str) return DeserializeError::MISSING_FIELD;

    if (has_ms) {
        time_ms_out = obj[ms_key].as<uint32_t>();
        // Generate the string from ms (authoritative)
        TimeFormat::populateTimeStr(time_ms_out, time_str_out, time_str_size);
    } else {
        // Only string present — parse it to get ms
        const char* s = obj[str_key];
        uint32_t parsed_ms = 0;
        if (!TimeFormat::parseToMs(s, parsed_ms)) {
            return DeserializeError::MISSING_FIELD;
        }
        time_ms_out = parsed_ms;
        strncpy(time_str_out, s, time_str_size - 1);
        time_str_out[time_str_size - 1] = '\0';
    }
    return DeserializeError::OK;
}

/// Parse a VideoReviewSide from a JsonObjectConst.
inline DeserializeError parseVideoReviewSide(JsonVariantConst vobj,
                                              VideoReviewSide& out)
{
    JsonObjectConst obj = vobj.as<JsonObjectConst>();
    if (obj.isNull()) return DeserializeError::MISSING_FIELD;
    if (!obj["remaining"].is<int16_t>()) return DeserializeError::MISSING_FIELD;
    out.remaining  = obj["remaining"].as<int16_t>();
    out.call_count = 0;

    JsonArrayConst calls = obj["calls"].as<JsonArrayConst>();
    if (!calls.isNull()) {
        for (JsonObjectConst callObj : calls) {
            if (out.call_count >= OPP2_MAX_VIDEO_CALLS) break;
            VideoCall& vc = out.calls[out.call_count];
            vc.id      = callObj["id"]      | (uint16_t)0;
            vc.round   = callObj["round"]   | (uint8_t)0;
            vc.time_ms = callObj["time_ms"] | (uint32_t)0;
            // 'granted' is absent when pending
            if (callObj["granted"].is<bool>()) {
                vc.granted = callObj["granted"].as<bool>()
                           ? CallResolution::GRANTED
                           : CallResolution::DENIED;
            } else {
                vc.granted = CallResolution::PENDING;
            }
            ++out.call_count;
        }
    }
    return DeserializeError::OK;
}

/// Parse a JsonDocument from a raw payload buffer.
/// Returns INVALID_JSON on failure.
template<size_t N, typename TDoc>
inline DeserializeError parseJson(const char* buf, size_t len, TDoc& doc) {
    if (!buf) return DeserializeError::BUFFER_NULL;
    if (len == 0) return DeserializeError::INVALID_JSON;
    auto result = deserializeJson(doc, buf, len);
    if (result != DeserializationError::Ok) return DeserializeError::INVALID_JSON;
    return DeserializeError::OK;
}

}  // namespace detail

// ============================================================================
// Deserializer
// ============================================================================

class Deserializer {
public:

    // ------------------------------------------------------------------------
    // lights  (Section 8) — QoS 1, retained
    // ------------------------------------------------------------------------

    static DeserializeError deserialize(const char* buf, size_t len, Lights& out) {
        if (!buf) return DeserializeError::BUFFER_NULL;
        StaticJsonDocument<JSON_SIZE_LIGHTS * 2> doc;
        auto err = detail::parseJson<JSON_SIZE_LIGHTS * 2>(buf, len, doc);
        if (err != DeserializeError::OK) return err;

        JsonObjectConst root = doc.as<JsonObjectConst>();
        err = detail::parseCommon(root, out.protocol, sizeof(out.protocol),
                                        out.version,  sizeof(out.version));
        if (err != DeserializeError::OK) return err;
        if (detail::parseSeq(root, out.seq) != DeserializeError::OK)
            return DeserializeError::MISSING_FIELD;

        out.ts = detail::parseTimestamp(root, "ts");

        JsonObjectConst right = root["right"].as<JsonObjectConst>();
        JsonObjectConst left = root["left"].as<JsonObjectConst>();
        if (right.isNull() || left.isNull()) return DeserializeError::MISSING_FIELD;

        out.right.on_target = right["green"] | false;
        out.right.white     = right["white"] | false;
        out.left.on_target  = left["red"]    | false;
        out.left.white      = left["white"]  | false;

        return DeserializeError::OK;
    }

    // ------------------------------------------------------------------------
    // clock  (Section 9) — QoS 0, no seq
    // ------------------------------------------------------------------------

    static DeserializeError deserialize(const char* buf, size_t len, Clock& out) {
        if (!buf) return DeserializeError::BUFFER_NULL;
        StaticJsonDocument<JSON_SIZE_CLOCK * 2> doc;
        auto err = detail::parseJson<JSON_SIZE_CLOCK * 2>(buf, len, doc);
        if (err != DeserializeError::OK) return err;

        JsonObjectConst root = doc.as<JsonObjectConst>();
        err = detail::parseCommon(root, out.protocol, sizeof(out.protocol),
                                        out.version,  sizeof(out.version));
        if (err != DeserializeError::OK) return err;

        out.ts      = detail::parseTimestamp(root, "ts");
        out.running = root["running"] | false;

        // time_ms is mandatory for clock (Section 9)
        if (!root["time_ms"].is<uint32_t>()) return DeserializeError::MISSING_FIELD;
        out.time_ms = root["time_ms"].as<uint32_t>();
        // Regenerate time string from time_ms (authoritative)
        TimeFormat::populateTimeStr(out.time_ms, out.time, sizeof(out.time));

        return DeserializeError::OK;
    }

    // ------------------------------------------------------------------------
    // blade_contact  (Section 10) — QoS 0, no seq
    // ------------------------------------------------------------------------

    static DeserializeError deserialize(const char* buf, size_t len, BladeContact& out) {
        if (!buf) return DeserializeError::BUFFER_NULL;
        StaticJsonDocument<JSON_SIZE_BLADE_CONTACT * 2> doc;
        auto err = detail::parseJson<JSON_SIZE_BLADE_CONTACT * 2>(buf, len, doc);
        if (err != DeserializeError::OK) return err;

        JsonObjectConst root = doc.as<JsonObjectConst>();
        err = detail::parseCommon(root, out.protocol, sizeof(out.protocol),
                                        out.version,  sizeof(out.version));
        if (err != DeserializeError::OK) return err;

        out.ts     = detail::parseTimestamp(root, "ts");
        if (!root["active"].is<bool>()) return DeserializeError::MISSING_FIELD;
        out.active = root["active"].as<bool>();

        return DeserializeError::OK;
    }

    // ------------------------------------------------------------------------
    // score  (Section 11) — QoS 1, retained
    // ------------------------------------------------------------------------

    static DeserializeError deserialize(const char* buf, size_t len, Score& out) {
        if (!buf) return DeserializeError::BUFFER_NULL;
        StaticJsonDocument<JSON_SIZE_SCORE * 2> doc;
        auto err = detail::parseJson<JSON_SIZE_SCORE * 2>(buf, len, doc);
        if (err != DeserializeError::OK) return err;

        JsonObjectConst root = doc.as<JsonObjectConst>();
        err = detail::parseCommon(root, out.protocol, sizeof(out.protocol),
                                        out.version,  sizeof(out.version));
        if (err != DeserializeError::OK) return err;
        if (detail::parseSeq(root, out.seq) != DeserializeError::OK)
            return DeserializeError::MISSING_FIELD;

        err = detail::parseScoreSide(root["right"], out.right);
        if (err != DeserializeError::OK) return err;
        err = detail::parseScoreSide(root["left"], out.left);
        if (err != DeserializeError::OK) return err;

        out.priority = detail::parsePriority(root["priority"]);

        return DeserializeError::OK;
    }

    // ------------------------------------------------------------------------
    // connection  (Section 12) — QoS 1, retained
    // Handles both full payload and minimal LWT {"online": false}.
    // ------------------------------------------------------------------------

    static DeserializeError deserialize(const char* buf, size_t len, Connection& out) {
        if (!buf) return DeserializeError::BUFFER_NULL;
        StaticJsonDocument<JSON_SIZE_CONNECTION * 2> doc;
        auto err = detail::parseJson<JSON_SIZE_CONNECTION * 2>(buf, len, doc);
        if (err != DeserializeError::OK) return err;

        JsonObjectConst root = doc.as<JsonObjectConst>();

        // 'online' is the only mandatory field — present in both LWT and full payload
        if (!root["online"].is<bool>()) return DeserializeError::MISSING_FIELD;
        out.online = root["online"].as<bool>();

        if (!out.online) {
            // LWT form — no other fields expected or required
            return DeserializeError::OK;
        }

        // Full payload
        err = detail::parseCommon(root, out.protocol, sizeof(out.protocol),
                                        out.version,  sizeof(out.version));
        if (err != DeserializeError::OK) return err;
        detail::parseSeq(root, out.seq);  // best-effort — not fatal if absent in LWT context

        if (root["device"].is<const char*>()) {
            detail::copyStr(root["device"].as<const char*>(), out.device);
            out.device_present = true;
        }
        if (root["fw_version"].is<const char*>()) {
            detail::copyStr(root["fw_version"].as<const char*>(), out.fw_version);
            out.fw_version_present = true;
        }

        return DeserializeError::OK;
    }

    // ------------------------------------------------------------------------
    // state → ApparatusStateMsg  (Section 13) — QoS 1, retained
    // ------------------------------------------------------------------------

    static DeserializeError deserialize(const char* buf, size_t len, ApparatusStateMsg& out) {
        if (!buf) return DeserializeError::BUFFER_NULL;
        StaticJsonDocument<JSON_SIZE_STATE * 2> doc;
        auto err = detail::parseJson<JSON_SIZE_STATE * 2>(buf, len, doc);
        if (err != DeserializeError::OK) return err;

        JsonObjectConst root = doc.as<JsonObjectConst>();
        err = detail::parseCommon(root, out.protocol, sizeof(out.protocol),
                                        out.version,  sizeof(out.version));
        if (err != DeserializeError::OK) return err;
        if (detail::parseSeq(root, out.seq) != DeserializeError::OK)
            return DeserializeError::MISSING_FIELD;

        if (!root["state"].is<const char*>()) return DeserializeError::MISSING_FIELD;
        out.state = detail::parseApparatusState(root["state"]);
        // UNKNOWN is a valid result here — forward compatibility

        return DeserializeError::OK;
    }

    // ------------------------------------------------------------------------
    // fencers  (Section 14) — QoS 1, retained
    // ------------------------------------------------------------------------

    static DeserializeError deserialize(const char* buf, size_t len, Fencers& out) {
        if (!buf) return DeserializeError::BUFFER_NULL;
        StaticJsonDocument<JSON_SIZE_FENCERS * 2> doc;
        auto err = detail::parseJson<JSON_SIZE_FENCERS * 2>(buf, len, doc);
        if (err != DeserializeError::OK) return err;

        JsonObjectConst root = doc.as<JsonObjectConst>();
        err = detail::parseCommon(root, out.protocol, sizeof(out.protocol),
                                        out.version,  sizeof(out.version));
        if (err != DeserializeError::OK) return err;
        if (detail::parseSeq(root, out.seq) != DeserializeError::OK)
            return DeserializeError::MISSING_FIELD;

        // left fencer — mandatory
        JsonObjectConst leftObj = root["left"].as<JsonObjectConst>();
        JsonObjectConst rightObj = root["right"].as<JsonObjectConst>();
        if (leftObj.isNull() || rightObj.isNull()) return DeserializeError::MISSING_FIELD;

        if (!detail::parsePerson(leftObj["fencer"],  out.left.fencer))
            return DeserializeError::MISSING_FIELD;
        if (!detail::parsePerson(rightObj["fencer"], out.right.fencer))
            return DeserializeError::MISSING_FIELD;

        // coaches — optional
        detail::parsePerson(leftObj["coach"],  out.left.coach);
        detail::parsePerson(rightObj["coach"], out.right.coach);

        // common section — both optional
        JsonObjectConst common = root["common"].as<JsonObjectConst>();
        if (!common.isNull()) {
            detail::parsePerson(common["referee"],        out.referee);
            detail::parsePerson(common["video_official"], out.video_official);
        }

        return DeserializeError::OK;
    }

    // ------------------------------------------------------------------------
    // match  (Section 15) — QoS 1, retained
    // ------------------------------------------------------------------------

    static DeserializeError deserialize(const char* buf, size_t len, Match& out) {
        if (!buf) return DeserializeError::BUFFER_NULL;
        StaticJsonDocument<JSON_SIZE_MATCH * 2> doc;
        auto err = detail::parseJson<JSON_SIZE_MATCH * 2>(buf, len, doc);
        if (err != DeserializeError::OK) return err;

        JsonObjectConst root = doc.as<JsonObjectConst>();
        err = detail::parseCommon(root, out.protocol, sizeof(out.protocol),
                                        out.version,  sizeof(out.version));
        if (err != DeserializeError::OK) return err;
        if (detail::parseSeq(root, out.seq) != DeserializeError::OK)
            return DeserializeError::MISSING_FIELD;

        if (!root["weapon"].is<const char*>())      return DeserializeError::MISSING_FIELD;
        if (!root["type"].is<const char*>())        return DeserializeError::MISSING_FIELD;
        if (!root["competition"].is<const char*>()) return DeserializeError::MISSING_FIELD;
        if (!root["phase_type"].is<const char*>())  return DeserializeError::MISSING_FIELD;
        if (!root["phase"].is<const char*>())       return DeserializeError::MISSING_FIELD;
        if (!root["poule"].is<const char*>())       return DeserializeError::MISSING_FIELD;
        if (!root["match"].is<uint16_t>())          return DeserializeError::MISSING_FIELD;
        if (!root["round"].is<uint8_t>())           return DeserializeError::MISSING_FIELD;

        out.weapon     = detail::parseWeapon(root["weapon"]);
        out.type       = detail::parseMatchType(root["type"]);
        detail::copyStr(root["competition"].as<const char*>(), out.competition);
        out.phase_type = detail::parsePhaseType(root["phase_type"]);
        detail::copyStr(root["phase"].as<const char*>(), out.phase);
        detail::copyStr(root["poule"].as<const char*>(), out.poule);
        out.match_num  = root["match"].as<uint16_t>();
        out.round      = root["round"].as<uint8_t>();

        if (root["scheduled"].is<const char*>()) {
            detail::copyStr(root["scheduled"].as<const char*>(), out.scheduled);
            out.scheduled_present = true;
        }

        return DeserializeError::OK;
    }

    // ------------------------------------------------------------------------
    // uw2f  (Section 16) — QoS 1, retained
    // ------------------------------------------------------------------------

    static DeserializeError deserialize(const char* buf, size_t len, UW2F& out) {
        if (!buf) return DeserializeError::BUFFER_NULL;
        StaticJsonDocument<JSON_SIZE_UW2F * 2> doc;
        auto err = detail::parseJson<JSON_SIZE_UW2F * 2>(buf, len, doc);
        if (err != DeserializeError::OK) return err;

        JsonObjectConst root = doc.as<JsonObjectConst>();
        err = detail::parseCommon(root, out.protocol, sizeof(out.protocol),
                                        out.version,  sizeof(out.version));
        if (err != DeserializeError::OK) return err;
        if (detail::parseSeq(root, out.seq) != DeserializeError::OK)
            return DeserializeError::MISSING_FIELD;

        // At least one of time_ms / time required (Section 16)
        err = detail::parseTimePair(root, "time_ms", "time",
                                    out.time_ms, out.time, sizeof(out.time));
        if (err != DeserializeError::OK) return err;

        JsonObjectConst right = root["right"].as<JsonObjectConst>();
        JsonObjectConst left = root["left"].as<JsonObjectConst>();
        if (right.isNull() || left.isNull()) return DeserializeError::MISSING_FIELD;

        out.right.p_card = right["p_card"] | (uint8_t)0;
        out.left.p_card  = left["p_card"]  | (uint8_t)0;

        return DeserializeError::OK;
    }

    // ------------------------------------------------------------------------
    // medical  (Section 17) — QoS 1, retained
    // ------------------------------------------------------------------------

    static DeserializeError deserialize(const char* buf, size_t len, Medical& out) {
        if (!buf) return DeserializeError::BUFFER_NULL;
        StaticJsonDocument<JSON_SIZE_MEDICAL * 2> doc;
        auto err = detail::parseJson<JSON_SIZE_MEDICAL * 2>(buf, len, doc);
        if (err != DeserializeError::OK) return err;

        JsonObjectConst root = doc.as<JsonObjectConst>();
        err = detail::parseCommon(root, out.protocol, sizeof(out.protocol),
                                        out.version,  sizeof(out.version));
        if (err != DeserializeError::OK) return err;
        if (detail::parseSeq(root, out.seq) != DeserializeError::OK)
            return DeserializeError::MISSING_FIELD;

        if (!root["active"].is<bool>()) return DeserializeError::MISSING_FIELD;
        out.active = root["active"].as<bool>();

        if (!root["side"].is<const char*>()) return DeserializeError::MISSING_FIELD;
        out.side = detail::parseSide(root["side"]);

        if (out.active) {
            if (!root["duration_ms"].is<uint32_t>()) return DeserializeError::MISSING_FIELD;
            out.duration_ms = root["duration_ms"].as<uint32_t>();

            // At least one of remaining_ms / remaining required (Section 17)
            err = detail::parseTimePair(root, "remaining_ms", "remaining",
                                        out.remaining_ms, out.remaining, sizeof(out.remaining));
            if (err != DeserializeError::OK) return err;
        }

        return DeserializeError::OK;
    }

    // ------------------------------------------------------------------------
    // video_review  (Section 18) — QoS 1, retained
    // ------------------------------------------------------------------------

    static DeserializeError deserialize(const char* buf, size_t len, VideoReview& out) {
        if (!buf) return DeserializeError::BUFFER_NULL;
        StaticJsonDocument<JSON_SIZE_VIDEO_REVIEW * 2> doc;
        auto err = detail::parseJson<JSON_SIZE_VIDEO_REVIEW * 2>(buf, len, doc);
        if (err != DeserializeError::OK) return err;

        JsonObjectConst root = doc.as<JsonObjectConst>();
        err = detail::parseCommon(root, out.protocol, sizeof(out.protocol),
                                        out.version,  sizeof(out.version));
        if (err != DeserializeError::OK) return err;
        if (detail::parseSeq(root, out.seq) != DeserializeError::OK)
            return DeserializeError::MISSING_FIELD;

        err = detail::parseVideoReviewSide(root["left"],  out.left);
        if (err != DeserializeError::OK) return err;
        err = detail::parseVideoReviewSide(root["right"], out.right);
        if (err != DeserializeError::OK) return err;

        return DeserializeError::OK;
    }

    // ------------------------------------------------------------------------
    // control  (Section 19) — QoS 1, NOT retained
    // ------------------------------------------------------------------------

    static DeserializeError deserialize(const char* buf, size_t len, Control& out) {
        if (!buf) return DeserializeError::BUFFER_NULL;
        StaticJsonDocument<JSON_SIZE_CONTROL * 2> doc;
        auto err = detail::parseJson<JSON_SIZE_CONTROL * 2>(buf, len, doc);
        if (err != DeserializeError::OK) return err;

        JsonObjectConst root = doc.as<JsonObjectConst>();
        err = detail::parseCommon(root, out.protocol, sizeof(out.protocol),
                                        out.version,  sizeof(out.version));
        if (err != DeserializeError::OK) return err;
        if (detail::parseSeq(root, out.seq) != DeserializeError::OK)
            return DeserializeError::MISSING_FIELD;

        out.ts = detail::parseTimestamp(root, "ts");

        if (!root["command"].is<const char*>()) return DeserializeError::MISSING_FIELD;
        out.command = detail::parseCommand(root["command"]);
        // UNKNOWN is valid here — forward compatibility, caller must ignore

        if (root["side"].is<const char*>()) {
            out.side = detail::parseSide(root["side"]);
        }
        if (root["duration"].is<uint16_t>()) {
            out.duration         = root["duration"].as<uint16_t>();
            out.duration_present = true;
        }

        return DeserializeError::OK;
    }

};  // class Deserializer

}  // namespace OPP2
