cmake_minimum_required(VERSION 3.10)
project(cpp_parser LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_library(cppparser STATIC
  src/lexer.cpp
  src/parser.cpp
  src/parser_decl.cpp
  src/parser_type.cpp
  src/parser_name.cpp
  src/parser_stmt.cpp
  src/parser_expr.cpp)
target_include_directories(cppparser PUBLIC src)
if(MSVC)
  target_compile_options(cppparser PRIVATE /W4)
else()
  target_compile_options(cppparser PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(cpp_parser src/main.cpp)
target_link_libraries(cpp_parser PRIVATE cppparser)

enable_testing()
add_test(NAME sample_tests
  COMMAND ${CMAKE_SOURCE_DIR}/tests/run_tests.sh $<TARGET_FILE:cpp_parser>
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
