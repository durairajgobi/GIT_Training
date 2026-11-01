cmake_minimum_required(VERSION 3.16)
project(openai_cpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# libcurl handles the HTTPS transport (TLS via OpenSSL)
find_package(CURL REQUIRED)

# nlohmann/json for building and parsing JSON.
# Use the system package if present, otherwise download it.
find_package(nlohmann_json 3.10 QUIET)
if(NOT nlohmann_json_FOUND)
  include(FetchContent)
  FetchContent_Declare(json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz)
  FetchContent_MakeAvailable(json)
endif()

add_library(openai_client
  src/openai_request.cpp
  src/https_client.cpp
  src/openai_response.cpp
  src/cost.cpp)
target_include_directories(openai_client PUBLIC include)
target_link_libraries(openai_client PUBLIC CURL::libcurl nlohmann_json::nlohmann_json)

add_executable(openai_demo src/main.cpp)
target_link_libraries(openai_demo PRIVATE openai_client)

# Offline tests: no API key or network needed
enable_testing()
add_executable(openai_tests src/tests.cpp)
target_link_libraries(openai_tests PRIVATE openai_client)
add_test(NAME openai_tests COMMAND openai_tests)
