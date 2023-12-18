clang++  -rdynamic -g -O3 main.cpp $(llvm-config --cxxflags --ldflags --system-libs --libs core orcjit native) -o main.o
