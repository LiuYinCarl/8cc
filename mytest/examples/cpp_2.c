#define FUNCNAME foo
#define M1(arg1, arg2) FUNCNAME(arg1, arg2)

void foo(int a, int b) {}


int main() {
    M1(21, 31);
}
