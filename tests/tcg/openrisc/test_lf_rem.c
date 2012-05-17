#include <stdio.h>

int main(void)
{
    int a, b, c;
    int result;

    b = 0x1234;
    c = 0x1233;
    result = 0x1;

    __asm
    ("lf.rem.d      %0, %1, %2\n\t"
     : "=r"(a)
     : "r"(b), "r"(c)
    );
    if (a != result) {
        printf("lf.rem.d error\n");
    }

    __asm
    ("lf.rem.s      %0, %1, %2\n\t"
     : "=r"(a)
     : "r"(b), "r"(c)
    );
    if (a != result) {
        printf("lf.rem.s error\n");
    }

    return 0;
}
