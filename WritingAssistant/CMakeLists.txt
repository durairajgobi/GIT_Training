cmake_minimum_required(VERSION 3.16)
project(WritingAssistant VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(WA_BUILD_TESTS "Build unit tests" ON)
option(WA_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(WA_SANITIZE "Build with address and undefined-behaviour sanitizers" OFF)

add_library(wa_core
  src/json.cpp
  src/curl_http_client.cpp
  src/anthropic_client.cpp
  src/offline_client.cpp
  src/config.cpp
  src/features.cpp
  src/writing_assistant.cpp
  src/cli.cpp
)
target_include_directories(wa_core PUBLIC include)
find_package(Threads REQUIRED)
target_link_libraries(wa_core PUBLIC Threads::Threads)

target_compile_options(wa_core PUBLIC -Wall -Wextra -Wpedantic)
if(WA_WARNINGS_AS_ERRORS)
  target_compile_options(wa_core PUBLIC -Werror)
endif()
if(WA_SANITIZE)
  target_compile_options(wa_core PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(wa_core PUBLIC -fsanitize=address,undefined)
endif()

add_executable(writing_assistant src/main.cpp)
target_link_libraries(writing_assistant PRIVATE wa_core)

if(WA_BUILD_TESTS)
  enable_testing()
  add_executable(wa_tests
    tests/test_main.cpp
    tests/test_foundation.cpp
    tests/test_features.cpp
    tests/test_anthropic_client.cpp
    tests/test_cli.cpp
  )
  target_link_libraries(wa_tests PRIVATE wa_core)
  add_test(NAME unit_tests COMMAND wa_tests)

  # Smoke tests of the real binary in offline mode (no network, no key).
  add_test(NAME cli_help COMMAND writing_assistant --help)
  add_test(NAME cli_offline_summarize
    COMMAND writing_assistant --provider offline summarize --length short "First point. Second point.")
  set_tests_properties(cli_offline_summarize PROPERTIES PASS_REGULAR_EXPRESSION "^First point\\.")
  add_test(NAME cli_bad_option_fails
    COMMAND writing_assistant --provider offline tone --tone angry "hello")
  set_tests_properties(cli_bad_option_fails PROPERTIES WILL_FAIL TRUE)
endif()

install(TARGETS writing_assistant RUNTIME DESTINATION bin)
