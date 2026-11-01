cmake_minimum_required(VERSION 3.16)
project(rag_cpp VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(CURL REQUIRED)

# nlohmann/json: use the system package if present, otherwise fetch it.
find_package(nlohmann_json 3.10 QUIET)
if(NOT nlohmann_json_FOUND)
  include(FetchContent)
  FetchContent_Declare(json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz)
  FetchContent_MakeAvailable(json)
endif()

# Core library shared by all applications
add_library(rag_core
  src/http_client.cpp
  src/embedder.cpp
  src/vector_store.cpp
  src/llm.cpp
  src/chunker.cpp
  src/config.cpp
)
target_include_directories(rag_core PUBLIC include)
target_link_libraries(rag_core PUBLIC CURL::libcurl nlohmann_json::nlohmann_json)
if(MSVC)
  target_compile_options(rag_core PRIVATE /W4)
else()
  target_compile_options(rag_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Applications
add_executable(similarity_demo apps/similarity_demo.cpp)
target_link_libraries(similarity_demo PRIVATE rag_core)

add_executable(ingestor apps/ingestor.cpp)
target_link_libraries(ingestor PRIVATE rag_core)

add_executable(query apps/query.cpp)
target_link_libraries(query PRIVATE rag_core)

# Offline unit tests (no network / Docker needed)
enable_testing()
add_executable(rag_tests tests/test_main.cpp)
target_link_libraries(rag_tests PRIVATE rag_core)
add_test(NAME rag_tests COMMAND rag_tests)
