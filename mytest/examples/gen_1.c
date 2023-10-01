static int f1() {
    return 0;
}

int f2(int a, int b) {
    return 0;
}

int f3(int a, ...) {
    return 0;
}

int main() {
    f2(10, 20);
    f3(10, 20);
    return 0;
}
