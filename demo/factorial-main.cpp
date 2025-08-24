

#include <iostream>

extern "C" {
double forTest(double);
}

int main() { std::cout << forTest(5); }
