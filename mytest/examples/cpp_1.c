#include <stdio.h>

#define M1(fmt, ...) printf(fmt, __VA_ARGS__)
#define M2(fmt, args...) printf(fmt, args)

// if va_args is empty, ## can used to avoid compile mistack
#define M3(fmt, ...) printf(fmt, ## __VA_ARGS__)
#define M4(fmt, args...) printf(fmt, ## args)

int main() {
    M1("M1: %d %d\n", 10, 20);
    M2("M2: %d %d\n", 10, 20);

    M3("M3: \n");
    M4("M4: \n");
}
