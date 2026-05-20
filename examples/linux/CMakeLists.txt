cmake_minimum_required(VERSION 3.16)
project(opp2-linux-examples CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── ArduinoJson ───────────────────────────────────────────────────────────────
# Fetched automatically — no manual download needed.

include(FetchContent)
FetchContent_Declare(
    arduinojson
    GIT_REPOSITORY https://github.com/bblanchon/ArduinoJson.git
    GIT_TAG        v7.2.0
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(arduinojson)

# ── OPP2 library ─────────────────────────────────────────────────────────────
# Points to the src/ directory two levels up from examples/linux/
# Adjust this path if you move the examples elsewhere.

set(OPP2_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../src")

# ── mosquitto ─────────────────────────────────────────────────────────────────
# Install with: sudo apt install libmosquitto-dev

find_library(MOSQUITTO_LIB mosquitto REQUIRED)
find_path(MOSQUITTO_INCLUDE mosquitto.h REQUIRED)

# ── Shared interface target ───────────────────────────────────────────────────
# Both examples link against this to get ArduinoJson, OPP2, and mosquitto.

add_library(opp2_deps INTERFACE)
target_include_directories(opp2_deps INTERFACE
    ${OPP2_INCLUDE_DIR}
    ${MOSQUITTO_INCLUDE}
)
target_link_libraries(opp2_deps INTERFACE
    ArduinoJson
    ${MOSQUITTO_LIB}
)
target_compile_options(opp2_deps INTERFACE -Wall -Wextra)

# ── Examples ──────────────────────────────────────────────────────────────────

add_subdirectory(apparatus_basic)
add_subdirectory(subscriber_basic)
