/**
 * @file opp2_topic.h
 * @brief OPP2 MQTT topic parsing and building.
 *
 * Topic pattern (Section 5):
 *   openpiste/{piste_id}/{publisher}/{message_type}
 *
 * This file is self-contained: it depends only on opp2_types.h and
 * standard C string functions. No dynamic allocation.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "opp2_types.h"
#include <string.h>
#include <stdio.h>

namespace OPP2 {

// ============================================================================
// String ↔ enum conversion tables
// ============================================================================

// These are intentionally file-scope static structs rather than switch
// statements so that the serializer and deserializer share the same mapping
// and so that adding a new value in one place automatically covers both.

struct PublisherMap  { const char* str; Publisher   value; };
struct MessageTypeMap{ const char* str; MessageType value; };

static const PublisherMap PUBLISHER_STRINGS[] = {
    { "apparatus", Publisher::APPARATUS },
    { "software",  Publisher::SOFTWARE  },
    { "remote",    Publisher::REMOTE    },
};
static const size_t PUBLISHER_STRINGS_COUNT =
    sizeof(PUBLISHER_STRINGS) / sizeof(PUBLISHER_STRINGS[0]);

static const MessageTypeMap MESSAGE_TYPE_STRINGS[] = {
    { "lights",        MessageType::LIGHTS         },
    { "clock",         MessageType::CLOCK          },
    { "blade_contact", MessageType::BLADE_CONTACT  },
    { "score",         MessageType::SCORE          },
    { "connection",    MessageType::CONNECTION      },
    { "state",         MessageType::APPARATUS_STATE },  // wire name is "state"
    { "fencers",       MessageType::FENCERS        },
    { "match",         MessageType::MATCH          },
    { "uw2f",          MessageType::UW2F           },
    { "medical",       MessageType::MEDICAL        },
    { "video_review",  MessageType::VIDEO_REVIEW   },
    { "control",       MessageType::CONTROL        },
};
static const size_t MESSAGE_TYPE_STRINGS_COUNT =
    sizeof(MESSAGE_TYPE_STRINGS) / sizeof(MESSAGE_TYPE_STRINGS[0]);

// ============================================================================
// Lookup helpers
// ============================================================================

/** Convert a publisher string to its enum value. Returns UNKNOWN if not found. */
inline Publisher publisherFromString(const char* str) {
    for (size_t i = 0; i < PUBLISHER_STRINGS_COUNT; ++i) {
        if (strcmp(str, PUBLISHER_STRINGS[i].str) == 0)
            return PUBLISHER_STRINGS[i].value;
    }
    return Publisher::UNKNOWN;
}

/** Convert a Publisher enum to its wire string. Returns nullptr if UNKNOWN. */
inline const char* publisherToString(Publisher p) {
    for (size_t i = 0; i < PUBLISHER_STRINGS_COUNT; ++i) {
        if (PUBLISHER_STRINGS[i].value == p)
            return PUBLISHER_STRINGS[i].str;
    }
    return nullptr;
}

/** Convert a message type string to its enum value. Returns UNKNOWN if not found. */
inline MessageType messageTypeFromString(const char* str) {
    for (size_t i = 0; i < MESSAGE_TYPE_STRINGS_COUNT; ++i) {
        if (strcmp(str, MESSAGE_TYPE_STRINGS[i].str) == 0)
            return MESSAGE_TYPE_STRINGS[i].value;
    }
    return MessageType::UNKNOWN;
}

/** Convert a MessageType enum to its wire string. Returns nullptr if UNKNOWN. */
inline const char* messageTypeToString(MessageType t) {
    for (size_t i = 0; i < MESSAGE_TYPE_STRINGS_COUNT; ++i) {
        if (MESSAGE_TYPE_STRINGS[i].value == t)
            return MESSAGE_TYPE_STRINGS[i].str;
    }
    return nullptr;
}

// ============================================================================
// Topic class
// ============================================================================

/**
 * TopicParser — parses and builds OPP2 MQTT topic strings.
 *
 * All methods are static. No state, no allocation.
 *
 * Example:
 *   OPP2::Topic t;
 *   if (OPP2::TopicParser::parse("openpiste/17/apparatus/lights", t)) {
 *       // t.piste_id    == "17"
 *       // t.publisher   == Publisher::APPARATUS
 *       // t.message_type == MessageType::LIGHTS
 *   }
 *
 *   char buf[64];
 *   OPP2::TopicParser::build(t, buf, sizeof(buf));
 *   // buf == "openpiste/17/apparatus/lights"
 */
