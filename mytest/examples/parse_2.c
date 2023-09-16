// 将一个 int 类型按照 double 类型进行对齐
int _Alignas(double) foo;

// 将一个 int 类型按照 16 字节对齐
int _Alignas(16) bar;

int _Alignas(2) goo;

int main() {
    return 0;
}
