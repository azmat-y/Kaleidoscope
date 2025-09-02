
# Kaleidoscope

An Ahead Of Time (AOT) compiler for the kaleidoscope language made with LLVM Compiler Infrastructure with the help of [My First language frontend with LLVM](https://llvm.org/docs/tutorial/MyFirstLanguageFrontend/index.html). I also added the functionality for importing files, which works similar to the c pre-processor at a basic level. It combines the included file with the rest of the file and then starts to process.

## Quickstart

First you need to install the dependences and tools.

1. LLVM 20.1.8 (it should also work on 20.1.x)
2. Cmake
3. Clang

```
git clone https://github.com/azmat-y/Kaleidoscope
cd Kaleidoscope
cmake --preset release # generate cmake files
cmake --build build    # build the project
```

You also need to compile runtime.cpp before using the compiler
```
clang++ runtime.cpp -o runtime.o
```

## Usage
```
build/kaleidoscope demo/factorial.kd
./a.out

# result 
120.000000

# Try this 
build/kaleidoscope demo/set.kd
./a.out
```

More examples are inside the demo directory. 

Keep in mind that, if you want to directly execute a file it must have top level expressions. As they are put inside the main function.

## Running with docker

```
cd Kaleidoscope
docker build -t kd:1.0 .
docker run --name compiler kd:1.0 demo/set.kd
docker cp compiler:/app/a.out .
./a.out
```
