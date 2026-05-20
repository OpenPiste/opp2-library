/**
 * @file opp2_dispatcher.h
 * @brief OPP2 message dispatcher — routes incoming MQTT messages to callbacks.
 *
 * The Dispatcher is the main entry point for subscriber implementations.
 * It combines topic parsing and deserialization into a single call, then
 * invokes a user-supplied callback for the message type received.
 *
 * Usage pattern:
 *
 *   OPP2::Dispatcher dispatcher;
 *
 *   dispatcher.onLights = [](const OPP2::Topic& t, const OPP2::Lights& msg) {
 *       // handle lights change on piste t.piste_id
 *   };
 *   dispatcher.onScore = [](const OPP2::Topic& t, const OPP2::Score& msg) {
 *       // handle score change
 *   };
 *
 *   // In your MQTT client callback:
 *   void mqttCallback(const char* topic, const uint8_t* payload, size_t len) {
 *       OPP2::DispatchResult r = dispatcher.dispatch(topic, (const char*)payload, len);
 *       if (r != OPP2::DispatchResult::OK) {
 *           // handle error
 *       }
 *   }
 *
 * Callbacks:
 *   Each callback is a std::function. Unregistered callbacks are silently
 *   skipped — you only register the message types you care about.
 *   All callbacks receive a const reference to the parsed Topic and the
 *   deserialized message struct. Both are valid only for the duration of
 *   the callback; copy what you need.
 *
 * SystemState mirror:
 *   Optionally provide a SystemState pointer via setSystemState(). When set,
 *   the dispatcher automatically updates the relevant field in the SystemState
 *   on every successfully dispatched retained-topic message. This gives you a
 *   live mirror of all piste state with no extra code in your callbacks.
 *   blade_contact and control are not mirrored (not retained, event-only).
 *
 * Error handling:
 *   dispatch() returns a DispatchResult. OK means the message was parsed and
 *   the callback (if registered) was called. Other values indicate where the
 *   pipeline failed. The caller decides whether to log, ignore, or escalate.
 *
 * Thread safety:
 *   The Dispatcher is not thread-safe. On embedded targets this is not a
 *   concern (single-threaded). On Linux, protect with a mutex if needed.
 *
 * Platform note:
 *   std::function requires a C++11 standard library. This is available on
 *   all supported platforms (ESP32 Arduino, ESP-IDF, desktop Linux).
 *   On very constrained targets without std::function, replace the callback
 *   members with plain function pointers — the dispatch logic is unchanged.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "opp2_types.h"
#include "opp2_topic.h"
#include "opp2_serialize.h"
#include "opp2_deserialize.h"
#include <functional>

namespace OPP2 {

// ============================================================================
// DispatchResult
// ============================================================================

enum class DispatchResult : uint8_t {
    OK,                  ///< Message parsed and callback invoked (or skipped if unregistered)
    UNKNOWN_TOPIC,       ///< Topic could not be parsed as a valid OPP2 topic
    DESERIALIZE_ERROR,   ///< Payload failed to deserialize (see lastDeserializeError())
    NO_CALLBACK,         ///< Message type recognised but no callback registered (not an error per se)
    UNKNOWN_TYPE,        ///< Message type parsed from topic but not handled (future type)
};

// ============================================================================
// Callback types
// ============================================================================

using LightsCallback       = std::function<void(const Topic&, const Lights&)>;
using ClockCallback        = std::function<void(const Topic&, const Clock&)>;
using BladeContactCallback = std::function<void(const Topic&, const BladeContact&)>;
using ScoreCallback        = std::function<void(const Topic&, const Score&)>;
using ConnectionCallback   = std::function<void(const Topic&, const Connection&)>;
using ApparatusStateCallback = std::function<void(const Topic&, const ApparatusStateMsg&)>;
using FencersCallback      = std::function<void(const Topic&, const Fencers&)>;
using MatchCallback        = std::function<void(const Topic&, const Match&)>;
using UW2FCallback         = std::function<void(const Topic&, const UW2F&)>;
using MedicalCallback      = std::function<void(const Topic&, const Medical&)>;
using VideoReviewCallback  = std::function<void(const Topic&, const VideoReview&)>;
using ControlCallback      = std::function<void(const Topic&, const Control&)>;

/// Called for any successfully deserialized message, before the type-specific
/// callback. Useful for logging, timestamping, or updating a UI.
using AnyCallback = std::function<void(const Topic&, MessageType)>;

/// Called on any dispatch error. Useful for diagnostics.
using ErrorCallback = std::function<void(const Topic&, DispatchResult, DeserializeError)>;

// ============================================================================
// Dispatcher
// ============================================================================

class Dispatcher {
public:

    // ── Callback registrations ───────────────────────────────────────────────
    // Set any of these to handle the corresponding message type.
    // Leave unset (nullptr) to silently skip that type.

    LightsCallback         onLights;
    ClockCallback          onClock;
    BladeContactCallback   onBladeContact;
    ScoreCallback          onScore;
    ConnectionCallback     onConnection;
    ApparatusStateCallback onApparatusState;
    FencersCallback        onFencers;
    MatchCallback          onMatch;
    UW2FCallback           onUW2F;
    MedicalCallback        onMedical;
    VideoReviewCallback    onVideoReview;
    ControlCallback        onControl;

    /// Called for every successfully deserialized message (before type callback).
    AnyCallback   onAny;

    /// Called on parse or deserialize errors.
    ErrorCallback onError;

    // ── SystemState mirror ───────────────────────────────────────────────────

    /**
     * Optionally attach a SystemState to be kept up to date automatically.
     * The dispatcher writes to the relevant field on every successful dispatch.
     * Pass nullptr to detach.
     *
     * Note: The SystemState is keyed to a single piste. If you are subscribing
     * to openpiste/# (multiple pistes), maintain one SystemState per piste_id
     * and select the right one in your callbacks rather than using this feature.
     */
    void setSystemState(SystemState* state) { state_ = state; }
    SystemState* systemState() const        { return state_; }

    // ── Last error accessors ─────────────────────────────────────────────────

    DeserializeError lastDeserializeError() const { return last_deser_err_; }
    DispatchResult   lastDispatchResult()   const { return last_result_; }

    // ── Main dispatch entry point ────────────────────────────────────────────

    /**
     * Dispatch an incoming MQTT message.
     *
     * @param mqtt_topic    Null-terminated MQTT topic string.
     * @param payload       Message payload (JSON). Need not be null-terminated;
     *                      length is given by payload_len.
     * @param payload_len   Length of payload in bytes.
     * @return              DispatchResult indicating outcome.
     *
     * The method:
     *   1. Parses the topic into an OPP2::Topic.
     *   2. Deserializes the payload into the appropriate message struct.
     *   3. Updates the SystemState mirror (if attached).
     *   4. Calls onAny (if registered).
     *   5. Calls the type-specific callback (if registered).
     */
    DispatchResult dispatch(const char* mqtt_topic,
                            const char* payload,
                            size_t      payload_len)
    {
        last_deser_err_ = DeserializeError::OK;
        last_result_    = DispatchResult::OK;

        // Step 1: parse topic
        Topic topic;
        // TopicParser::parse returns false for unknown publisher/type,
        // but still populates piste_id. We proceed even on false if
        // piste_id was extracted — we need it for the error callback.
        bool topic_ok = TopicParser::parse(mqtt_topic, topic);

        if (!topic_ok && topic.piste_id[0] == '\0') {
            // Completely unparseable — not an OPP2 topic at all
            last_result_ = DispatchResult::UNKNOWN_TOPIC;
            if (onError) onError(topic, last_result_, DeserializeError::OK);
            return last_result_;
        }

        if (topic.message_type == MessageType::UNKNOWN) {
            // Valid OPP2 prefix and piste_id, but unknown message type
            // (forward compatibility — a future message type we don't know yet)
            last_result_ = DispatchResult::UNKNOWN_TYPE;
            // Not calling onError for unknown types — this is expected behaviour
            return last_result_;
        }

        // Step 2: deserialize and dispatch
        last_result_ = dispatchByType(topic, payload, payload_len);

        if (last_result_ == DispatchResult::DESERIALIZE_ERROR && onError) {
            onError(topic, last_result_, last_deser_err_);
        }

        return last_result_;
    }

    /**
     * Convenience overload for null-terminated payload strings.
     * Uses strlen() to determine payload length.
     */
    DispatchResult dispatch(const char* mqtt_topic, const char* payload) {
        return dispatch(mqtt_topic, payload, payload ? strlen(payload) : 0);
    }

