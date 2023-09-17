// K&R style function declaratioin
int func() {
    return 0;
}

int func2(a, b, c)
     int a;
     char b;
     double c;
{
    return 0;
}

int main() {
    func();
    func(1, 2, 'c', "d", 5.5);
    func2(10, 'a', 3.14);
}
