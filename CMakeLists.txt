cmake_minimum_required(VERSION 3.29)
project(drone-ws-target-tracking-asio 
    VERSION 1.0.0
    LANGUAGES CXX
    DESCRIPTION "TLS WebSocket Client/Server with C++23 Coroutines"
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Compiler warnings
add_compile_options(
    -Wall -Wextra -Wpedantic
    -Wnon-virtual-dtor
    -Wold-style-cast
    -Wcast-align
    -Wunused
    -Woverloaded-virtual
    -Wconversion
    -Wsign-conversion
    -Wnull-dereference
    -Wdouble-promotion
    -Wformat=2
)

include(FetchContent)

#
# ============================================================================
# Boost (Beast + Asio - NO Cobalt)
# ============================================================================
#

FetchContent_Declare(
    boost
    GIT_REPOSITORY https://github.com/boostorg/boost.git
    GIT_TAG boost-1.85.0
    GIT_SHALLOW TRUE
    GIT_PROGRESS TRUE
)

set(BOOST_ENABLE_CMAKE ON CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(boost)

#
# ============================================================================
# nlohmann_json (header-only)
# ============================================================================
#

FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

#
# ============================================================================
# fmt
# ============================================================================
#

FetchContent_Declare(
    fmt
    GIT_REPOSITORY https://github.com/fmtlib/fmt.git
    GIT_TAG 10.2.1
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(fmt)

#
# ============================================================================
# OpenSSL (system)
# ============================================================================
#

find_package(OpenSSL REQUIRED)

message(STATUS "OpenSSL include: ${OPENSSL_INCLUDE_DIR}")
message(STATUS "OpenSSL libs: ${OPENSSL_LIBRARIES}")

#
# ============================================================================
# Subdirectories
# ============================================================================
#

add_subdirectory(svckit)
add_subdirectory(protocol)
add_subdirectory(ws-server)
add_subdirectory(ws-client)

#
# ============================================================================
# Root Orchestrator
# ============================================================================
#

add_executable(drone-ws-orchestrator
    src/main.cpp
    ws-server/src/ws_server.cpp
    ws-client/src/ws_client.cpp
)

target_include_directories(drone-ws-orchestrator PRIVATE
    ${CMAKE_SOURCE_DIR}/ws-server/include
    ${CMAKE_SOURCE_DIR}/ws-client/include
)

target_link_libraries(drone-ws-orchestrator PRIVATE
    Boost::beast
    Boost::asio
    protocol-lib
    svckit
    fmt::fmt
    OpenSSL::SSL
    OpenSSL::Crypto
)

if(UNIX)
    target_link_libraries(drone-ws-orchestrator PRIVATE pthread)
endif()
