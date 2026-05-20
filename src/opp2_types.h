/**
 * @file opp2_types.h
 * @brief OpenPiste Protocol Level 2 (OPP2) — type definitions
 *
 * This header defines all structs, enums, and constants for the OPP2 protocol.
 * It has no dependencies beyond stdint.h, stdbool.h, and string.h — it is
 * intentionally portable to any C++11 target, including AVR, ESP32, ESP-IDF,
 * desktop Linux, and Windows.
 *
 * Design principles reflected here:
 *   - Plain structs, no virtual dispatch, no inheritance.
 *   - Fixed-size char arrays throughout; no heap allocation.
 *   - One struct per message type, mirroring the JSON wire format directly.
 *   - Enums for every field with a fixed value set; UNKNOWN variant on each.
 *   - Optional fields represented by a companion bool (field_present flag).
 *   - Bi-directional: suitable for both publishers (apparatus, software,
 *     remote) and subscribers (displays, monitors, video tools).
 *
 * Spec reference: OpenPiste Protocol Level 2, draft v1.0, May 2026.
 * Repository:     https://github.com/OpenPiste
 * Website:        https://openpiste.org
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

namespace OPP2 {

// ============================================================================
// Constants
// ============================================================================

/** Protocol family identifier — appears in every message payload. */
static const char PROTOCOL_ID[]  = "OPP2";

/** Protocol version implemented by this header. */
static const char PROTOCOL_VERSION[] = "1.0";

/** Fixed topic prefix for all OPP2 topics. */
static const char TOPIC_PREFIX[]  = "openpiste";

/** Default broker hostname (mDNS, Section 4.2). */
static const char DEFAULT_BROKER_HOST[] = "openpiste.local";

/** Default broker port, unencrypted (Section 4.7). */
static const uint16_t DEFAULT_BROKER_PORT     = 1883;

/** Default broker port, TLS (Section 4.7). */
static const uint16_t DEFAULT_BROKER_PORT_TLS = 8883;

/**
 * Maximum number of video review calls stored per fencer per bout.
 * The spec does not bound this. Override with -DOPP2_MAX_VIDEO_CALLS=N.
 * At the maximum FIE DE call count (2 per fencer) across 9 team rounds
 * and extra overtime bouts, 16 is very conservative.
 */
#ifndef OPP2_MAX_VIDEO_CALLS
#define OPP2_MAX_VIDEO_CALLS 16
#endif

/** Maximum length of a piste identifier string, including null terminator. */
static const size_t PISTE_ID_MAX = 32;

// ============================================================================
// Enumerations
// ============================================================================

/**
 * Publisher role — encoded in the MQTT topic segment, not in the payload.
 * The topic is the authoritative source (Section 5).
 */
enum class Publisher : uint8_t {
    APPARATUS,  ///< Scoring apparatus
    SOFTWARE,   ///< Competition management software
    REMOTE,     ///< Remote control device
    UNKNOWN     ///< Unrecognised value — forward-compatibility (Section 23.4)
};

/**
 * Message type — derived from the final topic segment.
 */
enum class MessageType : uint8_t {
    LIGHTS,
    CLOCK,
    BLADE_CONTACT,
    SCORE,
    CONNECTION,
    APPARATUS_STATE,  ///< The 'state' topic (renamed to avoid clash with OPP2SystemState)
    FENCERS,
    MATCH,
    UW2F,
    MEDICAL,
    VIDEO_REVIEW,
    CONTROL,
    UNKNOWN
};

/**
 * Apparatus operational state (Section 13).
 * Values inherited from EFP1.1.
 */
enum class ApparatusState : uint8_t {
    FENCING,    ///< "F" — stopwatch running
    HALT,       ///< "H" — stopwatch stopped, bout in progress
    PAUSE,      ///< "P" — between periods
    WAITING,    ///< "W" — no active bout (default)
    ENDING,     ///< "E" — awaiting ACK from software
    UNKNOWN
};

/**
 * Fencer match status (Section 11).
 * Values inherited from EFP1.1.
 */
