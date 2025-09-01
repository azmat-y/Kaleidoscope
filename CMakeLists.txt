
cmake_minimum_required(VERSION 3.20)

project(Main)
add_executable(kaleidoscope  Main.cpp parser.cpp lexer.cpp codegen.cpp kpp.cpp)

find_package(LLVM 20.1 REQUIRED CONFIG
  COMPONENTS
    Core
    ExecutionEngine
    InstCombine
    JIT
    OrcJIT
    Support
    TransformUtils
    Target
  )
target_include_directories(kaleidoscope  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(kaleidoscope PUBLIC ${LLVM_INCLUDE_DIRS})
target_compile_definitions(kaleidoscope PRIVATE ${LLVM_DEFINITIONS})
target_link_libraries(kaleidoscope PRIVATE LLVM)

set_target_properties(kaleidoscope PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED YES
)
