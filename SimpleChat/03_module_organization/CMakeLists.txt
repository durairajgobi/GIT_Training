cmake_minimum_required(VERSION 3.14)
project(SimpleChat VERSION 1.3 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(simplechat_core
    src/config.cpp
    src/text_utils.cpp
    src/logger.cpp
    src/chat_state.cpp
    src/intents.cpp
    src/json.cpp
    src/claude_client.cpp
    src/chat_app.cpp
)
target_include_directories(simplechat_core PUBLIC include)
target_compile_options(simplechat_core PRIVATE -Wall -Wextra)

add_executable(simple_chat src/main.cpp)
target_link_libraries(simple_chat PRIVATE simplechat_core)

enable_testing()
add_executable(simplechat_tests tests/test_main.cpp)
target_link_libraries(simplechat_tests PRIVATE simplechat_core)
add_test(NAME simplechat_tests COMMAND simplechat_tests)
