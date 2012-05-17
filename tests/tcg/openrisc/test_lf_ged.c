#include <stdio.h>

int main(void)
{
    int a, b, c;
    int result;

    a = 0x1;
    b = 0x122;
    c = 0x123;
    result = 0x2;
    __asm
    ("lfged:\n\t"
     "l.addi    %0, %0, 0x1\n\t"
     "lf.sfge.d %1, %2\n\t"
     "l.bf      lfged\n\t"
     "l.nop\n\t"
     : "+r"(a)
     : "r"(b), "r"(c)
    );
    if (a != result) {
        printf("lf.sfge.d error\n");
    }

    b = 0x133;
    c = 0x13;
    result = 0x4;
    __asm
    ("lf.sfge.d %1, %2\n\t"
     "l.bf      1f\n\t"
     "l.nop\n\t"
     "l.addi    %0, %0, 0x1\n\t"
     "l.addi    %0, %0, 0x1\n\t"
     "1:\n\t"
     "l.addi    %0, %0, 0x1\n\t"
     "l.addi    %0, %0, 0x1\n\t"
     : "+r"(a)
     : "r"(b), "r"(c)
    );
    if (a != result) {
        printf("lf.sfge.d error\n");
    }

    return 0;
}
