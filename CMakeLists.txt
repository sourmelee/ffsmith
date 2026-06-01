# FFSmith — clean-room engine for Final Fantasy Dimensions / Final Fantasy Legends
cmake_minimum_required(VERSION 3.16)
project(FFSmith VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
endif()

# --- SDL2: prefer the CMake config package, fall back to pkg-config ---
find_package(SDL2 QUIET)
if(NOT SDL2_FOUND)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(SDL2 REQUIRED sdl2)
endif()

file(GLOB_RECURSE FFSMITH_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/*.cpp)

add_executable(ffsmith ${FFSMITH_SOURCES})
target_include_directories(ffsmith PRIVATE ${CMAKE_SOURCE_DIR}/src ${SDL2_INCLUDE_DIRS})
target_compile_options(ffsmith PRIVATE -Wall -Wextra -Wpedantic)

if(TARGET SDL2::SDL2)
  target_link_libraries(ffsmith PRIVATE SDL2::SDL2)
else()
  target_link_libraries(ffsmith PRIVATE ${SDL2_LIBRARIES})
  target_link_directories(ffsmith PRIVATE ${SDL2_LIBRARY_DIRS})
endif()