enum class FencerStatus : uint8_t {
    UNDEFINED,    ///< "U" (default)
    VICTORY,      ///< "V"
    DEFEAT,       ///< "D"
    ABANDONMENT,  ///< "A"
    EXCLUSION,    ///< "E"
    DNS,          ///< "DNS" — did not show
    UNKNOWN
};

/**
 * Priority assignment (Section 11).
 */
enum class Priority : uint8_t {
    NONE,   ///< "N" — no priority assigned (default)
    RIGHT,  ///< "R" — right fencer has priority
    LEFT,   ///< "L" — left fencer has priority
    UNKNOWN
};

/**
 * Weapon type (Section 15).
 */
enum class Weapon : uint8_t {
    FOIL,   ///< "F"
    EPEE,   ///< "E"
    SABRE,  ///< "S"
    UNKNOWN
};

/**
 * Match type: individual or team (Section 15).
 */
enum class MatchType : uint8_t {
    INDIVIDUAL, ///< "I"
    TEAM,       ///< "T"
    UNKNOWN
};

/**
 * Competition phase type (Section 15).
 * New values may be added in minor revisions without a version bump (Section 23.4).
 */
enum class PhaseType : uint8_t {
    POOL,           ///< "pool"
    DE,             ///< "DE" — direct elimination
    REPECHAGE,      ///< "repechage"
    CLASSIFICATION, ///< "classification"
    UNKNOWN
};

/**
 * Control command (Section 19).
 * New values may be added in minor revisions without a version bump (Section 23.4).
 * Receivers MUST ignore unknown commands (Section 19).
 */
enum class Command : uint8_t {
    // Apparatus → software
    NEXT,                  ///< Request next match or round
    PREV,                  ///< Request previous match or round
    END,                   ///< Signal end of match or round, awaiting ACK
    MEDICAL,               ///< Medical timeout granted; side + duration required
    RESERVE,               ///< Reserve fencer introduction; side required
    VIDEO_REVIEW_REQUEST,  ///< Fencer requests video review; side required
    // Software → apparatus
    ACK,                   ///< Approve end of match or round
    NAK,                   ///< Reject end of match or round
    VIDEO_REVIEW_GRANTED,  ///< Video review call granted; side required
    VIDEO_REVIEW_DENIED,   ///< Video review call denied; side required
    // Remote → apparatus
    BEGIN,                 ///< Start the bout
    HALT,                  ///< Call halt
    RESET,                 ///< Reset the apparatus
    VALIDATE,              ///< Confirm end of match
    UNKNOWN
};

/**
 * Side — left or right fencer.
 * Used in several messages and commands. NONE means the field was absent.
 */
enum class Side : uint8_t {
    LEFT,
    RIGHT,
    NONE    ///< Field absent — for optional side fields
};

/**
 * Timestamp clock source flag (Section 22.3).
 * Encoded in bits 63–56 of the 64-bit timestamp value.
 */
enum class ClockSource : uint8_t {
    NTP     = 0x00,  ///< UTC Unix epoch milliseconds, NTP-synchronised
    SESSION = 0x01,  ///< Milliseconds since device boot (millis())
    UNKNOWN = 0xFF
};

/**
 * Video review call resolution state.
 * The spec encodes this as a boolean field that may be absent when unresolved.
 */
enum class CallResolution : uint8_t {
    GRANTED,  ///< granted == true
    DENIED,   ///< granted == false
    PENDING   ///< 'granted' field absent — call not yet resolved
};

// ============================================================================
// Timestamp
// ============================================================================

/**
 * OPP2 timestamp (Section 22).
 *
 * Wire format: 64-bit unsigned integer.
 *   Bits 63–56: ClockSource flag
 *   Bits 55–0:  Time value in milliseconds
 *
 * Encode/decode helpers are provided. On NTP-synchronised devices,
 * encode(ClockSource::NTP, epochMs) is sufficient — the upper byte of
 * any current epoch millisecond value is naturally 0x00.
 */
struct Timestamp {
    ClockSource source = ClockSource::SESSION;
    uint64_t    value  = 0;  ///< ms — epoch if NTP, boot-relative if SESSION

