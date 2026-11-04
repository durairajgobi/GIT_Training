cmake_minimum_required(VERSION 3.16)
project(ble_manager VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

option(BLEMGR_BUILD_TESTS "Build unit and integration tests" ON)
option(BLEMGR_WARNINGS_AS_ERRORS "Treat warnings as errors" OFF)
option(BLEMGR_SANITIZE "Build with ASan/UBSan" OFF)

add_compile_options(-Wall -Wextra -Wpedantic -Wshadow -Wno-unused-parameter)
if(BLEMGR_WARNINGS_AS_ERRORS)
  add_compile_options(-Werror)
endif()
if(BLEMGR_SANITIZE)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

include(GNUInstallDirs)
find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GIO REQUIRED IMPORTED_TARGET gio-2.0>=2.56)

# Stack-independent policy layer: unit-testable on any host.
add_library(blemgr_core STATIC
  src/types.cpp
  src/log.cpp
  src/reconnect_policy.cpp
  src/ble_manager.cpp
  src/config.cpp)
target_include_directories(blemgr_core PUBLIC include)
target_link_libraries(blemgr_core PUBLIC Threads::Threads)

# BlueZ D-Bus backend + GLib event loop.
add_library(blemgr_bluez STATIC
  src/bluez/gdbus_util.cpp
  src/bluez/glib_scheduler.cpp
  src/bluez/bluez_backend.cpp
  src/bluez/bluez_agent.cpp
  src/bluez/bluez_peripheral.cpp)
target_link_libraries(blemgr_bluez PUBLIC blemgr_core PRIVATE PkgConfig::GIO)

add_executable(blemgrd app/blemgrd.cpp app/shell.cpp)
target_link_libraries(blemgrd PRIVATE blemgr_bluez)

install(TARGETS blemgrd RUNTIME DESTINATION ${CMAKE_INSTALL_SBINDIR})
install(FILES config/blemgrd.conf DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/blemgr)
install(FILES systemd/blemgrd.service DESTINATION lib/systemd/system)
install(FILES config/blemgrd-dbus.conf DESTINATION ${CMAKE_INSTALL_DATADIR}/dbus-1/system.d
        RENAME com.tcu.blemgr.conf)

if(BLEMGR_BUILD_TESTS)
  enable_testing()
  foreach(t types reconnect_policy config ble_manager)
    add_executable(test_${t} tests/test_${t}.cpp)
    target_include_directories(test_${t} PRIVATE tests)
    target_link_libraries(test_${t} PRIVATE blemgr_core)
    add_test(NAME ${t} COMMAND test_${t})
  endforeach()

  # End-to-end: real BluezBackend + BleManager against a mock bluetoothd on a
  # private dbus-daemon. Skipped when dbus-daemon is not installed.
  find_program(DBUS_DAEMON dbus-daemon)
  add_executable(mock_bluez tests/mock_bluez/mock_bluez.cpp)
  target_link_libraries(mock_bluez PRIVATE PkgConfig::GIO)
  add_executable(test_bluez_e2e tests/test_bluez_e2e.cpp)
  target_include_directories(test_bluez_e2e PRIVATE tests)
  target_link_libraries(test_bluez_e2e PRIVATE blemgr_bluez PkgConfig::GIO)
  if(DBUS_DAEMON)
    add_test(NAME bluez_e2e COMMAND test_bluez_e2e $<TARGET_FILE:mock_bluez> ${DBUS_DAEMON})
    set_tests_properties(bluez_e2e PROPERTIES TIMEOUT 60)
  endif()
endif()
