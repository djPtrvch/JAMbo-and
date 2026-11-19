# Тесты Core на Linux (хост): сверка C++-порта с эталонами ветки swift JAMbo.
# Сборка: scripts/fetch-test-deps.sh; cmake -S tests -B build/tests && cmake --build build/tests && build/tests/core_tests
cmake_minimum_required(VERSION 3.22)
project(jambo_core_tests CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(CORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../app/src/main/cpp/core)
set(TP ${CMAKE_CURRENT_SOURCE_DIR}/../third_party)
file(GLOB_RECURSE CORE_SOURCES CONFIGURE_DEPENDS ${CORE_DIR}/*.cpp)

add_executable(core_tests main.cpp golden.cpp fx_golden.cpp jambo_golden.cpp drums_test.cpp texgran_test.cpp tables_test.cpp model_test.cpp coding_test.cpp edit_test.cpp audio_test.cpp synth_test.cpp seq_test.cpp sampler_test.cpp eq_golden.cpp eq_test.cpp master_golden.cpp master_test.cpp arp_test.cpp monitor_test.cpp ${CORE_SOURCES})
target_include_directories(core_tests PRIVATE ${CORE_DIR} ${TP}/json/single_include)
target_compile_options(core_tests PRIVATE -Wall -Wextra -O2)
# gcc 15: ложные -Wmaybe-uninitialized в std::optional<std::map> забивают лог CI.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  target_compile_options(core_tests PRIVATE -Wno-maybe-uninitialized)
endif()
target_compile_definitions(core_tests PRIVATE
  GOLDEN_DIR="${TP}/jambo-golden/ios/CoreTests/golden")
