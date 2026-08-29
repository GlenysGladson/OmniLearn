cmake_minimum_required(VERSION 3.10)
project(OmniLearnBackend CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Headers and sources currently sit flat in the project root
# (LearnerModel.hpp/cpp, TemplateEngine.hpp/cpp, Theory.hpp, main.cpp).
# If you later move to src/ + include/, just update the two blocks below.
include_directories(${CMAKE_SOURCE_DIR})

find_path(Z3_INCLUDE_DIR z3++.h HINTS /usr/include /usr/local/include)
find_library(Z3_LIBRARIES NAMES z3 HINTS /usr/lib /usr/local/lib /usr/lib/x86_64-linux-gnu)

if(NOT Z3_INCLUDE_DIR OR NOT Z3_LIBRARIES)
    message(FATAL_ERROR "Z3 headers or library not found! Please check your installation.")
else()
    message(STATUS "Found Z3 headers at: ${Z3_INCLUDE_DIR}")
    message(STATUS "Found Z3 library at: ${Z3_LIBRARIES}")
endif()

include_directories(${Z3_INCLUDE_DIR})

set(SOURCES
    ${CMAKE_SOURCE_DIR}/LearnerModel.cpp
    ${CMAKE_SOURCE_DIR}/TemplateEngine.cpp
    ${CMAKE_SOURCE_DIR}/main.cpp
)

add_executable(omnilearn_backend ${SOURCES})

target_link_libraries(omnilearn_backend PRIVATE ${Z3_LIBRARIES} pthread)