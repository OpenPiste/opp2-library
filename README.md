# OPP2 Library

**OpenPiste Protocol Level 2 — C++ reference implementation**

[![PlatformIO](https://img.shields.io/badge/PlatformIO-compatible-orange)](https://platformio.org)
[![Arduino](https://img.shields.io/badge/Arduino-compatible-blue)](https://www.arduino.cc)
[![License: MIT](https://img.shields.io/badge/License-MIT-green)](LICENSE)
[![Protocol: OPP2 draft v1.0](https://img.shields.io/badge/Protocol-OPP2%20draft%20v1.0-lightgrey)](https://openpiste.org)

A C++ library for building fencing electronics that communicate using the [OpenPiste Protocol Level 2 (OPP2)](https://openpiste.org) — a modern, open MQTT/JSON communication standard for scoring apparatus, displays, remote controls, and competition management software.

---

## What is OPP2?

Most commercial fencing scoring equipment communicates using EFP1.1 (commonly called Cyrano), a UDP-based protocol that has been in use since 2008. It is error-prone, difficult to extend, and locks clubs and federations into proprietary vendor ecosystems.

OPP2 replaces it with MQTT and JSON — the same communication stack used across the IoT industry, with libraries available for every platform from microcontrollers to cloud services. The result is reliable message delivery, multiple simultaneous subscribers, retained state for late-joining clients, and millisecond-precision timestamps for video synchronisation.

OPP2 covers the full lifecycle of a fencing bout:

| Message | Publisher | Description |
|---|---|---|
| `lights` | apparatus | On-target and off-target light state |
| `clock` | apparatus | Stopwatch — running, stopped, time value |
| `blade_contact` | apparatus | Raw blade contact event with timestamp |
| `score` | apparatus / software | Scores, cards, priority, fencer status |
| `connection` | apparatus | Online/offline status, firmware version |
| `state` | apparatus | Apparatus state: fencing, halt, pause, waiting, ending |
| `fencers` | software | Fencer, coach, referee, and video official identities |
| `match` | software | Weapon, competition, phase, round metadata |
| `uw2f` | apparatus | Passivity timer and P-card state |
| `medical` | apparatus | Medical timeout with countdown timer |
| `video_review` | apparatus / software | Review requests, decisions, and call history |
| `control` | apparatus / software / remote | Commands between devices |

The full protocol specification is at [openpiste.org](https://openpiste.org) and in the [OpenPiste repository](https://github.com/OpenPiste).

---

## What this library provides

- **Typed structs** for all 12 OPP2 message types — no raw JSON parsing in your application code
- **Serializer** — populate a struct, get a JSON string ready to publish
- **Deserializer** — receive a JSON payload, get a populated struct
- **Topic parser and builder** — parse and construct `openpiste/{piste_id}/{publisher}/{message_type}` strings
- **Dispatcher** — route incoming MQTT messages to typed callbacks with a single `dispatch()` call
- **SystemState** — optional live mirror of all retained piste state, updated automatically on every received message
- **Forward compatibility** — unknown enum values deserialize to `UNKNOWN` rather than causing errors; new protocol versions are accepted gracefully

The library is designed for both directions: apparatus firmware that publishes, and subscriber implementations (displays, monitors, video tools, competition software) that consume.

---

## Platform support

| Platform | Framework | Status |
|---|---|---|
| ESP32 | Arduino | Primary target |
| ESP32 | ESP-IDF | Supported |
| Linux / macOS / Windows | Native (PlatformIO) | Supported — used for unit testing |
| Any C++11 target | — | Header-only, no platform dependencies |

The library has one external dependency: [ArduinoJson](https://arduinojson.org) (v6 or v7), which itself supports all the above platforms.

---

## Installation

### PlatformIO

Add to your `platformio.ini`:

```ini
lib_deps =
    https://github.com/OpenPiste/opp2-library
    bblanchon/ArduinoJson @ ^7.0.0
```

### Arduino IDE

1. Download the latest release zip from the [Releases](https://github.com/OpenPiste/opp2-library/releases) page
2. In Arduino IDE: **Sketch → Include Library → Add .ZIP Library**
3. Install [ArduinoJson](https://arduinojson.org) via the Library Manager

### Manual

Copy the `src/` directory into your project and include `opp2.h`.

---

## Quick start

### Publishing (apparatus side)

```cpp
#include <opp2.h>

// Build a lights message
OPP2::Lights msg;
msg.seq             = nextSeq();
msg.ts              = OPP2::Timestamp::fromEpochMs(epochMillis());
msg.left.on_target  = true;   // red light on
msg.left.white      = false;
msg.right.on_target = false;
msg.right.white     = false;

// Serialize to JSON
char payload[128];
OPP2::Serializer::serialize(msg, payload, sizeof(payload));
// payload == {"protocol":"OPP2","version":"1.0","seq":1,"ts":...,"right":{"green":false,"white":false},"left":{"red":true,"white":false}}

// Build the MQTT topic
char topic[64];
OPP2::TopicParser::buildFrom("17", OPP2::Publisher::APPARATUS,
                              OPP2::MessageType::LIGHTS, topic, sizeof(topic));
// topic == "openpiste/17/apparatus/lights"

// Publish using your MQTT client of choice
mqttClient.publish(topic, payload, /*retained=*/true);
```

### Subscribing (display, monitor, software)

```cpp
#include <opp2.h>

OPP2::Dispatcher dispatcher;
OPP2::SystemState pisteState;  // live mirror of all retained topics

// Register callbacks for the message types you care about
dispatcher.onLights = [](const OPP2::Topic& t, const OPP2::Lights& msg) {
    Serial.printf("Piste %s: left=%s right=%s\n",
        t.piste_id,
        msg.left.on_target  ? "ON" : "off",
        msg.right.on_target ? "ON" : "off");
};

dispatcher.onScore = [](const OPP2::Topic& t, const OPP2::Score& msg) {
    Serial.printf("Score: %d - %d\n", msg.left.score, msg.right.score);
};

// Attach SystemState to keep a live mirror automatically
dispatcher.setSystemState(&pisteState);

// In your MQTT callback:
void mqttCallback(const char* topic, uint8_t* payload, unsigned int length) {
    dispatcher.dispatch(topic, (const char*)payload, length);
}
```

### Last Will and Testament (apparatus connection)

```cpp
// Set up LWT before connecting — the broker publishes this automatically
// if your device disconnects unexpectedly
char lwtTopic[64];
OPP2::TopicParser::buildLwtTopic("17", lwtTopic, sizeof(lwtTopic));
// lwtTopic == "openpiste/17/apparatus/connection"

const char* lwtPayload = "{\"online\":false}";

mqttClient.setWill(lwtTopic, lwtPayload, /*retained=*/true, /*qos=*/1);
```

---

## Network setup

OPP2 is designed to run on a local network — a club network, a competition network, or a self-contained access point. No internet connection is required.

**Recommended broker:** [Mosquitto](https://mosquitto.org) — lightweight, open source, runs on a Raspberry Pi or laptop.

**Default broker hostname:** `openpiste.local` (mDNS) — all OPP2-compatible devices use this as their default broker address, so no IP configuration is needed on a local network.

**NTP:** For video synchronisation, run a local NTP server on the same host as the broker. All devices synchronise to UTC from it, making timestamps comparable across apparatus, displays, and video tools.

---

## Topic structure

All OPP2 topics follow this pattern:

```
openpiste/{piste_id}/{publisher}/{message_type}
```

The `piste_id` can be a number, name, or colour (`17`, `podium`, `rouge`, `vert`). The publisher segment identifies who sent the message (`apparatus`, `software`, `remote`). Both are authoritative — they are not duplicated in the payload.

Useful subscription patterns:

```
openpiste/#                    all messages from all pistes
openpiste/17/#                 all messages from piste 17
openpiste/+/apparatus/lights   lights from all pistes
openpiste/+/+/control          control events from all publishers, all pistes
```

---

## Architecture

The library is header-only and structured in layers:

```
opp2.h                   ← single include (pulls in everything below)
├── opp2_types.h         ← structs, enums, Timestamp, SystemState
├── opp2_topic.h         ← topic parser and builder
├── opp2_time.h          ← time string formatting (M:SS, M:SS.cc)
├── opp2_serialize.h     ← struct → JSON   (requires ArduinoJson)
├── opp2_deserialize.h   ← JSON → struct   (requires ArduinoJson)
└── opp2_dispatcher.h    ← topic routing, callbacks, SystemState mirror
```

Each layer can be included independently. If you only need topic parsing and struct definitions (no JSON), include `opp2_types.h` and `opp2_topic.h` directly — no ArduinoJson dependency.

---

## Running the tests

```bash
git clone https://github.com/OpenPiste/opp2-library
cd opp2-library
pio test -e native
```

PlatformIO downloads all dependencies automatically on first run. The test suite covers all 12 message types with serialization round-trips, deserialization error paths, forward compatibility, LWT handling, dispatcher routing, and SystemState mirroring.

To run a single test suite:

```bash
pio test -e native -f test_topic
pio test -e native -f test_serialize
```

---

## Relation to the OpenPiste platform

This library is one component of the [OpenPiste](https://openpiste.org) open source fencing electronics platform — a complete alternative to expensive proprietary scoring equipment, covering scoring hardware, weapon and wire testing, remote controls, piste monitoring, and electronics designs.

The platform is developed by Piet Wauters, member of the FIE SEMI Commission (technology commission of the International Fencing Federation) and EFC SEMI Commission, and field-tested at international competitions including the European Fencing Championships.

- Platform website: [openpiste.org](https://openpiste.org)
- Platform organisation: [github.com/OpenPiste](https://github.com/OpenPiste)
- Protocol specification: [openpiste.org/docs/platform](https://openpiste.org/docs/platform)

---

## Contributing

Contributions are welcome — bug reports, protocol feedback, implementations in other languages, and hardware integrations.

Please read [CONTRIBUTING.md](CONTRIBUTING.md) before opening a pull request. Note that contributions require signing a Contributor Licence Agreement to allow the project to be relicensed or used commercially in the future, while you retain copyright over your own contributions.

---

## Licence

MIT — see [LICENSE](LICENSE).

The protocol specification (OPP2) is separately released under MIT licence. Any implementation — in any language, by any party — may use it without restriction.
