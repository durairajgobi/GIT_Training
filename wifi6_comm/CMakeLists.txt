cmake_minimum_required(VERSION 3.16)
project(wifi6_comm VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE MinSizeRel)
endif()

option(WIFI6_EMBEDDED_FLAGS "Build library and app without exceptions/RTTI" ON)
option(WIFI6_BUILD_TESTS "Build host unit tests" ON)
option(WIFI6_WERROR "Treat warnings as errors" OFF)

set(WIFI6_WARNINGS -Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor -Wcast-align -Wformat=2)
if(WIFI6_WERROR)
  list(APPEND WIFI6_WARNINGS -Werror)
endif()
set(WIFI6_EMBEDDED_OPTS "")
if(WIFI6_EMBEDDED_FLAGS)
  set(WIFI6_EMBEDDED_OPTS -fno-exceptions -fno-rtti -ffunction-sections -fdata-sections)
endif()

add_library(wifi6core STATIC
  src/status.cpp
  src/log.cpp
  src/types.cpp
  src/frame.cpp
  src/twt.cpp
  src/ie_parser.cpp
  src/wpa_parse.cpp
  src/wpa_ctrl_driver.cpp
  src/sim_driver.cpp
  src/ap_selector.cpp
  src/link_monitor.cpp
  src/connection_manager.cpp
  src/transport.cpp
  src/message_channel.cpp)
target_include_directories(wifi6core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_options(wifi6core PRIVATE ${WIFI6_WARNINGS} ${WIFI6_EMBEDDED_OPTS})

add_executable(wifi6-comm app/main.cpp)
target_link_libraries(wifi6-comm PRIVATE wifi6core)
target_compile_options(wifi6-comm PRIVATE ${WIFI6_WARNINGS} ${WIFI6_EMBEDDED_OPTS})
if(WIFI6_EMBEDDED_FLAGS)
  target_link_options(wifi6-comm PRIVATE -Wl,--gc-sections)
endif()

include(GNUInstallDirs)
install(TARGETS wifi6-comm RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES systemd/wifi6-comm.service DESTINATION lib/systemd/system)

if(WIFI6_BUILD_TESTS)
  enable_testing()
  add_executable(wifi6_tests tests/test_main.cpp)
  target_link_libraries(wifi6_tests PRIVATE wifi6core)
  target_compile_options(wifi6_tests PRIVATE ${WIFI6_WARNINGS})
  add_test(NAME wifi6_tests COMMAND wifi6_tests)
endif()
