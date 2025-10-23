cmake_minimum_required(VERSION 3.16)
project(cs425_mp2 CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_BUILD_TYPE Release)

# Fix for GCC dual ABI issue on Linux
add_definitions(-D_GLIBCXX_USE_CXX11_ABI=1)

add_compile_options(-Wall -Wextra -Wpedantic -Werror -Werror=missing-include-dirs)

# Include directories
include_directories(include libs/catch2)

# --- Core library ---
add_library(core
    src/socket.cpp
    src/logger.cpp
    src/message.cpp
    src/membership_list.cpp
    src/node.cpp
    src/shared.cpp
    # MP3 file system components
    src/file_block.cpp
    src/file_metadata.cpp
    src/consistent_hash_ring.cpp
    src/file_store.cpp
    src/client_tracker.cpp
    src/file_message.cpp
    src/file_operations_handler.cpp
)

# --- Applications ---
add_executable(main src/main.cpp)
target_link_libraries(main PRIVATE core)

add_executable(tests
    tests/test_main.cpp
    tests/test_message.cpp
    libs/catch2/catch_amalgamated.cpp
)

target_link_libraries(tests PRIVATE core)
