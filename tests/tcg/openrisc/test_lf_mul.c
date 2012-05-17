#include <stdio.h>

int main(void)
{
    int a, b, c;
    int result;

    b = 0x420;
    c = 0x3000;
    result = 0xc60000;

    __asm
    ("lf.mul.d   %0, %1, %2\n\t"
     : "=r"(a)
     : "r"(b), "r"(c)
    );
    if (a != result) {
        printf("lf.mul.d error\n");
    }

    return 0;
}
