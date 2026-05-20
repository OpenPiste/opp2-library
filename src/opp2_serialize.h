/**
 * @file opp2_serialize.h
 * @brief OPP2 message serializer — struct → JSON string.
 *
 * Converts OPP2 message structs to JSON payloads suitable for publishing
 * over MQTT. All output is a flat null-terminated char buffer; no heap
 * allocation occurs inside the serializer itself.
 *
 * Dependency: ArduinoJson v6 or v7.
 *   ArduinoJson is header-only, works identically on ESP32 (Arduino and
 *   ESP-IDF), desktop Linux, and Windows. It is the only external dependency
 *   of this file.
 *
 *   PlatformIO:  lib_deps = bblanchon/ArduinoJson
 *   CMake/Linux: fetch from https://github.com/bblanchon/ArduinoJson
 *
 * Buffer sizing:
 *   Each serialize() overload takes a caller-provided buffer and its size.
 *   The recommended minimum sizes are given as OPP2_JSON_SIZE_* constants
 *   below. On embedded targets, allocate these on the stack or as static
 *   buffers. On Linux, 512 bytes is sufficient for every message type.
 *
 * Error handling:
 *   All serialize() functions return an OPP2::SerializeError.
 *   OK           — success, buf contains valid JSON.
 *   BUFFER_SMALL — the provided buffer was too small; buf is empty.
 *   INVALID_ARG  — a mandatory enum field was UNKNOWN, or buf was null.
 *
 * Thread safety:
 *   All functions are stateless and re-entrant. No global state is modified.
 *
 * Wire format compliance:
 *   - Enum values are serialized to their exact wire strings (Section 5/11–19).
 *   - Optional fields with present=false are omitted from the output.
 *   - time[] strings are generated from _ms values (Section 20.1).
 *   - ts is encoded as a 64-bit integer (Section 22).
 *   - seq is omitted on QoS 0 messages (clock, blade_contact).
 *   - LWT payload for connection offline is {"online":false} only (Section 4.6).
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "opp2_types.h"
#include "opp2_topic.h"
#include "opp2_time.h"
#include <ArduinoJson.h>

namespace OPP2 {

// ============================================================================
// Error type
// ============================================================================

enum class SerializeError : uint8_t {
    OK,
    BUFFER_SMALL,  ///< Output buffer too small for the serialized JSON
    INVALID_ARG,   ///< Null buffer, or a mandatory enum field was UNKNOWN
};

// ============================================================================
// Recommended minimum buffer sizes (bytes)
// These are generous — actual output is typically 30–50% smaller.
// ============================================================================

static const size_t JSON_SIZE_LIGHTS       = 128;
static const size_t JSON_SIZE_CLOCK        = 128;
static const size_t JSON_SIZE_BLADE_CONTACT= 96;
static const size_t JSON_SIZE_SCORE        = 256;
static const size_t JSON_SIZE_CONNECTION   = 160;
static const size_t JSON_SIZE_STATE        = 96;
static const size_t JSON_SIZE_FENCERS      = 512;
static const size_t JSON_SIZE_MATCH        = 256;
static const size_t JSON_SIZE_UW2F         = 160;
static const size_t JSON_SIZE_MEDICAL      = 192;
static const size_t JSON_SIZE_VIDEO_REVIEW = 512;
static const size_t JSON_SIZE_CONTROL      = 160;

/// Safe size for any single OPP2 message (use when type is not known at
/// compile time, e.g. in a generic transport buffer).
static const size_t JSON_SIZE_MAX          = 512;

// ============================================================================
// Internal helpers (not part of the public API)
// ============================================================================

namespace detail {

/// Write the three common fields present on every QoS 1 message.
inline void writeCommonQoS1(JsonObject& obj,
                             const char* protocol,
                             const char* version,
                             uint32_t    seq)
{
    obj["protocol"] = protocol;
    obj["version"]  = version;
    obj["seq"]      = seq;
}

/// Write the two common fields present on every QoS 0 message (no seq).
inline void writeCommonQoS0(JsonObject& obj,
                             const char* protocol,
                             const char* version)
{
    obj["protocol"] = protocol;
    obj["version"]  = version;
}

/// Write a Timestamp as a 64-bit integer.
/// ArduinoJson represents uint64 correctly on all platforms.
inline void writeTimestamp(JsonObject& obj, const char* key, const Timestamp& ts) {
    obj[key] = ts.encode();
}

/// Write both time_ms and the formatted time string.
/// Used by Clock, UW2F, Medical.
inline void writeTimeFields(JsonObject& obj,
                             const char* ms_key,
                             const char* str_key,
                             uint32_t    time_ms,
                             char*       time_buf,
                             size_t      time_buf_size)
{
    obj[ms_key] = time_ms;
    TimeFormat::populateTimeStr(time_ms, time_buf, time_buf_size);
    obj[str_key] = time_buf;
}

/// Serialize a Person block into a JsonObject.
/// Used by Fencers for fencer, coach, referee, video_official.
inline void writePerson(JsonObject& obj, const Person& p) {
    obj["id"]     = p.id;
    obj["name"]   = p.name;
    obj["nation"] = p.nation;
}

/// Serialize a FencerSide (fencer + optional coach) into a JsonObject.
inline void writeFencerSide(JsonObject& sideObj, const FencerSide& side) {
    JsonObject fencerObj = sideObj["fencer"].to<JsonObject>();
    writePerson(fencerObj, side.fencer);
    if (side.coach.present) {
        JsonObject coachObj = sideObj["coach"].to<JsonObject>();
        writePerson(coachObj, side.coach);
    }
}

/// Serialize a ScoreState into a JsonObject.
inline SerializeError writeScoreSide(JsonObject& obj, const ScoreState& s) {
    obj["score"]       = s.score;
    const char* status = nullptr;
    switch (s.status) {
        case FencerStatus::UNDEFINED:   status = "U";   break;
        case FencerStatus::VICTORY:     status = "V";   break;
        case FencerStatus::DEFEAT:      status = "D";   break;
        case FencerStatus::ABANDONMENT: status = "A";   break;
        case FencerStatus::EXCLUSION:   status = "E";   break;
        case FencerStatus::DNS:         status = "DNS"; break;
        default: return SerializeError::INVALID_ARG;
    }
    obj["status"]      = status;
    obj["yellow_card"] = s.yellow_card;
    obj["red_cards"]   = s.red_cards;
    obj["black_card"]  = s.black_card;
    return SerializeError::OK;
}

/// Finalize: serialize JsonDocument to the caller's buffer.
/// Returns BUFFER_SMALL if the buffer is insufficient.
template<typename TDoc>
inline SerializeError finalize(TDoc& doc, char* buf, size_t buf_size) {
    size_t needed = measureJson(doc) + 1;
    if (needed > buf_size) {
        if (buf_size > 0) buf[0] = '\0';
        return SerializeError::BUFFER_SMALL;
    }
    serializeJson(doc, buf, buf_size);
    return SerializeError::OK;
}

}  // namespace detail

// ============================================================================
// Serializer
// ============================================================================

class Serializer {
public:

    // ------------------------------------------------------------------------
    // lights  (Section 8) — QoS 1, retained
    // ------------------------------------------------------------------------

    static SerializeError serialize(const Lights& msg, char* buf, size_t buf_size) {
        if (!buf) return SerializeError::INVALID_ARG;

        StaticJsonDocument<JSON_SIZE_LIGHTS> doc;
        JsonObject root = doc.to<JsonObject>();

        detail::writeCommonQoS1(root, msg.protocol, msg.version, msg.seq);
        detail::writeTimestamp(root, "ts", msg.ts);

        JsonObject right = root["right"].to<JsonObject>();
        right["green"] = msg.right.on_target;
        right["white"] = msg.right.white;

        JsonObject left = root["left"].to<JsonObject>();
        left["red"]   = msg.left.on_target;
        left["white"] = msg.left.white;

        return detail::finalize(doc, buf, buf_size);
    }

    // ------------------------------------------------------------------------
    // clock  (Section 9) — QoS 0, retained, no seq
    // ------------------------------------------------------------------------

    static SerializeError serialize(const Clock& msg, char* buf, size_t buf_size) {
        if (!buf) return SerializeError::INVALID_ARG;

        StaticJsonDocument<JSON_SIZE_CLOCK> doc;
        JsonObject root = doc.to<JsonObject>();

        detail::writeCommonQoS0(root, msg.protocol, msg.version);
        detail::writeTimestamp(root, "ts", msg.ts);
        root["running"] = msg.running;

        // time_ms and formatted time string (Section 9, Section 20.1)
        char time_buf[12];
        detail::writeTimeFields(root, "time_ms", "time",
                                msg.time_ms, time_buf, sizeof(time_buf));

        return detail::finalize(doc, buf, buf_size);
    }

    // ------------------------------------------------------------------------
    // blade_contact  (Section 10) — QoS 0, NOT retained, no seq
    // PROVISIONAL — see opp2_types.h note on BladeContact.
    // ------------------------------------------------------------------------

    static SerializeError serialize(const BladeContact& msg, char* buf, size_t buf_size) {
        if (!buf) return SerializeError::INVALID_ARG;

        StaticJsonDocument<JSON_SIZE_BLADE_CONTACT> doc;
        JsonObject root = doc.to<JsonObject>();

        detail::writeCommonQoS0(root, msg.protocol, msg.version);
        detail::writeTimestamp(root, "ts", msg.ts);
        root["active"] = msg.active;

        return detail::finalize(doc, buf, buf_size);
    }

    // ------------------------------------------------------------------------
    // score  (Section 11) — QoS 1, retained
    // ------------------------------------------------------------------------

    static SerializeError serialize(const Score& msg, char* buf, size_t buf_size) {
        if (!buf) return SerializeError::INVALID_ARG;

        StaticJsonDocument<JSON_SIZE_SCORE> doc;
        JsonObject root = doc.to<JsonObject>();

        detail::writeCommonQoS1(root, msg.protocol, msg.version, msg.seq);

        JsonObject right = root["right"].to<JsonObject>();
        SerializeError err = detail::writeScoreSide(right, msg.right);
        if (err != SerializeError::OK) return err;

        JsonObject left = root["left"].to<JsonObject>();
        err = detail::writeScoreSide(left, msg.left);
        if (err != SerializeError::OK) return err;

        const char* priority = nullptr;
        switch (msg.priority) {
            case Priority::NONE:  priority = "N"; break;
            case Priority::RIGHT: priority = "R"; break;
            case Priority::LEFT:  priority = "L"; break;
            default: return SerializeError::INVALID_ARG;
        }
        root["priority"] = priority;

        return detail::finalize(doc, buf, buf_size);
    }

    // ------------------------------------------------------------------------
    // connection  (Section 12) — QoS 1, retained
    // Two forms: full (online=true) and LWT (online=false, minimal payload).
    // ------------------------------------------------------------------------

    static SerializeError serialize(const Connection& msg, char* buf, size_t buf_size) {
        if (!buf) return SerializeError::INVALID_ARG;

        StaticJsonDocument<JSON_SIZE_CONNECTION> doc;
        JsonObject root = doc.to<JsonObject>();

        if (!msg.online) {
            // LWT form — minimal payload (Section 4.6)
            root["online"] = false;
        } else {
            detail::writeCommonQoS1(root, msg.protocol, msg.version, msg.seq);
            root["online"] = true;
            if (msg.device_present)     root["device"]     = msg.device;
            if (msg.fw_version_present) root["fw_version"] = msg.fw_version;
        }

        return detail::finalize(doc, buf, buf_size);
    }

    // ------------------------------------------------------------------------
    // state (ApparatusStateMsg)  (Section 13) — QoS 1, retained
    // Wire key is "state"; struct is ApparatusStateMsg to avoid name clash.
    // ------------------------------------------------------------------------

    static SerializeError serialize(const ApparatusStateMsg& msg, char* buf, size_t buf_size) {
        if (!buf) return SerializeError::INVALID_ARG;

        StaticJsonDocument<JSON_SIZE_STATE> doc;
        JsonObject root = doc.to<JsonObject>();

        detail::writeCommonQoS1(root, msg.protocol, msg.version, msg.seq);

        const char* state = nullptr;
        switch (msg.state) {
            case ApparatusState::FENCING: state = "F"; break;
            case ApparatusState::HALT:    state = "H"; break;
            case ApparatusState::PAUSE:   state = "P"; break;
            case ApparatusState::WAITING: state = "W"; break;
            case ApparatusState::ENDING:  state = "E"; break;
            default: return SerializeError::INVALID_ARG;
        }
        root["state"] = state;

        return detail::finalize(doc, buf, buf_size);
    }

    // ------------------------------------------------------------------------
    // fencers  (Section 14) — QoS 1, retained
    // ------------------------------------------------------------------------

    static SerializeError serialize(const Fencers& msg, char* buf, size_t buf_size) {
        if (!buf) return SerializeError::INVALID_ARG;

        StaticJsonDocument<JSON_SIZE_FENCERS> doc;
        JsonObject root = doc.to<JsonObject>();

        detail::writeCommonQoS1(root, msg.protocol, msg.version, msg.seq);

        JsonObject left  = root["left"].to<JsonObject>();
        detail::writeFencerSide(left, msg.left);

        JsonObject right = root["right"].to<JsonObject>();
        detail::writeFencerSide(right, msg.right);

        // common section — both fields optional
        if (msg.referee.present || msg.video_official.present) {
            JsonObject common = root["common"].to<JsonObject>();
            if (msg.referee.present) {
                JsonObject refObj = common["referee"].to<JsonObject>();
                detail::writePerson(refObj, msg.referee);
            }
            if (msg.video_official.present) {
                JsonObject voObj = common["video_official"].to<JsonObject>();
                detail::writePerson(voObj, msg.video_official);
            }
        }

        return detail::finalize(doc, buf, buf_size);
    }

    // ------------------------------------------------------------------------
    // match  (Section 15) — QoS 1, retained
    // ------------------------------------------------------------------------

    static SerializeError serialize(const Match& msg, char* buf, size_t buf_size) {
        if (!buf) return SerializeError::INVALID_ARG;

        StaticJsonDocument<JSON_SIZE_MATCH> doc;
        JsonObject root = doc.to<JsonObject>();

        detail::writeCommonQoS1(root, msg.protocol, msg.version, msg.seq);

        const char* weapon = nullptr;
        switch (msg.weapon) {
            case Weapon::FOIL:  weapon = "F"; break;
            case Weapon::EPEE:  weapon = "E"; break;
            case Weapon::SABRE: weapon = "S"; break;
            default: return SerializeError::INVALID_ARG;
        }
        root["weapon"] = weapon;

        const char* type = nullptr;
        switch (msg.type) {
            case MatchType::INDIVIDUAL: type = "I"; break;
            case MatchType::TEAM:       type = "T"; break;
            default: return SerializeError::INVALID_ARG;
        }
        root["type"] = type;

        root["competition"] = msg.competition;

        const char* phase_type = nullptr;
        switch (msg.phase_type) {
            case PhaseType::POOL:           phase_type = "pool";           break;
            case PhaseType::DE:             phase_type = "DE";             break;
            case PhaseType::REPECHAGE:      phase_type = "repechage";      break;
            case PhaseType::CLASSIFICATION: phase_type = "classification"; break;
            default: return SerializeError::INVALID_ARG;
        }
        root["phase_type"] = phase_type;

        root["phase"]  = msg.phase;
        root["poule"]  = msg.poule;
        root["match"]  = msg.match_num;
        root["round"]  = msg.round;

        if (msg.scheduled_present) root["scheduled"] = msg.scheduled;

        return detail::finalize(doc, buf, buf_size);
    }

    // ------------------------------------------------------------------------
    // uw2f  (Section 16) — QoS 1, retained
    // ------------------------------------------------------------------------

    static SerializeError serialize(const UW2F& msg, char* buf, size_t buf_size) {
        if (!buf) return SerializeError::INVALID_ARG;

        StaticJsonDocument<JSON_SIZE_UW2F> doc;
        JsonObject root = doc.to<JsonObject>();

        detail::writeCommonQoS1(root, msg.protocol, msg.version, msg.seq);

        char time_buf[12];
        detail::writeTimeFields(root, "time_ms", "time",
                                msg.time_ms, time_buf, sizeof(time_buf));

        JsonObject right = root["right"].to<JsonObject>();
        right["p_card"] = msg.right.p_card;

        JsonObject left = root["left"].to<JsonObject>();
        left["p_card"] = msg.left.p_card;

        return detail::finalize(doc, buf, buf_size);
    }

    // ------------------------------------------------------------------------
    // medical  (Section 17) — QoS 1, retained
    // ------------------------------------------------------------------------

    static SerializeError serialize(const Medical& msg, char* buf, size_t buf_size) {
        if (!buf) return SerializeError::INVALID_ARG;
        if (msg.side == Side::NONE) return SerializeError::INVALID_ARG;

        StaticJsonDocument<JSON_SIZE_MEDICAL> doc;
        JsonObject root = doc.to<JsonObject>();

        detail::writeCommonQoS1(root, msg.protocol, msg.version, msg.seq);
        root["active"] = msg.active;
        root["side"]   = (msg.side == Side::LEFT) ? "left" : "right";

        if (msg.active) {
            root["duration_ms"] = msg.duration_ms;
            char rem_buf[12];
            detail::writeTimeFields(root, "remaining_ms", "remaining",
                                    msg.remaining_ms, rem_buf, sizeof(rem_buf));
        }

        return detail::finalize(doc, buf, buf_size);
    }

    // ------------------------------------------------------------------------
    // video_review  (Section 18) — QoS 1, retained
    // ------------------------------------------------------------------------

    static SerializeError serialize(const VideoReview& msg, char* buf, size_t buf_size) {
        if (!buf) return SerializeError::INVALID_ARG;

        StaticJsonDocument<JSON_SIZE_VIDEO_REVIEW> doc;
        JsonObject root = doc.to<JsonObject>();

        detail::writeCommonQoS1(root, msg.protocol, msg.version, msg.seq);

        // Helper lambda — serialize one VideoReviewSide
        // Written as a local function to avoid repetition without
        // introducing a separate detail:: function that needs the doc ref.
        auto writeSide = [&](const char* key, const VideoReviewSide& side) {
            JsonObject sideObj = root[key].to<JsonObject>();
            sideObj["remaining"] = side.remaining;
            JsonArray calls = sideObj["calls"].to<JsonArray>();
            for (uint8_t i = 0; i < side.call_count; ++i) {
                const VideoCall& vc = side.calls[i];
                JsonObject callObj = calls.add<JsonObject>();
                callObj["id"]      = vc.id;
                callObj["round"]   = vc.round;
                callObj["time_ms"] = vc.time_ms;
                // 'granted' is absent when PENDING (Section 18)
                if (vc.granted == CallResolution::GRANTED) callObj["granted"] = true;
                if (vc.granted == CallResolution::DENIED)  callObj["granted"] = false;
            }
        };

        writeSide("left",  msg.left);
        writeSide("right", msg.right);

        return detail::finalize(doc, buf, buf_size);
    }

    // ------------------------------------------------------------------------
    // control  (Section 19) — QoS 1, NOT retained
    // ------------------------------------------------------------------------

    static SerializeError serialize(const Control& msg, char* buf, size_t buf_size) {
        if (!buf) return SerializeError::INVALID_ARG;

        StaticJsonDocument<JSON_SIZE_CONTROL> doc;
        JsonObject root = doc.to<JsonObject>();

        detail::writeCommonQoS1(root, msg.protocol, msg.version, msg.seq);
        detail::writeTimestamp(root, "ts", msg.ts);

        const char* command = nullptr;
        switch (msg.command) {
            case Command::NEXT:                 command = "NEXT";                  break;
            case Command::PREV:                 command = "PREV";                  break;
            case Command::END:                  command = "END";                   break;
            case Command::MEDICAL:              command = "MEDICAL";               break;
            case Command::RESERVE:              command = "RESERVE";               break;
            case Command::VIDEO_REVIEW_REQUEST: command = "VIDEO_REVIEW_REQUEST";  break;
            case Command::ACK:                  command = "ACK";                   break;
            case Command::NAK:                  command = "NAK";                   break;
            case Command::VIDEO_REVIEW_GRANTED: command = "VIDEO_REVIEW_GRANTED";  break;
            case Command::VIDEO_REVIEW_DENIED:  command = "VIDEO_REVIEW_DENIED";   break;
            case Command::BEGIN:                command = "BEGIN";                 break;
            case Command::HALT:                 command = "HALT";                  break;
            case Command::RESET:                command = "RESET";                 break;
            case Command::VALIDATE:             command = "VALIDATE";              break;
            default: return SerializeError::INVALID_ARG;
        }
        root["command"] = command;

        // Optional fields
        if (msg.side != Side::NONE)
            root["side"] = (msg.side == Side::LEFT) ? "left" : "right";
        if (msg.duration_present)
            root["duration"] = msg.duration;

        return detail::finalize(doc, buf, buf_size);
    }

};  // class Serializer

}  // namespace OPP2
