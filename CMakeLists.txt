cmake_minimum_required(VERSION 3.22)
project(RaftKV VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Compiler warnings
add_compile_options(-Wall -Wextra -Werror)

# Sanitizer support
set(RAFTKV_SANITIZE "" CACHE STRING "Sanitizers to enable (e.g. thread, address,undefined)")
if(RAFTKV_SANITIZE)
    add_compile_options(-fsanitize=${RAFTKV_SANITIZE} -fno-omit-frame-pointer)
    add_link_options(-fsanitize=${RAFTKV_SANITIZE})
endif()

# Option for OpenTelemetry
option(RAFTKV_ENABLE_OTEL "Enable OpenTelemetry instrumentation" OFF)

# GoogleTest via FetchContent
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()
include(GoogleTest)

# Core library
add_library(raftkv_core STATIC
    src/storage/slab_allocator.cpp
    src/storage/memtable.cpp
    src/storage/wal.cpp
    src/consensus/raft_node.cpp
    src/consensus/raft_log.cpp
    src/transport/sim_transport.cpp
)
target_include_directories(raftkv_core PUBLIC include)
target_compile_features(raftkv_core PUBLIC cxx_std_20)

# Unit tests
add_executable(unit_tests
    tests/unit/slab_allocator_test.cpp
    tests/unit/memtable_test.cpp
    tests/unit/raft_log_test.cpp
    tests/unit/raft_node_test.cpp
    tests/unit/wal_test.cpp
)
target_link_libraries(unit_tests PRIVATE raftkv_core GTest::gtest_main)
gtest_discover_tests(unit_tests)

# Cluster tests (over SimTransport)
add_executable(cluster_tests
    tests/cluster/election_test.cpp
    tests/cluster/replication_test.cpp
    tests/cluster/partition_test.cpp
    tests/cluster/figure8_test.cpp
)
target_link_libraries(cluster_tests PRIVATE raftkv_core GTest::gtest_main)
gtest_discover_tests(cluster_tests)

# Property tests
add_executable(property_tests
    tests/property/safety_property_test.cpp
)
target_link_libraries(property_tests PRIVATE raftkv_core GTest::gtest_main)
gtest_discover_tests(property_tests)

# Benchmark loadgen
add_executable(loadgen bench/loadgen.cpp)
target_compile_features(loadgen PRIVATE cxx_std_20)

# Server binary (requires gRPC - conditionally built)
# The server links raftkv_core + grpc_transport. Without gRPC FetchContent,
# we build a minimal version for testing.
add_library(raftkv_server_lib STATIC
    src/server/server.cpp
    src/transport/grpc_transport.cpp
)
target_link_libraries(raftkv_server_lib PUBLIC raftkv_core)
target_include_directories(raftkv_server_lib PUBLIC include)

# Server executable
add_executable(raftkv_server src/server/main.cpp)
target_link_libraries(raftkv_server PRIVATE raftkv_server_lib)
