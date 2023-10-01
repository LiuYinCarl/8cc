struct Foo {
    int a;
    double b;
} Foo;

int func(struct Foo f) {
    return 0;
}

int main() {
    struct Foo foo;
    func(foo);
    return 0;
}
