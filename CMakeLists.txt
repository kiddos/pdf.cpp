cmake_minimum_required(VERSION 3.16)
project(pdfcpp VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(PDFCPP_BUILD_TESTS "Build pdfcpp tests" ON)
option(PDFCPP_BUILD_EXAMPLES "Build pdfcpp examples" ON)

find_package(ZLIB REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(JPEG)
find_package(PNG)

# ----------------------------------------------------------------------------
# Embedded resources (standard 14 font metrics, Adobe glyph list)
# ----------------------------------------------------------------------------
set(PDFCPP_RESOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/resources)
file(GLOB PDFCPP_RESOURCE_FILES
  ${PDFCPP_RESOURCE_DIR}/font/afm/*.afm
  ${PDFCPP_RESOURCE_DIR}/font/AdobeGlyphList.txt)
set(PDFCPP_EMBEDDED_CPP ${CMAKE_CURRENT_BINARY_DIR}/generated/EmbeddedResources.cpp)
add_custom_command(
  OUTPUT ${PDFCPP_EMBEDDED_CPP}
  COMMAND ${CMAKE_COMMAND}
          -DRESOURCE_DIR=${PDFCPP_RESOURCE_DIR}
          -DOUTPUT=${PDFCPP_EMBEDDED_CPP}
          -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedResources.cmake
  DEPENDS ${PDFCPP_RESOURCE_FILES} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedResources.cmake
  COMMENT "Embedding pdfcpp resources")

# ----------------------------------------------------------------------------
# Library
# ----------------------------------------------------------------------------
file(GLOB_RECURSE PDFCPP_SOURCES CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)

add_library(pdfcpp ${PDFCPP_SOURCES} ${PDFCPP_EMBEDDED_CPP})
add_library(pdfcpp::pdfcpp ALIAS pdfcpp)
target_include_directories(pdfcpp
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(pdfcpp PUBLIC ZLIB::ZLIB PRIVATE OpenSSL::Crypto)
if(JPEG_FOUND)
  target_compile_definitions(pdfcpp PRIVATE PDFCPP_HAVE_JPEG=1)
  target_link_libraries(pdfcpp PRIVATE JPEG::JPEG)
endif()
if(PNG_FOUND)
  target_compile_definitions(pdfcpp PRIVATE PDFCPP_HAVE_PNG=1)
  target_link_libraries(pdfcpp PRIVATE PNG::PNG)
endif()
if(MSVC)
  target_compile_options(pdfcpp PRIVATE /W3 /utf-8)
else()
  target_compile_options(pdfcpp PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

include(GNUInstallDirs)
install(TARGETS pdfcpp EXPORT pdfcppTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT pdfcppTargets NAMESPACE pdfcpp:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/pdfcpp)

# ----------------------------------------------------------------------------
# Tests & examples
# ----------------------------------------------------------------------------
if(PDFCPP_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
if(PDFCPP_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()
