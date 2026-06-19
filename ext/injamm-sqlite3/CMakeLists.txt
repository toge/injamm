cmake_minimum_required(VERSION 3.20)
project(injamm-sqlite3 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(injamm-sqlite3 INTERFACE)
add_library(injamm::sqlite3 ALIAS injamm-sqlite3)

target_include_directories(injamm-sqlite3 INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
target_link_libraries(injamm-sqlite3 INTERFACE injamm)

# ---- Install ----
include(GNUInstallDirs)

install(
  DIRECTORY include/
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  FILES_MATCHING PATTERN "*.hpp"
)

install(
  TARGETS injamm-sqlite3
  EXPORT injamm-sqlite3Targets
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(
  EXPORT injamm-sqlite3Targets
  FILE injamm-sqlite3Targets.cmake
  NAMESPACE injamm::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/injamm-sqlite3
)

include(CMakePackageConfigHelpers)
configure_package_config_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake/injamm-sqlite3Config.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/injamm-sqlite3Config.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/injamm-sqlite3
)
install(
  FILES ${CMAKE_CURRENT_BINARY_DIR}/injamm-sqlite3Config.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/injamm-sqlite3
)

# ---- Tests ----
if(BUILD_TEST)
  add_subdirectory(tests)
endif()
