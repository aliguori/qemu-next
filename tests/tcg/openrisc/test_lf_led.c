#include <stdio.h>

int main(void)
{
    int a, b, c;
    int result;

    a = 0;
    b = 0x1212;
    c = 0x123;
    result = 0x1;
    __asm
    ("lfled:\n\t"
     "l.addi    %0, %0, 0x1\n\t"
     "lf.sfle.d %1, %2\n\t"
     "l.bf      lfled\n\t"
     "l.nop\n\t"
     : "+r"(a)
     : "r"(b), "r"(c)
    );
    if (a != result) {
        printf("lf.sfle.d error\n");
    }

    b = 0x13;
    c = 0x113;
    result = 0x2;
    __asm
    ("l.addi    %0, %0, 0x1\n\t"
     "lf.sfle.d %1, %2\n\t"
     "l.bf      1f\n\t"
     "l.nop\n\t"
     "l.addi    %0, %0, 0x1\n\t"
     "1:\n\t"
     : "+r"(a)
     : "r"(b), "r"(c)
    );
    if (a != result) {
        printf("lf.sfle.d error\n");
    }

    return 0;
}
