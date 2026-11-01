cmake_minimum_required(VERSION 3.14)
project(MasterChefRecipeGenerator LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

add_library(chef STATIC
  src/KnowledgeBase.cpp
  src/RecipeEngine.cpp
  src/ClaudeClient.cpp
)
target_include_directories(chef PUBLIC src)

add_executable(masterchef src/main.cpp)
target_link_libraries(masterchef PRIVATE chef)

enable_testing()
add_executable(chef_tests tests/test_main.cpp)
target_link_libraries(chef_tests PRIVATE chef)
add_test(NAME chef_tests COMMAND chef_tests)
