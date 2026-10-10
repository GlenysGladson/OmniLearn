cmake_minimum_required(VERSION 3.10)
project(OmniLearnBackend CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(${CMAKE_SOURCE_DIR})

# Linux filesystems are case-sensitive. The sources must be named exactly
# GenericExprEngine.cpp / GenericExprEngine.hpp (capital E and E).
foreach(f GenericExprEngine.cpp GenericExprEngine.hpp)
    if(NOT EXISTS "${CMAKE_SOURCE_DIR}/${f}")
        message(FATAL_ERROR
            "${f} not found. If the file is named 'Genericexprengine...', rename it to ${f}.")
    endif()
endforeach()

# --- Z3 ---------------------------------------------------------------------
# Preferred: the CMake package shipped with libz3-dev (Ubuntu/WSL: sudo apt install libz3-dev).
# Fallback: search the usual Linux locations.
find_package(Z3 CONFIG QUIET)
set(OMNI_Z3_LIBS "")
if(TARGET z3::libz3)
    set(OMNI_Z3_LIBS z3::libz3)
elseif(Z3_FOUND AND Z3_LIBRARIES)
    if(Z3_CXX_INCLUDE_DIRS)
        include_directories(${Z3_CXX_INCLUDE_DIRS})
    endif()
    set(OMNI_Z3_LIBS ${Z3_LIBRARIES})
else()
    find_path(Z3_INCLUDE_DIR z3++.h
              HINTS /usr/include /usr/local/include
              PATH_SUFFIXES z3)
    find_library(Z3_LIB NAMES z3
                 HINTS /usr/lib /usr/local/lib /usr/lib/x86_64-linux-gnu /usr/lib/aarch64-linux-gnu)
    if(NOT Z3_INCLUDE_DIR OR NOT Z3_LIB)
        message(FATAL_ERROR
            "Z3 headers or library not found. On Ubuntu/WSL run: sudo apt install libz3-dev")
    endif()
    include_directories(${Z3_INCLUDE_DIR})
    set(OMNI_Z3_LIBS ${Z3_LIB})
endif()

# --- yaml-cpp (Ubuntu/WSL: sudo apt install libyaml-cpp-dev) ----------------
find_package(yaml-cpp REQUIRED)

find_package(Threads REQUIRED)

set(SOURCES
    ${CMAKE_SOURCE_DIR}/LearnerModel.cpp
    ${CMAKE_SOURCE_DIR}/TemplateEngine.cpp
    ${CMAKE_SOURCE_DIR}/Theory.cpp
    ${CMAKE_SOURCE_DIR}/YamlParser.cpp
    ${CMAKE_SOURCE_DIR}/GenericExprEngine.cpp
    ${CMAKE_SOURCE_DIR}/TemplateValidator.cpp
    ${CMAKE_SOURCE_DIR}/InstanceValidator.cpp
)

set(OMNI_TARGETS omnilearn_backend omnilearn_validate omnilearn_tests)

add_executable(omnilearn_backend ${SOURCES} ${CMAKE_SOURCE_DIR}/main.cpp)
add_executable(omnilearn_validate ${SOURCES} ${CMAKE_SOURCE_DIR}/validate_main.cpp)
add_executable(omnilearn_tests ${SOURCES} ${CMAKE_SOURCE_DIR}/tests/test_validator.cpp)

foreach(t ${OMNI_TARGETS})
    target_link_libraries(${t} PRIVATE ${OMNI_Z3_LIBS} yaml-cpp Threads::Threads)
    # GCC < 9 ships std::filesystem in a separate library.
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9.0)
        target_link_libraries(${t} PRIVATE stdc++fs)
    endif()
endforeach()

enable_testing()
add_test(NAME validator_tests COMMAND omnilearn_tests)
