cmake_minimum_required(VERSION 3.16)
project(tcu-apps C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
add_compile_options(-Wall -Wextra -Wno-unused-parameter -D_GNU_SOURCE)

option(TCU_BUILD_TESTS "Build host unit tests" ON)
option(TCU_BUILD_BTMGR "Build btmgr (needs gio-2.0)" ON)
option(TCU_BUILD_TLS "Build TLS client (needs OpenSSL)" ON)

include(GNUInstallDirs)
find_package(PkgConfig)

# Pure state machines / codecs: no OS dependencies, unit-tested on the host.
add_library(tcu_core STATIC
  src/common/backoff.c
  src/ecall/msd.c
  src/ecall/ecall_sm.c
  src/lte/at.c
  src/lte/lte_sm.c
  src/bt/bt_reconnect.c
  src/bt/hfp_codec.c
  src/pm/pm_policy.c)
target_include_directories(tcu_core PUBLIC
  src/common src/ecall src/lte src/bt src/pm)

add_library(tcu_os STATIC src/common/tty.c src/security/afalg.c)
target_include_directories(tcu_os PUBLIC src/common src/security)

add_executable(ecalld src/ecall/ecalld.c)
target_link_libraries(ecalld tcu_core tcu_os)

add_executable(ltecm src/lte/ltecm.c)
target_link_libraries(ltecm tcu_core tcu_os)

add_executable(tcu-pmd src/pm/tcu_pmd.c)
target_link_libraries(tcu-pmd tcu_core)

add_executable(tcu-hash src/security/tcu_hash.c)
target_link_libraries(tcu-hash tcu_os)

set(TCU_BINS ecalld ltecm tcu-pmd tcu-hash)

if(TCU_BUILD_BTMGR AND PKG_CONFIG_FOUND)
  pkg_check_modules(GIO IMPORTED_TARGET gio-2.0)
  if(GIO_FOUND)
    add_executable(btmgr src/bt/btmgr.c)
    target_link_libraries(btmgr tcu_core PkgConfig::GIO)
    list(APPEND TCU_BINS btmgr)
  endif()
endif()

if(TCU_BUILD_TLS)
  find_package(OpenSSL 1.1.1 REQUIRED)
  add_library(tcu_tls STATIC src/security/tls_client.c)
  target_include_directories(tcu_tls PUBLIC src/common src/security)
  target_link_libraries(tcu_tls OpenSSL::SSL OpenSSL::Crypto)
  add_executable(tcu-tls-probe src/security/tcu_tls_probe.c)
  target_link_libraries(tcu-tls-probe tcu_tls)
  list(APPEND TCU_BINS tcu-tls-probe)
endif()

install(TARGETS ${TCU_BINS} RUNTIME DESTINATION ${CMAKE_INSTALL_SBINDIR})
install(DIRECTORY systemd/ DESTINATION lib/systemd/system FILES_MATCHING PATTERN "*.service")
install(PROGRAMS src/hotspot/hotspot_policy.py src/hotspot/captive_portal.py
        DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/tcu-hotspot)
install(PROGRAMS src/hotspot/hotspot-up.sh DESTINATION ${CMAKE_INSTALL_SBINDIR}
        RENAME tcu-hotspot-up)
install(DIRECTORY config/ DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/tcu)

if(TCU_BUILD_TESTS)
  enable_testing()
  foreach(t backoff msd ecall_sm at lte_sm bt_reconnect hfp_codec pm_policy afalg)
    add_executable(test_${t} tests/c/test_${t}.c)
    target_include_directories(test_${t} PRIVATE tests/c)
    target_compile_definitions(test_${t} PRIVATE TCU_LOG_STDERR)
    target_link_libraries(test_${t} tcu_core tcu_os)
    add_test(NAME ${t} COMMAND test_${t})
  endforeach()
endif()
