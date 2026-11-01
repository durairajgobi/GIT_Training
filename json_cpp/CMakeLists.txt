cmake_minimum_required(VERSION 3.10)
project(SimpleJson CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(simplejson json.cpp)
target_include_directories(simplejson PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(json_demo main.cpp)
target_link_libraries(json_demo simplejson)

add_executable(test_json test_json.cpp)
target_link_libraries(test_json simplejson)

enable_testing()
add_test(NAME test_json COMMAND test_json)
