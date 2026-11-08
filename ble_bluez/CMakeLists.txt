cmake_minimum_required(VERSION 3.16)
project(ble_bluez CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()
add_compile_options(-Wall -Wextra -Wpedantic)

option(BLE_BUILD_TESTS "Build host unit tests" ON)

include(GNUInstallDirs)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GIO REQUIRED IMPORTED_TARGET gio-2.0 gio-unix-2.0)

# Pure helpers: no GLib dependency, unit-testable anywhere.
add_library(ble_utils STATIC src/utils.cpp)
target_include_directories(ble_utils PUBLIC include)

add_library(ble_bluez STATIC
  src/dbus.cpp
  src/adapter.cpp
  src/ble_manager.cpp
  src/device.cpp
  src/agent.cpp
  src/gatt_client.cpp
  src/gatt_server.cpp)
target_include_directories(ble_bluez PUBLIC include)
target_link_libraries(ble_bluez PUBLIC ble_utils PkgConfig::GIO)

add_executable(ble-app src/main.cpp)
target_link_libraries(ble-app PRIVATE ble_bluez)

install(TARGETS ble-app RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES config/ble-app.conf DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/dbus-1/system.d)

if(BLE_BUILD_TESTS)
  enable_testing()
  add_executable(test_utils tests/test_utils.cpp)
  target_link_libraries(test_utils PRIVATE ble_utils)
  add_test(NAME utils COMMAND test_utils)
endif()

# End-to-end test against a fake bluetoothd on a private bus (needs dbus-daemon).
find_program(DBUS_DAEMON dbus-daemon)
if(BLE_BUILD_TESTS AND DBUS_DAEMON)
  add_executable(fake_bluez tests/fake_bluez.cpp)
  target_link_libraries(fake_bluez PRIVATE PkgConfig::GIO)
  add_test(NAME integration
           COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration.sh ${CMAKE_CURRENT_BINARY_DIR})
endif()
