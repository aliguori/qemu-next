#include <stdio.h>

int main(void)
{
    int a, b, c;
    int result;

    a = 0x1;
    b = 0x122;
    c = 0x123;
    result = 0x3;
    __asm
    ("lfeqd:\n\t"
     "l.addi    %0, %0, 0x1\n\t"
     "lf.sfeq.s %1, %2\n\t"
     "l.bf      lfeqd\n\t"
     "l.nop\n\t"
     "l.addi    %0, %0, 0x1\n\t"
     : "+r"(a)
     : "r"(b), "r"(c)
    );
    if (a != result) {
        printf("lf.sfeq.s error\n");
    }

    b = 0x13;
    c = 0x13;
    result = 0x3;
    __asm
    ("lf.sfeq.s %1, %2\n\t"
     "l.bf      1f\n\t"
     "l.nop\n\t"
     "l.addi    r4, r4, 0x1\n\t"
     "1:\n\t"
     : "+r"(a)
     : "r"(b), "r"(c)
    );
    if (a != result) {
        printf("lf.sfeq.s error\n");
    }

    return 0;
}
