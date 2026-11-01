cmake_minimum_required(VERSION 3.14)
project(cpp_httplib_demo CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(HTTPLIB_USE_OPENSSL "Enable HTTPS support via OpenSSL" OFF)
set(HTTPLIB_VERSION "0.18.3" CACHE STRING "cpp-httplib release tag to download")

# cpp-httplib is a single header: download it once into the build tree.
set(HTTPLIB_HEADER "${CMAKE_BINARY_DIR}/third_party/httplib.h")
if(NOT EXISTS "${HTTPLIB_HEADER}")
  message(STATUS "Downloading cpp-httplib v${HTTPLIB_VERSION}")
  file(DOWNLOAD
    "https://raw.githubusercontent.com/yhirose/cpp-httplib/v${HTTPLIB_VERSION}/httplib.h"
    "${HTTPLIB_HEADER}"
    STATUS dl_status TLS_VERIFY ON)
  list(GET dl_status 0 dl_code)
  if(NOT dl_code EQUAL 0)
    file(REMOVE "${HTTPLIB_HEADER}")
    message(FATAL_ERROR "Failed to download httplib.h: ${dl_status}")
  endif()
endif()

find_package(Threads REQUIRED)

add_executable(http_demo main.cpp)
target_include_directories(http_demo PRIVATE "${CMAKE_BINARY_DIR}/third_party")
target_link_libraries(http_demo PRIVATE Threads::Threads)

if(HTTPLIB_USE_OPENSSL)
  find_package(OpenSSL 3.0 REQUIRED)
  target_compile_definitions(http_demo PRIVATE CPPHTTPLIB_OPENSSL_SUPPORT)
  target_link_libraries(http_demo PRIVATE OpenSSL::SSL OpenSSL::Crypto)
endif()
