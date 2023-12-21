
cmake_minimum_required(VERSION 3.20)

project(Main)
add_executable(Main Main.cpp parser.cpp lexer.cpp codegen.cpp)
target_include_directories(Main PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
