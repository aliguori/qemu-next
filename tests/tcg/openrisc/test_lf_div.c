#include <stdio.h>

int main(void)
{
    int a, b, c;
    int result;

    b = 0x80000000;
    c = 0x40;
    result = 0x2000000;
    __asm
    ("lf.div.d    %0, %1, %2\n\t"
     : "=r"(a)
     : "r"(b), "r"(c)
    );
    if (a != result) {
        printf("lf.div.d error\n");
    }

    b = 0x8000;
    c = 0x40;
    result = 0x200;
    __asm
    ("lf.div.s    %0, %1, %2\n\t"
     : "=r"(a)
     : "r"(b), "r"(c)
    );
    if (a != result) {
        printf("lf.div.s error\n");
    }

    return 0;
}
