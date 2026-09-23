cmake_minimum_required(VERSION 3.10)
project(OmniLearnBackend CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(${CMAKE_SOURCE_DIR})

# Locate Z3
find_path(Z3_INCLUDE_DIR z3++.h HINTS /usr/include /usr/local/include)
find_library(Z3_LIBRARIES NAMES z3 HINTS /usr/lib /usr/local/lib /usr/lib/x86_64-linux-gnu)

if(NOT Z3_INCLUDE_DIR OR NOT Z3_LIBRARIES)
    message(FATAL_ERROR "Z3 headers or library not found! Please check your installation.")
endif()

# Locate yaml-cpp
find_package(yaml-cpp REQUIRED)

include_directories(${Z3_INCLUDE_DIR})

set(SOURCES
    ${CMAKE_SOURCE_DIR}/LearnerModel.cpp
    ${CMAKE_SOURCE_DIR}/TemplateEngine.cpp
    ${CMAKE_SOURCE_DIR}/Theory.cpp
    ${CMAKE_SOURCE_DIR}/YamlParser.cpp
    ${CMAKE_SOURCE_DIR}/GenericExprEngine.cpp
    ${CMAKE_SOURCE_DIR}/main.cpp
)

add_executable(omnilearn_backend ${SOURCES})

target_link_libraries(omnilearn_backend PRIVATE ${Z3_LIBRARIES} yaml-cpp pthread)

# main.cpp uses std::filesystem (directory scanning for theory YAML
# files). GCC < 9 ships this in a separate library that must be linked
# explicitly; GCC >= 9 and Clang with a recent libc++ do not need it.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9.0)
    target_link_libraries(omnilearn_backend PRIVATE stdc++fs)
endif()