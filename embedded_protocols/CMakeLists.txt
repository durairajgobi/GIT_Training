cmake_minimum_required(VERSION 3.16)
project(embedded_protocols C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(EP_BUILD_TESTS     "Build host unit tests"                      ON)
option(EP_BUILD_LINUX     "Build Linux ports (spidev, i2c-dev, SocketCAN, AF_PACKET, PHC)" ON)
option(EP_BUILD_EXAMPLES  "Build Linux example programs"               ON)
option(EP_BUILD_STM32_CHECK "Compile-check the STM32F4 register-level port" ON)
option(EP_WERROR          "Treat warnings as errors"                   ON)
option(EP_SANITIZE        "Build with AddressSanitizer + UBSan"        OFF)

set(EP_WARNINGS -Wall -Wextra -Wshadow -Wconversion -Wno-sign-conversion)
if(EP_WERROR)
  list(APPEND EP_WARNINGS -Werror)
endif()
if(EP_SANITIZE)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
  add_link_options(-fsanitize=address,undefined)
endif()

# ---------------------------------------------------------------------------
# Portable C core: no OS, no heap. This is what goes into MCU firmware.
# ---------------------------------------------------------------------------
add_library(ep_core STATIC
  src/ep_common.c
  src/ep_spi.c
  src/ep_i2c.c
  src/ep_uart.c
  src/ep_serial_line.c
  src/ep_framing.c
  src/ep_can.c
  src/ep_isotp.c
  src/ep_eth.c
  src/ep_ptp.c
  src/ep_tsn.c)
target_include_directories(ep_core PUBLIC include)
target_compile_options(ep_core PRIVATE ${EP_WARNINGS} -pedantic)
target_link_libraries(ep_core PUBLIC m)

# C++17 header-only layer (templates over the C core / HAL policies).
add_library(ep_cpp INTERFACE)
target_include_directories(ep_cpp INTERFACE cpp/include)
target_link_libraries(ep_cpp INTERFACE ep_core)

# ---------------------------------------------------------------------------
# Linux backends
# ---------------------------------------------------------------------------
if(EP_BUILD_LINUX AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_library(ep_linux STATIC
    ports/linux/ep_linux_clock.c
    ports/linux/ep_linux_spidev.c
    ports/linux/ep_linux_i2cdev.c
    ports/linux/ep_linux_serial.c
    ports/linux/ep_linux_socketcan.c
    ports/linux/ep_linux_rawsock.c
    ports/linux/ep_linux_phc.c)
  target_include_directories(ep_linux PUBLIC ports/linux)
  target_compile_definitions(ep_linux PRIVATE _GNU_SOURCE)
  target_compile_options(ep_linux PRIVATE ${EP_WARNINGS})
  target_link_libraries(ep_linux PUBLIC ep_core)

  if(EP_BUILD_EXAMPLES)
    add_executable(ptp_e2e_slave examples/linux/ptp_e2e_slave.c)
    target_link_libraries(ptp_e2e_slave ep_linux)
    add_executable(isotp_socketcan examples/linux/isotp_socketcan.c)
    target_link_libraries(isotp_socketcan ep_linux)
    add_executable(tsn_taprio_plan examples/linux/tsn_taprio_plan.c)
    target_link_libraries(tsn_taprio_plan ep_core)
    add_executable(modbus_rs485 examples/linux/modbus_rs485.cpp)
    target_link_libraries(modbus_rs485 ep_linux ep_cpp)
    foreach(t ptp_e2e_slave isotp_socketcan tsn_taprio_plan modbus_rs485)
      target_compile_options(${t} PRIVATE ${EP_WARNINGS})
    endforeach()
    if(EP_BUILD_TESTS)
      add_test(NAME example_tsn_taprio_plan COMMAND tsn_taprio_plan eth0 1000)
    endif()
  endif()
endif()

# Register-level MCU port: compiled (not linked) on the host so it cannot rot.
if(EP_BUILD_STM32_CHECK)
  add_library(ep_stm32f4_check OBJECT
    ports/stm32f4/ep_stm32f4_uart.c
    ports/stm32f4/ep_stm32f4_spi.c
    ports/stm32f4/ep_stm32f4_bxcan.c)
  target_include_directories(ep_stm32f4_check PRIVATE include ports/stm32f4)
  target_compile_options(ep_stm32f4_check PRIVATE ${EP_WARNINGS})
endif()

# ---------------------------------------------------------------------------
# Tests (simulated hardware, run on the host)
# ---------------------------------------------------------------------------
if(EP_BUILD_TESTS)
  enable_testing()
  add_library(ep_sim STATIC tests/sim/sim_spi_slave.c tests/sim/sim_i2c_eeprom.c)
  target_include_directories(ep_sim PUBLIC tests/sim tests)
  target_link_libraries(ep_sim PUBLIC ep_core)

  foreach(t common spi i2c uart_line framing can isotp eth ptp tsn)
    add_executable(test_${t} tests/c/test_${t}.c)
    target_link_libraries(test_${t} ep_sim)
    target_compile_options(test_${t} PRIVATE ${EP_WARNINGS})
    add_test(NAME c_${t} COMMAND test_${t})
  endforeach()

  foreach(t containers mcp2515 modbus i2c_eeprom ptp_tsn)
    add_executable(test_cpp_${t} tests/cpp/test_${t}.cpp)
    target_link_libraries(test_cpp_${t} ep_cpp ep_sim)
    target_compile_options(test_cpp_${t} PRIVATE ${EP_WARNINGS} -fno-exceptions -fno-rtti)
    add_test(NAME cpp_${t} COMMAND test_cpp_${t})
  endforeach()
endif()
