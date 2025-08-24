

#include <iostream>

extern "C" double putchard(double x) {
  putchar((char)x);
  return 0;
}

extern "C" {
double forTest(double, double);
}

int main() { std::cout << forTest(0, 255); }