private:

    SystemState*     state_          = nullptr;
    DeserializeError last_deser_err_ = DeserializeError::OK;
    DispatchResult   last_result_    = DispatchResult::OK;

    // ── Per-type dispatch ────────────────────────────────────────────────────

    DispatchResult dispatchByType(const Topic& topic,
                                  const char*  payload,
                                  size_t       len)
    {
        switch (topic.message_type) {

            case MessageType::LIGHTS: {
                Lights msg;
                if (!deser(Deserializer::deserialize(payload, len, msg)))
                    return DispatchResult::DESERIALIZE_ERROR;
                if (state_) state_->lights = msg;
                if (onAny)  onAny(topic, MessageType::LIGHTS);
                if (onLights) { onLights(topic, msg); return DispatchResult::OK; }
                return DispatchResult::NO_CALLBACK;
            }

            case MessageType::CLOCK: {
                Clock msg;
                if (!deser(Deserializer::deserialize(payload, len, msg)))
                    return DispatchResult::DESERIALIZE_ERROR;
                if (state_) state_->clock = msg;
                if (onAny)  onAny(topic, MessageType::CLOCK);
                if (onClock) { onClock(topic, msg); return DispatchResult::OK; }
                return DispatchResult::NO_CALLBACK;
            }

            case MessageType::BLADE_CONTACT: {
                BladeContact msg;
                if (!deser(Deserializer::deserialize(payload, len, msg)))
                    return DispatchResult::DESERIALIZE_ERROR;
                // blade_contact is not retained — not mirrored in SystemState
                if (onAny) onAny(topic, MessageType::BLADE_CONTACT);
                if (onBladeContact) { onBladeContact(topic, msg); return DispatchResult::OK; }
                return DispatchResult::NO_CALLBACK;
            }

            case MessageType::SCORE: {
                Score msg;
                if (!deser(Deserializer::deserialize(payload, len, msg)))
                    return DispatchResult::DESERIALIZE_ERROR;
                if (state_) state_->score = msg;
                if (onAny)  onAny(topic, MessageType::SCORE);
                if (onScore) { onScore(topic, msg); return DispatchResult::OK; }
                return DispatchResult::NO_CALLBACK;
            }

            case MessageType::CONNECTION: {
                Connection msg;
                if (!deser(Deserializer::deserialize(payload, len, msg)))
                    return DispatchResult::DESERIALIZE_ERROR;
                if (state_) state_->connection = msg;
                if (onAny)  onAny(topic, MessageType::CONNECTION);
                if (onConnection) { onConnection(topic, msg); return DispatchResult::OK; }
                return DispatchResult::NO_CALLBACK;
            }

            case MessageType::APPARATUS_STATE: {
                ApparatusStateMsg msg;
                if (!deser(Deserializer::deserialize(payload, len, msg)))
                    return DispatchResult::DESERIALIZE_ERROR;
                if (state_) state_->apparatus_state = msg;
                if (onAny)  onAny(topic, MessageType::APPARATUS_STATE);
                if (onApparatusState) { onApparatusState(topic, msg); return DispatchResult::OK; }
                return DispatchResult::NO_CALLBACK;
            }

            case MessageType::FENCERS: {
                Fencers msg;
                if (!deser(Deserializer::deserialize(payload, len, msg)))
                    return DispatchResult::DESERIALIZE_ERROR;
                if (state_) state_->fencers = msg;
                if (onAny)  onAny(topic, MessageType::FENCERS);
                if (onFencers) { onFencers(topic, msg); return DispatchResult::OK; }
                return DispatchResult::NO_CALLBACK;
            }

            case MessageType::MATCH: {
                Match msg;
                if (!deser(Deserializer::deserialize(payload, len, msg)))
                    return DispatchResult::DESERIALIZE_ERROR;
                if (state_) state_->match = msg;
                if (onAny)  onAny(topic, MessageType::MATCH);
                if (onMatch) { onMatch(topic, msg); return DispatchResult::OK; }
                return DispatchResult::NO_CALLBACK;
            }

            case MessageType::UW2F: {
                UW2F msg;
                if (!deser(Deserializer::deserialize(payload, len, msg)))
                    return DispatchResult::DESERIALIZE_ERROR;
                if (state_) state_->uw2f = msg;
                if (onAny)  onAny(topic, MessageType::UW2F);
                if (onUW2F) { onUW2F(topic, msg); return DispatchResult::OK; }
                return DispatchResult::NO_CALLBACK;
            }

            case MessageType::MEDICAL: {
                Medical msg;
                if (!deser(Deserializer::deserialize(payload, len, msg)))
                    return DispatchResult::DESERIALIZE_ERROR;
                if (state_) state_->medical = msg;
                if (onAny)  onAny(topic, MessageType::MEDICAL);
                if (onMedical) { onMedical(topic, msg); return DispatchResult::OK; }
                return DispatchResult::NO_CALLBACK;
            }

            case MessageType::VIDEO_REVIEW: {
                VideoReview msg;
                if (!deser(Deserializer::deserialize(payload, len, msg)))
                    return DispatchResult::DESERIALIZE_ERROR;
                if (state_) state_->video_review = msg;
                if (onAny)  onAny(topic, MessageType::VIDEO_REVIEW);
                if (onVideoReview) { onVideoReview(topic, msg); return DispatchResult::OK; }
                return DispatchResult::NO_CALLBACK;
            }

            case MessageType::CONTROL: {
                Control msg;
                if (!deser(Deserializer::deserialize(payload, len, msg)))
                    return DispatchResult::DESERIALIZE_ERROR;
                // control is not retained — not mirrored in SystemState
                if (onAny) onAny(topic, MessageType::CONTROL);
                if (onControl) { onControl(topic, msg); return DispatchResult::OK; }
                return DispatchResult::NO_CALLBACK;
            }

            default:
                return DispatchResult::UNKNOWN_TYPE;
        }
    }

    /// Store the DeserializeError and return true if OK.
    bool deser(DeserializeError err) {
        last_deser_err_ = err;
        return err == DeserializeError::OK;
    }
};

}  // namespace OPP2