    /** Encode to the 64-bit wire representation. */
    uint64_t encode() const {
        return (static_cast<uint64_t>(source) << 56)
             | (value & UINT64_C(0x00FFFFFFFFFFFFFF));
    }

    /** Decode from the 64-bit wire representation. */
    static Timestamp decode(uint64_t raw) {
        Timestamp ts;
        const uint8_t flag = static_cast<uint8_t>((raw >> 56) & 0xFF);
        switch (flag) {
            case 0x00: ts.source = ClockSource::NTP;     break;
            case 0x01: ts.source = ClockSource::SESSION; break;
            default:   ts.source = ClockSource::UNKNOWN; break;
        }
        ts.value = raw & UINT64_C(0x00FFFFFFFFFFFFFF);
        return ts;
    }

    /** Convenience: construct an NTP timestamp from epoch milliseconds. */
    static Timestamp fromEpochMs(uint64_t epochMs) {
        Timestamp ts;
        ts.source = ClockSource::NTP;
        ts.value  = epochMs;
        return ts;
    }

    /** Convenience: construct a session timestamp from millis(). */
    static Timestamp fromMillis(uint32_t ms) {
        Timestamp ts;
        ts.source = ClockSource::SESSION;
        ts.value  = ms;
        return ts;
    }

    bool isValid() const { return source != ClockSource::UNKNOWN; }
};

// ============================================================================
// Topic
// ============================================================================

/**
 * Parsed MQTT topic (Section 5).
 *
 * Topic pattern: openpiste/{piste_id}/{publisher}/{message_type}
 *
 * The topic is the authoritative source of piste_id and publisher.
 * Neither is duplicated in the payload.
 */
struct Topic {
    char        piste_id[PISTE_ID_MAX] = {};
    Publisher   publisher   = Publisher::UNKNOWN;
    MessageType message_type = MessageType::UNKNOWN;
};

// ============================================================================
// Sub-structs (reused across multiple message types)
// ============================================================================

/**
 * Light state for one fencer side (Section 8).
 * Right side: green = on-target, white = off-target or broken circuit.
 * Left side:  red   = on-target, white = off-target or broken circuit.
 * The field is named 'on_target' here for clarity; the serializer maps it
 * to "green" (right) or "red" (left) as required by the wire format.
 */
struct LightState {
    bool on_target = false;  ///< green (right) or red (left)
    bool white     = false;
};

/**
 * Score and card state for one fencer side (Section 11).
 */
struct ScoreState {
    int16_t      score       = 0;
    FencerStatus status      = FencerStatus::UNDEFINED;
    bool         yellow_card = false;
    uint8_t      red_cards   = 0;  ///< 0–9
    bool         black_card  = false;
};

/**
 * A person (fencer, coach, referee, video official) with optional presence flag.
 * Used wherever the spec has an optional participant identity block (Section 14).
 */
struct Person {
    bool present   = false;  ///< false → entire block was absent in the message
    char id[32]    = {};
    char name[64]  = {};
    char nation[5] = {};     ///< IOC 3-letter code + null terminator (e.g. "FRA\0")
};

/**
 * One side's fencer + coach pair (Section 14).
 * fencer.present is always true for a valid Fencers message (mandatory field).
 * coach.present is false when the coach block was absent (optional field).
 */
struct FencerSide {
    Person fencer;  ///< Mandatory
    Person coach;   ///< Optional
};

/**
 * UW2F (unwillingness-to-fight / passivity) P-card state for one side (Section 16).
 */
struct UW2FSide {
    uint8_t p_card = 0;  ///< 0 = none, 1–5 = ordinal position per rulebook
};

/**
 * One video review call record (Section 18).
 */
struct VideoCall {
    uint16_t       id       = 0;
    uint8_t        round    = 0;
    uint32_t       time_ms  = 0;      ///< Stopwatch value at moment of call (ms)
    CallResolution granted  = CallResolution::PENDING;
};

/**
 * Video review state for one fencer side (Section 18).
 */
struct VideoReviewSide {
    int16_t   remaining  = 0;
    VideoCall calls[OPP2_MAX_VIDEO_CALLS];
    uint8_t   call_count = 0;  ///< Number of valid entries in calls[]
};

