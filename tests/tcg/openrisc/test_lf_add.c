#include <stdio.h>

int main(void)
{
    int a, b;
    int result;

    a = 0x1001234;
    b = 0x1101234;
    result = 0x2102468;

    __asm
    ("lf.add.d  %0, %0, %1\n\t"
     : "+r"(a)
     : "r"(b)
    );
    if (a != result) {
        printf("lf.add.d error, %x\n", a);
    }

    result = 0x320369c;
    __asm
    ("lf.add.s  %0, %0, %1\n\t"
     : "+r"(a)
     : "r"(b)
    );
    if (a != result) {
        printf("lf.add.s error, %x\n", a);
    }

    return 0;
}
