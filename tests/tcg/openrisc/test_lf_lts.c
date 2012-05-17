#include <stdio.h>

int main(void)
{
    int a, b, c;
    int result;

    a = 0;
    b = 0x122;
    c = 0x23;
    result = 0x1;

    __asm
    ("lfltd:\n\t"
     "l.addi    %0, %0, 0x1\n\t"
     "lf.sflt.s %1, %2\n\t"
     "l.bf      lfltd\n\t"
     "l.nop\n\t"
     : "+r"(a)
     : "r"(b), "r"(c)
    );
    if (a != result) {
        printf("lf.sflt.s error\n");
    }

    a = 0;
    b = 0x13;
    c = 0x123;
    result = 0x110;
    __asm
    ("1:\n\t"
     "l.addi    %1, %1, 0x1\n\t"
     "l.addi    %0, %0, 0x1\n\t"
     "lf.sflt.s %1, %2\n\t"
     "l.bf      1b\n\t"
     "l.nop\n\t"
     : "+r"(a)
     : "r"(b), "r"(c)
    );
    if (a != result) {
        printf("lf.sflt.s error\n");
    }

    return 0;
}