// ============================================================================
// Message structs
// ============================================================================

// ----------------------------------------------------------------------------
// Lights (Section 8) — QoS 1, retained
// Publisher: apparatus
// ----------------------------------------------------------------------------

/**
 * lights message.
 * Published immediately on any change to the light state.
 * ts is mandatory (Section 7).
 */
struct Lights {
    // Common fields
    char     protocol[8] = "OPP2";
    char     version[8]  = "1.0";
    uint32_t seq         = 0;
    Timestamp ts;

    // Payload fields
    LightState right;  ///< green + white
    LightState left;   ///< red   + white
};

// ----------------------------------------------------------------------------
// Clock (Section 9) — QoS 0, retained
// Publisher: apparatus
// No seq field (QoS 0, Section 7 / 21.6)
// ----------------------------------------------------------------------------

/**
 * clock message.
 * Published once per second while running; also on any state change.
 * ts is mandatory (Section 7).
 *
 * time[] is the formatted representation of time_ms.
 * Serializer generates time[] from time_ms automatically.
 * Format: "M:SS" normally; "M:SS.cc" when time_ms < 10000 (hundredths mandatory,
 * Section 20.1 and EFP1.1 convention).
 */
struct Clock {
    // Common fields (no seq — QoS 0)
    char      protocol[8] = "OPP2";
    char      version[8]  = "1.0";
    Timestamp ts;

    // Payload fields
    bool     running = false;
    uint32_t time_ms = 0;
    char     time[12] = {};  ///< Serializer populates this from time_ms
};

// ----------------------------------------------------------------------------
// BladeContact (Section 10) — QoS 0, NOT retained
// Publisher: apparatus
// No seq field (QoS 0, Section 7 / 21.6)
// PROVISIONAL: semantics (stateful vs momentary) are an open item (Section 25).
// Current decision: momentary event — published on detection only.
// ----------------------------------------------------------------------------

/**
 * blade_contact message.
 * Published on blade contact events. Primary purpose: video sync timestamp.
 * ts is mandatory (Section 7).
 *
 * NOTE (Section 25 open item): The 'active' field is retained from the spec
 * draft pending resolution of the stateful-vs-momentary question. Current
 * agreed interpretation: this is a momentary event message.
 */
struct BladeContact {
    // Common fields (no seq — QoS 0)
    char      protocol[8] = "OPP2";
    char      version[8]  = "1.0";
    Timestamp ts;

    // Payload fields
    bool active = false;  ///< PROVISIONAL — see note above
};

// ----------------------------------------------------------------------------
// Score (Section 11) — QoS 1, retained
// Publisher: apparatus or software
// ----------------------------------------------------------------------------

/**
 * score message.
 * Published on any change to scores, cards, or priority.
 * Publisher segment in topic identifies whether apparatus or software sent it.
 */
struct Score {
    // Common fields
    char     protocol[8] = "OPP2";
    char     version[8]  = "1.0";
    uint32_t seq         = 0;

    // Payload fields
    ScoreState right;
    ScoreState left;
    Priority   priority = Priority::NONE;
};

// ----------------------------------------------------------------------------
// Connection (Section 12) — QoS 1, retained
// Publisher: apparatus
// LWT payload is {"online": false} with no other fields (Section 4.6).
// Deserializer handles this gracefully: sets online=false, all else at default.
// ----------------------------------------------------------------------------

/**
 * connection message.
 * Indicates whether the apparatus is connected to the broker.
 * The LWT (broker-published on unexpected disconnect) carries only
 * {"online": false}; all other fields will be at their zero defaults.
 */
struct Connection {
    // Common fields
    char     protocol[8] = "OPP2";
    char     version[8]  = "1.0";
    uint32_t seq         = 0;  ///< 0 when parsed from LWT

    // Payload fields
    bool online = false;

    // Optional fields
    char device[64]     = {};   ///< Device model/identifier
    char fw_version[32] = {};   ///< Firmware version string
    bool device_present     = false;
    bool fw_version_present = false;
};

