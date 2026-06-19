cmake_minimum_required(VERSION 3.20)
project(injamm-sqlite3 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(injamm-sqlite3 INTERFACE)
target_include_directories(injamm-sqlite3 INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(injamm-sqlite3 INTERFACE injamm)

if(BUILD_TEST)
  add_subdirectory(tests)
endif()
