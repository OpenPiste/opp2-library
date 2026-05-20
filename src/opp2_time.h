/**
 * @file opp2_time.h
 * @brief OPP2 time formatting utilities.
 *
 * Converts millisecond values to the formatted time strings required by
 * the OPP2 protocol (Section 20.1):
 *
 *   "M:SS"     — when time >= 10 seconds
 *   "M:SS.cc"  — when time < 10 seconds (hundredths mandatory)
 *
 * This convention is inherited from EFP1.1. It applies to:
 *   - Clock.time       (countdown stopwatch)
 *   - UW2F.time        (passivity timer, counts up)
 *   - Medical.remaining (countdown timer)
 *
 * All functions are pure: no side effects, no allocation, no dependencies
 * beyond stdint.h, stdio.h, and string.h.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdint.h>
#include <stdio.h>
#include <string.h>

namespace OPP2 {

/**
 * TimeFormat — time string formatting and parsing utilities.
 *
 * All methods are static.
 *
 * Buffer requirement: callers must provide at least 12 bytes.
 * The longest possible output is "99:59.99" (8 chars + null = 9 bytes).
 * 12 bytes provides comfortable headroom.
 *
 * Behaviour on out-of-range input:
 *   - time_ms values representing more than 99 minutes and 59 seconds will
 *     produce strings with a minutes value > 99. The protocol does not define
 *     a maximum bout duration, so this is intentionally unclamped.
 *   - Values are always treated as unsigned. Negative values passed via
 *     cast will produce large positive numbers; callers should validate.
 */
class TimeFormat {
public:

    /**
     * Format milliseconds as an OPP2 time string.
     *
     * Produces "M:SS" when time_ms >= 10000 (10 seconds).
     * Produces "M:SS.cc" when time_ms < 10000 (hundredths mandatory).
     *
     * @param time_ms   Time value in milliseconds.
     * @param buf       Output buffer, must be at least 12 bytes.
     * @param buf_size  Size of output buffer.
     * @return          true on success; false if buf is null or too small.
     *
     * Examples:
     *   formatMs(89250, buf, 12)  → "1:29"      (>= 10 s, no hundredths)
     *   formatMs(9250,  buf, 12)  → "0:09.25"   (< 10 s, hundredths)
     *   formatMs(500,   buf, 12)  → "0:00.50"   (< 10 s, hundredths)
     *   formatMs(0,     buf, 12)  → "0:00.00"   (< 10 s, hundredths)
     *   formatMs(60000, buf, 12)  → "1:00"      (exactly 1 min, no hundredths)
     */
    static bool formatMs(uint32_t time_ms, char* buf, size_t buf_size) {
        if (!buf || buf_size < 9) return false;

        const uint32_t total_cs  = time_ms / 10;         // centiseconds
        const uint32_t total_s   = total_cs / 100;       // whole seconds
        const uint32_t cs_part   = total_cs % 100;       // centiseconds remainder
        const uint32_t minutes   = total_s / 60;
        const uint32_t seconds   = total_s % 60;

        int written;
        if (time_ms < 10000) {
            // Below 10 seconds: include hundredths (centiseconds)
            written = snprintf(buf, buf_size, "%u:%02u.%02u",
                               minutes, seconds, cs_part);
        } else {
            // 10 seconds and above: whole seconds only
            written = snprintf(buf, buf_size, "%u:%02u",
                               minutes, seconds);
        }

        return (written > 0 && static_cast<size_t>(written) < buf_size);
    }

    /**
     * Parse an OPP2 time string back to milliseconds.
     *
     * Accepts both "M:SS" and "M:SS.cc" formats.
     * The colon and dot are the mandatory delimiters.
     *
     * @param str       Null-terminated time string.
     * @param out_ms    Output: parsed time in milliseconds.
     * @return          true on success; false if the string is malformed.
     *
     * This is provided for completeness and for use in the deserializer and
     * test harness. The serializer always generates time strings from time_ms,
     * so parsing is only needed when a received message contains a time string
     * but no corresponding _ms field (allowed by UW2F and Medical specs).
     */
    static bool parseToMs(const char* str, uint32_t& out_ms) {
        if (!str || !*str) return false;

        uint32_t minutes = 0, seconds = 0, centiseconds = 0;
        bool has_centiseconds = false;

        // Find the colon
        const char* colon = strchr(str, ':');
        if (!colon) return false;

        // Parse minutes
        char min_buf[8] = {};
        size_t min_len = static_cast<size_t>(colon - str);
        if (min_len == 0 || min_len >= sizeof(min_buf)) return false;
        memcpy(min_buf, str, min_len);
        if (!parseUint(min_buf, minutes)) return false;

        // Parse seconds (and optional centiseconds)
        const char* sec_start = colon + 1;
        const char* dot = strchr(sec_start, '.');

        if (dot) {
            // "SS.cc" format
            has_centiseconds = true;
            char sec_buf[4] = {};
            size_t sec_len = static_cast<size_t>(dot - sec_start);
            if (sec_len == 0 || sec_len > 2) return false;
            memcpy(sec_buf, sec_start, sec_len);
            if (!parseUint(sec_buf, seconds)) return false;
            if (seconds > 59) return false;

            const char* cs_start = dot + 1;
            size_t cs_len = strlen(cs_start);
            if (cs_len == 0 || cs_len > 2) return false;
            char cs_buf[4] = {};
            memcpy(cs_buf, cs_start, cs_len);
            // Normalise single-digit centiseconds: "5" means 50cs, not 5cs
            if (cs_len == 1) { cs_buf[1] = '0'; cs_buf[2] = '\0'; }
            if (!parseUint(cs_buf, centiseconds)) return false;
            if (centiseconds > 99) return false;
        } else {
            // "SS" format
            char sec_buf[4] = {};
            size_t sec_len = strlen(sec_start);
            if (sec_len == 0 || sec_len > 2) return false;
            memcpy(sec_buf, sec_start, sec_len);
            if (!parseUint(sec_buf, seconds)) return false;
            if (seconds > 59) return false;
        }

        out_ms = (minutes * 60 + seconds) * 1000
               + (has_centiseconds ? centiseconds * 10 : 0);
        return true;

        (void)has_centiseconds; // suppress potential warning
    }

    /**
     * Populate a time string field in a message struct from its _ms counterpart.
     * This is the function called by the serializer for Clock, UW2F, and Medical.
     *
     * @param time_ms   Source millisecond value.
     * @param buf       Destination char array (minimum 12 bytes).
     * @param buf_size  Size of destination array.
     */
    static void populateTimeStr(uint32_t time_ms, char* buf, size_t buf_size) {
        if (!formatMs(time_ms, buf, buf_size)) {
            // Fallback: write "0:00" — should never happen with a 12-byte buffer
            strncpy(buf, "0:00", buf_size - 1);
            buf[buf_size - 1] = '\0';
        }
    }

private:

    /** Parse a null-terminated string as an unsigned integer. */
    static bool parseUint(const char* str, uint32_t& out) {
        if (!str || !*str) return false;
        out = 0;
        for (const char* p = str; *p; ++p) {
            if (*p < '0' || *p > '9') return false;
            out = out * 10 + static_cast<uint32_t>(*p - '0');
        }
        return true;
    }
};

}  // namespace OPP2