// ----------------------------------------------------------------------------
// ApparatusStateMsg (Section 13) — QoS 1, retained
// Publisher: apparatus
// Renamed from "State" to "ApparatusStateMsg" to avoid ambiguity with
// OPP2SystemState (the full piste state aggregate). Implementation decision,
// not a spec change — the topic name remains "state".
// ----------------------------------------------------------------------------

/**
 * state message (apparatus operational state).
 * Published on every state transition.
 */
struct ApparatusStateMsg {
    // Common fields
    char     protocol[8] = "OPP2";
    char     version[8]  = "1.0";
    uint32_t seq         = 0;

    // Payload fields
    ApparatusState state = ApparatusState::WAITING;
};

// ----------------------------------------------------------------------------
// Fencers (Section 14) — QoS 1, retained
// Publisher: software
// ----------------------------------------------------------------------------

/**
 * fencers message.
 * Published when any participant identity changes.
 * left.fencer and right.fencer are mandatory; all others are optional.
 */
struct Fencers {
    // Common fields
    char     protocol[8] = "OPP2";
    char     version[8]  = "1.0";
    uint32_t seq         = 0;

    // Payload fields
    FencerSide left;
    FencerSide right;

    // common section
    Person referee;        ///< Optional
    Person video_official; ///< Optional
};

// ----------------------------------------------------------------------------
// Match (Section 15) — QoS 1, retained
// Publisher: software
// ----------------------------------------------------------------------------

/**
 * match message.
 * Published when match or competition metadata changes.
 * 'scheduled' is optional; all other fields are mandatory.
 */
struct Match {
    // Common fields
    char     protocol[8] = "OPP2";
    char     version[8]  = "1.0";
    uint32_t seq         = 0;

    // Payload fields
    Weapon    weapon    = Weapon::UNKNOWN;
    MatchType type      = MatchType::UNKNOWN;
    char      competition[64] = {};
    PhaseType phase_type = PhaseType::UNKNOWN;
    char      phase[16]  = {};
    char      poule[16]  = {};
    uint16_t  match_num  = 0;   ///< Named match_num to avoid clash with struct name
    uint8_t   round      = 1;

    // Optional
    char scheduled[8]      = {};  ///< "HH:MM"
    bool scheduled_present = false;
};

// ----------------------------------------------------------------------------
// UW2F (Section 16) — QoS 1, retained
// Publisher: apparatus
// ----------------------------------------------------------------------------

/**
 * uw2f message (unwillingness-to-fight / passivity timer and P-cards).
 * Timer counts upward from zero.
 *
 * Spec requires at least one of time_ms or time to be present (Section 16).
 * The serializer emits both; the deserializer accepts either.
 * time[] is generated by the serializer from time_ms.
 */
struct UW2F {
    // Common fields
    char     protocol[8] = "OPP2";
    char     version[8]  = "1.0";
    uint32_t seq         = 0;

    // Payload fields
    uint32_t time_ms = 0;
    char     time[12] = {};  ///< Serializer populates from time_ms
    UW2FSide right;
    UW2FSide left;
};

// ----------------------------------------------------------------------------
// Medical (Section 17) — QoS 1, retained
// Publisher: apparatus
// ----------------------------------------------------------------------------

/**
 * medical message.
 * Published when a medical timeout is granted and on every timer update.
 * When active=false, duration_ms/remaining_ms/remaining are meaningless
 * and the serializer omits them.
 *
 * Spec requires at least one of remaining_ms or remaining when active (Section 17).
 * The serializer emits both; the deserializer accepts either.
 * remaining[] is generated by the serializer from remaining_ms.
 */
struct Medical {
    // Common fields
    char     protocol[8] = "OPP2";
    char     version[8]  = "1.0";
    uint32_t seq         = 0;

    // Payload fields
    bool     active      = false;
    Side     side        = Side::NONE;
    uint32_t duration_ms = 0;   ///< Total timeout duration (ms); meaningful when active
    uint32_t remaining_ms = 0;  ///< Remaining time (ms); meaningful when active
    char     remaining[12] = {}; ///< Serializer populates from remaining_ms
};

