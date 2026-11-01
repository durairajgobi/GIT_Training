cmake_minimum_required(VERSION 3.16)
project(OllamaCppChat LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(CURL REQUIRED)

# Use a system nlohmann_json if installed, otherwise download it.
find_package(nlohmann_json 3.10 QUIET)
if(NOT nlohmann_json_FOUND)
  include(FetchContent)
  FetchContent_Declare(json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz)
  FetchContent_MakeAvailable(json)
endif()

add_library(llm STATIC
  src/http.cpp
  src/openai_client.cpp
  src/ollama_client.cpp
  src/client_factory.cpp)
target_include_directories(llm PUBLIC include)
target_link_libraries(llm PUBLIC CURL::libcurl nlohmann_json::nlohmann_json)

add_executable(llm_chat src/main.cpp)
target_link_libraries(llm_chat PRIVATE llm)

if(MSVC)
  target_compile_options(llm PRIVATE /W4)
else()
  target_compile_options(llm PRIVATE -Wall -Wextra -Wpedantic)
endif()
