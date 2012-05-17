#include <stdio.h>

int main(void)
{
    int a, b, c;
    int result;

    b = 0x999;
    c = 0x654;
    result = 0x345;

    __asm
    ("lf.sub.d  %0, %1, %2\n\t"
     : "=r"(a)
     : "r"(b), "r"(c)
    );
    if (a != result) {
        printf("lf.sub.d error\n");
    }

    __asm
    ("lf.sub.s  %0, %1, %2\n\t"
     : "=r"(a)
     : "r"(b), "r"(c)
    );
    if (a != result) {
        printf("lf.sub.s error\n");
    }

    return 0;
}