// ----------------------------------------------------------------------------
// VideoReview (Section 18) — QoS 1, retained
// Publisher: apparatus or software
// ----------------------------------------------------------------------------

/**
 * video_review message.
 * Carries remaining call counts and full call history for the bout.
 */
struct VideoReview {
    // Common fields
    char     protocol[8] = "OPP2";
    char     version[8]  = "1.0";
    uint32_t seq         = 0;

    // Payload fields
    VideoReviewSide left;
    VideoReviewSide right;
};

// ----------------------------------------------------------------------------
// Control (Section 19) — QoS 1, NOT retained
// Publisher: apparatus, software, or remote
// ----------------------------------------------------------------------------

/**
 * control message.
 * One-shot commands between apparatus, software, and remote.
 * ts is mandatory (Section 7).
 * side and duration are optional — present depending on command type.
 */
struct Control {
    // Common fields
    char      protocol[8] = "OPP2";
    char      version[8]  = "1.0";
    uint32_t  seq         = 0;
    Timestamp ts;

    // Payload fields
    Command command = Command::UNKNOWN;

    // Optional fields
    Side     side             = Side::NONE;      ///< Side::NONE means absent
    uint16_t duration         = 0;               ///< Seconds; MEDICAL only
    bool     duration_present = false;
};

// ============================================================================
// System state aggregate
// ============================================================================

/**
 * OPP2SystemState — the complete live state of one piste.
 *
 * This struct aggregates all retained-topic message structs.
 * It is the natural state container for:
 *   - Apparatus firmware (what has been published)
 *   - Subscriber implementations (mirror of what has been received)
 *
 * blade_contact and control are excluded: they are not retained (Section 4.5)
 * and represent point-in-time events, not ongoing state.
 *
 * Usage pattern (subscriber):
 *   Initialise one SystemState per piste_id.
 *   On each received message, call the appropriate update method (or update
 *   the field directly). Query any field at any time for current piste state.
 */
struct SystemState {
    char piste_id[PISTE_ID_MAX] = {};

    Lights           lights;
    Clock            clock;
    Score            score;
    Connection       connection;
    ApparatusStateMsg apparatus_state;
    Fencers          fencers;
    Match            match;
    UW2F             uw2f;
    Medical          medical;
    VideoReview      video_review;
};

// ============================================================================
// QoS and retention metadata
// (Convenience constants; used by transport adapters and test harnesses.)
// ============================================================================

struct MessageMeta {
    MessageType type;
    uint8_t     qos;
    bool        retained;
    Publisher   expected_publisher;  ///< UNKNOWN means multiple publishers valid
};

/**
 * Per-message-type QoS and retention properties (Section 6).
 * Index by MessageType cast to uint8_t.
 * Publisher::UNKNOWN means multiple publishers are valid for that message type.
 */
static const MessageMeta MESSAGE_META[] = {
    // type                      qos  retained  expected_publisher
    { MessageType::LIGHTS,         1,  true,  Publisher::APPARATUS },
    { MessageType::CLOCK,          0,  true,  Publisher::APPARATUS },
    { MessageType::BLADE_CONTACT,  0,  false, Publisher::APPARATUS },
    { MessageType::SCORE,          1,  true,  Publisher::UNKNOWN   },  // apparatus or software
    { MessageType::CONNECTION,     1,  true,  Publisher::APPARATUS },
    { MessageType::APPARATUS_STATE,1,  true,  Publisher::APPARATUS },
    { MessageType::FENCERS,        1,  true,  Publisher::SOFTWARE  },
    { MessageType::MATCH,          1,  true,  Publisher::SOFTWARE  },
    { MessageType::UW2F,           1,  true,  Publisher::APPARATUS },
    { MessageType::MEDICAL,        1,  true,  Publisher::APPARATUS },
    { MessageType::VIDEO_REVIEW,   1,  true,  Publisher::UNKNOWN   },  // apparatus or software
    { MessageType::CONTROL,        1,  false, Publisher::UNKNOWN   },  // any publisher
};

}  // namespace OPP2
