cmake_minimum_required(VERSION 3.16)
project(wifi_manager VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(WIFIMGR_BUILD_TESTS "Build unit and end-to-end tests" ON)
option(WIFIMGR_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(WIFIMGR_SANITIZE "Build with address and undefined-behaviour sanitizers" OFF)

include(GNUInstallDirs)

add_library(wifimgr_core STATIC
  src/log.cpp
  src/strutil.cpp
  src/json.cpp
  src/types.cpp
  src/event_loop.cpp
  src/wpa_ctrl.cpp
  src/wpa_parser.cpp
  src/supplicant.cpp
  src/profile_store.cpp
  src/network_selector.cpp
  src/dhcp_client.cpp
  src/netlink_monitor.cpp
  src/connection_manager.cpp
  src/config.cpp
  src/control_server.cpp
  src/control_client.cpp
  src/commands.cpp
)
target_include_directories(wifimgr_core PUBLIC include)
target_compile_definitions(wifimgr_core PUBLIC WIFIMGR_VERSION="${PROJECT_VERSION}")
target_compile_options(wifimgr_core PUBLIC -Wall -Wextra -Wpedantic -Wshadow)
if(WIFIMGR_WARNINGS_AS_ERRORS)
  target_compile_options(wifimgr_core PUBLIC -Werror)
endif()
if(WIFIMGR_SANITIZE)
  target_compile_options(wifimgr_core PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(wifimgr_core PUBLIC -fsanitize=address,undefined)
endif()

add_executable(wifimgrd apps/wifimgrd.cpp)
target_link_libraries(wifimgrd PRIVATE wifimgr_core)

add_executable(wifictl apps/wifictl.cpp)
target_link_libraries(wifictl PRIVATE wifimgr_core)

install(TARGETS wifimgrd RUNTIME DESTINATION ${CMAKE_INSTALL_SBINDIR})
install(TARGETS wifictl RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(PROGRAMS scripts/udhcpc-hook DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/wifimgr)
install(FILES config/wifimgr.conf DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/wifimgr)
install(FILES config/wpa_supplicant-wlan0.conf DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/wpa_supplicant)
install(FILES systemd/wifimgrd.service DESTINATION lib/systemd/system)

if(WIFIMGR_BUILD_TESTS)
  enable_testing()
  add_executable(wifimgr_tests
    tests/test_main.cpp
    tests/test_strutil_json.cpp
    tests/test_wpa_parser.cpp
    tests/test_profile_store.cpp
    tests/test_selector.cpp
    tests/test_connection_manager.cpp
    tests/test_commands.cpp
    tests/test_event_loop.cpp
  )
  target_include_directories(wifimgr_tests PRIVATE tests)
  target_link_libraries(wifimgr_tests PRIVATE wifimgr_core)
  add_test(NAME unit_tests COMMAND wifimgr_tests)

  # End to end: real wifimgrd + wifictl against a simulated wpa_supplicant.
  find_package(Python3 COMPONENTS Interpreter)
  if(Python3_FOUND)
    add_test(NAME e2e_tests
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/e2e/test_e2e.py
              --wifimgrd $<TARGET_FILE:wifimgrd> --wifictl $<TARGET_FILE:wifictl>)
    set_tests_properties(e2e_tests PROPERTIES TIMEOUT 300)
  endif()
endif()
