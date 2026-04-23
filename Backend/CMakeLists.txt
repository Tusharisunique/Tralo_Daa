cmake_minimum_required(VERSION 3.10)
project(TRALO_Exchange)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Keep high performance flags
set(CMAKE_CXX_FLAGS "-O3 -march=native -Wall -Wextra -Wpedantic")

# Setup our unified includes
include_directories("Load Generator/include")
include_directories("Order Matching Engine/include")
include_directories("Fairness Analyzer/include")

set(SOURCES
    "src/exchange_main.cpp"
    "Load Generator/src/LoadGenerator.cpp"
    "Order Matching Engine/src/OrderBook.cpp"
)

add_executable(tralo_exchange ${SOURCES})
target_link_libraries(tralo_exchange pthread)
