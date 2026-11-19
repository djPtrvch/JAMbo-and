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

# JSON (nlohmann): scripts/fetch-deps.sh кладёт заголовок в third_party/json.
set(JSON_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../../../third_party/json/single_include)
if(NOT EXISTS ${JSON_DIR}/nlohmann/json.hpp)
  message(FATAL_ERROR "nlohmann/json not found: run scripts/fetch-deps.sh")
endif()

# Core — порт ios/JAMbo.swiftpm/Core (ветка swift JAMbo), сверяется тестами tests/ на Linux.
file(GLOB_RECURSE CORE_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/core/*.cpp)
# Состояние приложения (порт части AppModel): правки, UNDO, данные для экрана.
file(GLOB APP_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/app/*.cpp)

add_library(jambo SHARED
  audio_engine.cpp
  link_session.cpp
  jni_bridge.cpp
  jni_app.cpp
  ${CORE_SOURCES}
  ${APP_SOURCES}
)
target_include_directories(jambo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/core ${CMAKE_CURRENT_SOURCE_DIR} ${JSON_DIR})
# Без -ffast-math: проверки NaN и точность должны совпадать с тестами и эталонами.
target_compile_options(jambo PRIVATE -Wall -Wno-multichar -O2)
target_link_libraries(jambo oboe::oboe Ableton::Link android log)
