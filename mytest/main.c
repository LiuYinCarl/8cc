int func1(int a, int b) {
  int c = a + b;
  return c;
}

int func2() {
  int a = 10;
  int b = 20;
  int c = func1(a, b);
  return c;
}

int main() {
  int z = func2();
  return 0;
}
