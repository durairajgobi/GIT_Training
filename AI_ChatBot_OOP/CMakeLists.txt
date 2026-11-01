cmake_minimum_required(VERSION 3.14)
project(AIChatBotOOP LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(CURL REQUIRED)

# Use a system nlohmann_json if installed, otherwise download it.
find_package(nlohmann_json 3.2.0 QUIET)
if(NOT nlohmann_json_FOUND)
  include(FetchContent)
  FetchContent_Declare(json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz)
  FetchContent_MakeAvailable(json)
endif()

add_executable(chatbot
  src/main.cpp
  src/ChatClient.cpp
  src/HttpClientHelper.cpp
)

target_include_directories(chatbot PRIVATE include)
target_link_libraries(chatbot PRIVATE CURL::libcurl nlohmann_json::nlohmann_json)

if(MSVC)
  target_compile_options(chatbot PRIVATE /W4)
else()
  target_compile_options(chatbot PRIVATE -Wall -Wextra -Wpedantic)
endif()
