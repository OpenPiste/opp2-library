/**
 * @file opp2.h
 * @brief OpenPiste Protocol Level 2 (OPP2) — single include header.
 *
 * Include this file in your project to get the complete OPP2 library:
 *
 *   #include "opp2.h"
 *
 * This is equivalent to including all five component headers individually.
 * If you only need a subset (e.g. types and topic parsing without JSON),
 * include the component headers directly instead.
 *
 * Component headers and their dependencies:
 *
 *   opp2_types.h       — structs, enums, Timestamp, SystemState
 *                        depends on: stdint.h, stdbool.h, string.h
 *
 *   opp2_topic.h       — MQTT topic parser and builder
 *                        depends on: opp2_types.h, string.h, stdio.h
 *
 *   opp2_time.h        — time string formatting and parsing
 *                        depends on: stdint.h, stdio.h, string.h
 *
 *   opp2_serialize.h   — struct → JSON (requires ArduinoJson)
 *                        depends on: opp2_types.h, opp2_topic.h, opp2_time.h
 *
 *   opp2_deserialize.h — JSON → struct (requires ArduinoJson)
 *                        depends on: opp2_types.h, opp2_topic.h, opp2_time.h
 *
 *   opp2_dispatcher.h  — MQTT message router with callbacks and SystemState mirror
 *                        depends on: all above
 *
 * External dependency:
 *   ArduinoJson v6 or v7 must be available on the include path.
 *   PlatformIO:  lib_deps = bblanchon/ArduinoJson @ ^7.0.0
 *   Arduino IDE: install "ArduinoJson" by Benoit Blanchon via Library Manager
 *   CMake/Linux: see https://github.com/bblanchon/ArduinoJson
 *
 * Spec reference: OpenPiste Protocol Level 2, draft v1.0, May 2026.
 * Repository:     https://github.com/OpenPiste
 * Website:        https://openpiste.org
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "opp2_types.h"
#include "opp2_topic.h"
#include "opp2_time.h"
#include "opp2_serialize.h"
#include "opp2_deserialize.h"
#include "opp2_dispatcher.h"