class TopicParser {
public:

    /**
     * Parse an MQTT topic string into an OPP2::Topic.
     *
     * @param topic     Null-terminated topic string.
     * @param out       Output Topic struct. Fields set to UNKNOWN on failure.
     * @return          true if the topic is a valid OPP2 topic with all four
     *                  segments recognised; false otherwise.
     *
     * Partial success: if the prefix and piste_id are valid but the publisher
     * or message_type segment is unrecognised, out.publisher or
     * out.message_type will be UNKNOWN and the function returns false.
     * The caller may still inspect piste_id in that case.
     *
     * The topic string is not modified (a working copy is made on the stack).
     */
    static bool parse(const char* topic, Topic& out) {
        // Zero the output
        memset(out.piste_id, 0, sizeof(out.piste_id));
        out.publisher    = Publisher::UNKNOWN;
        out.message_type = MessageType::UNKNOWN;

        if (!topic || !*topic) return false;

        // Work on a stack copy so we can tokenise with null bytes
        char buf[128];
        size_t len = strlen(topic);
        if (len >= sizeof(buf)) return false;  // topic too long
        memcpy(buf, topic, len + 1);

        // Expected: "openpiste/{piste_id}/{publisher}/{message_type}"
        // Split on '/'
        const char* segments[4] = { nullptr, nullptr, nullptr, nullptr };
        int seg = 0;
        segments[seg++] = buf;
        for (char* p = buf; *p && seg < 4; ++p) {
            if (*p == '/') {
                *p = '\0';
                segments[seg++] = p + 1;
            }
        }

        // Must have exactly 4 segments
        if (seg != 4) return false;

        // Segment 0: fixed prefix
        if (strcmp(segments[0], TOPIC_PREFIX) != 0) return false;

        // Segment 1: piste_id — copy, check length
        size_t id_len = strlen(segments[1]);
        if (id_len == 0 || id_len >= PISTE_ID_MAX) return false;
        memcpy(out.piste_id, segments[1], id_len + 1);

        // Segment 2: publisher
        out.publisher = publisherFromString(segments[2]);

        // Segment 3: message_type
        out.message_type = messageTypeFromString(segments[3]);

        // Return true only if both enums resolved
        return (out.publisher    != Publisher::UNKNOWN &&
                out.message_type != MessageType::UNKNOWN);
    }

    /**
     * Build an OPP2 MQTT topic string from a Topic struct.
     *
     * @param topic     Topic to serialise.
     * @param buf       Output buffer.
     * @param buf_size  Size of output buffer in bytes.
     * @return          true if the topic was written successfully;
     *                  false if the buffer is too small, piste_id is empty,
     *                  or publisher/message_type is UNKNOWN.
     */
    static bool build(const Topic& topic, char* buf, size_t buf_size) {
        if (!buf || buf_size == 0) return false;
        if (topic.piste_id[0] == '\0') return false;

        const char* pub = publisherToString(topic.publisher);
        const char* msg = messageTypeToString(topic.message_type);
        if (!pub || !msg) return false;

        int written = snprintf(buf, buf_size, "%s/%s/%s/%s",
                               TOPIC_PREFIX,
                               topic.piste_id,
                               pub,
                               msg);

        return (written > 0 && static_cast<size_t>(written) < buf_size);
    }

    /**
     * Convenience: build a topic string directly from components.
     *
     * @param piste_id      Piste identifier string.
     * @param publisher     Publisher enum value.
     * @param message_type  Message type enum value.
     * @param buf           Output buffer.
     * @param buf_size      Size of output buffer in bytes.
     * @return              true on success.
     */
    static bool buildFrom(const char*  piste_id,
                          Publisher    publisher,
                          MessageType  message_type,
                          char*        buf,
                          size_t       buf_size)
    {
        Topic t;
        size_t id_len = piste_id ? strlen(piste_id) : 0;
        if (id_len == 0 || id_len >= PISTE_ID_MAX) return false;
        memcpy(t.piste_id, piste_id, id_len + 1);
        t.publisher    = publisher;
        t.message_type = message_type;
        return build(t, buf, buf_size);
    }

    /**
     * Build the LWT topic for a given piste (Section 4.6).
     * Always: openpiste/{piste_id}/apparatus/connection
     */
    static bool buildLwtTopic(const char* piste_id, char* buf, size_t buf_size) {
        return buildFrom(piste_id,
                         Publisher::APPARATUS,
                         MessageType::CONNECTION,
                         buf, buf_size);
    }
};

}  // namespace OPP2
