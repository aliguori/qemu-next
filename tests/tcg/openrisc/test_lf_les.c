#include <stdio.h>

int main(void)
{
    int a, b, c;
    int result;

    a = 0;
    b = 0x1022;
    c = 0x123;
    result = 0x1;
    __asm
    ("lfles:\n\t"
     "l.addi    %0, %0, 0x1\n\t"
     "lf.sfle.s %1, %2\n\t"
     "l.bf      lfles\n\t"
     "l.nop\n\t"
     : "+r"(a)
     : "r"(b), "r"(c)
    );
    if (a != result) {
        printf("lf.sfle.s error\n");
    }

    b = 0x1;
    c = 0x13;
    result = 0x3;
    __asm
    ("l.addi    %0, %0, 0x1\n\t"
     "l.addi    %0, %0, 0x1\n\t"
     "lf.sfle.s %1, %2\n\t"
     "l.bf      1f\n\t"
     "l.nop\n\t"
     "l.addi    %0, %0, 0x1\n\t"
     "l.addi    %0, %0, 0x1\n\t"
     "1:\n\t"
     : "+r"(a)
     : "r"(b), "r"(c)
    );
    if (a != result) {
        printf("lf.sfle.s error\n");
    }

    return 0;
}
