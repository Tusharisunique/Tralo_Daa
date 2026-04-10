cmake_minimum_required(VERSION 3.10)
project(TRALO_LoadGenerator)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Compiler optimization flags for maximum performance
set(CMAKE_CXX_FLAGS "-O3 -march=native -Wall -Wextra -Wpedantic")

# Include directory
include_directories(include)

# Source files
set(SOURCES
    src/LoadGenerator.cpp
    src/main.cpp
)

# Executable
add_executable(load_generator ${SOURCES})
target_link_libraries(load_generator pthread)
