# OPP2 Library — Repository Layout

```
opp2-library/
│
├── src/                        # Library source — all header files
│   ├── opp2.h                  # Umbrella include (use this in your project)
│   ├── opp2_types.h            # Structs, enums, Timestamp, SystemState
│   ├── opp2_topic.h            # MQTT topic parser and builder
│   ├── opp2_time.h             # Time string formatting and parsing
│   ├── opp2_serialize.h        # struct → JSON  (requires ArduinoJson)
│   ├── opp2_deserialize.h      # JSON → struct  (requires ArduinoJson)
│   └── opp2_dispatcher.h       # Message router, callbacks, SystemState mirror
│
├── test/                       # Catch2 unit tests
│   ├── test_topic/
│   │   └── test_topic.cpp
│   ├── test_time/
│   │   └── test_time.cpp
│   ├── test_serialize/
│   │   └── test_serialize.cpp
│   ├── test_deserialize/
│   │   └── test_deserialize.cpp
│   └── test_dispatcher/
│       └── test_dispatcher.cpp
│
├── examples/                   # Arduino/ESP-IDF usage examples
│   ├── apparatus_basic/        # Minimal scoring apparatus publisher
│   │   └── apparatus_basic.ino
│   └── subscriber_basic/       # Minimal piste monitor subscriber
│       └── subscriber_basic.ino
│
├── tools/
│   └── conformance/            # Linux conformance checker (connects to Mosquitto)
│       ├── CMakeLists.txt
│       └── main.cpp
│
├── platformio.ini              # PlatformIO project config (native + ESP32)
├── library.json                # PlatformIO/Arduino library manifest
├── LICENSE
└── README.md
```

## Quick start

### PlatformIO project

Add to your `platformio.ini`:

```ini
lib_deps =
    https://github.com/OpenPiste/opp2-library
    bblanchon/ArduinoJson @ ^7.0.0
```

Then in your code:

```cpp
#include <opp2.h>
```

### Run tests on desktop

```bash
git clone https://github.com/OpenPiste/opp2-library
cd opp2-library
pio test -e native
```

### Run tests on ESP32

```bash
pio test -e esp32
```
