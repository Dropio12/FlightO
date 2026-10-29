cmake_minimum_required(VERSION 3.16)
project(FlightO VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(FLIGHTO_BUILD_TESTS "Build the unit tests" ON)

# Everything except main(): the optimizer, the fare providers and helpers.
add_library(flighto_core STATIC
  src/csv_provider.cpp
  src/date.cpp
  src/disk_cache.cpp
  src/fare_provider.cpp
  src/http.cpp
  src/optimizer.cpp
  src/travelpayouts_provider.cpp
  src/trip.cpp
)
target_include_directories(flighto_core PUBLIC src)
# Vendored single-header libraries (MIT licensed, see third_party/*/LICENSE*).
target_include_directories(flighto_core SYSTEM PUBLIC third_party)

if(MSVC)
  target_compile_options(flighto_core PUBLIC /W4 /permissive-)
else()
  target_compile_options(flighto_core PUBLIC -Wall -Wextra -Wpedantic)
endif()

# MinGW builds link the C++ runtime statically so flighto.exe runs on any
# Windows machine without the compiler's DLLs on PATH.
if(MINGW)
  target_link_options(flighto_core PUBLIC -static)
endif()

add_executable(flighto src/main.cpp)
target_link_libraries(flighto PRIVATE flighto_core)

if(FLIGHTO_BUILD_TESTS)
  enable_testing()

  add_executable(flighto_tests tests/test_main.cpp)
  target_link_libraries(flighto_tests PRIVATE flighto_core)
  target_compile_definitions(flighto_tests PRIVATE
    FLIGHTO_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
  add_test(NAME unit_tests COMMAND flighto_tests)

  # End-to-end smoke test: the README demo on the sample fares must find a trip.
  add_test(NAME demo_sample_fares
    COMMAND flighto --from YUL --visit CDG:3-5 --visit BCN:2-4 --visit FCO:3-4
            --depart 2027-05-01 --return-by 2027-05-20
            --fares ${CMAKE_CURRENT_SOURCE_DIR}/data/sample_fares.csv)
  set_tests_properties(demo_sample_fares PROPERTIES PASS_REGULAR_EXPRESSION "Best trip: [0-9]+\\.[0-9][0-9] USD")
endif()
