cmake_minimum_required(VERSION 3.22.1)
project(jambo CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(oboe REQUIRED CONFIG)

# Ableton Link: scripts/fetch-deps.sh кладёт исходники в third_party/link.
set(LINK_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../../../third_party/link)
if(NOT EXISTS ${LINK_DIR}/AbletonLinkConfig.cmake)
  message(FATAL_ERROR "Ableton Link not found: run scripts/fetch-deps.sh")
endif()
include(${LINK_DIR}/AbletonLinkConfig.cmake)

add_library(jambo SHARED
  audio_engine.cpp
  link_session.cpp
  jni_bridge.cpp
)
target_compile_options(jambo PRIVATE -Wall -Wno-multichar -O2 -ffast-math)
target_link_libraries(jambo oboe::oboe Ableton::Link android log)
