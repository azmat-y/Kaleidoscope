
#include <iostream>

extern "C" {
double fib(double);
}

int main() { std::cout << "8th fibonacci number is " << fib(8) << std::endl; }
