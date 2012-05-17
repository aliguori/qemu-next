#include <stdio.h>

int main(void)
{
    int a, b, c;
    int result;

    a = 0;
    b = 0x122;
    c = 0x122;
    result = 0x1;
    __asm
    ("lfned:\n\t"
     "l.addi    %0, %0, 0x1\n\t"
     "lf.sfne.d %1, %2\n\t"
     "l.bf      lfned\n\t"
     "l.nop\n\t"
     : "+r"(a)
     : "r"(b), "r"(c)
    );
    if (a != result) {
        printf("lf.sfne.d error\n");
    }

    b = 0x13;
    c = 0x133;
    result = 0x3;

    __asm
    ("l.addi    %0, %0, 0x1\n\t"
     "l.addi    %0, %0, 0x1\n\t"
     "lf.sfne.d %1, %2\n\t"
     "l.bf      1f\n\t"
     "l.nop\n\t"
     "l.addi    r4, r4, 0x1\n\t"
     "l.addi    r4, r4, 0x1\n\t"
     "1:\n\t"
     : "+r"(a)
     : "r"(b), "r"(c)
    );
    if (a != result) {
        printf("lf.sfne.d error\n");
    }

    return 0;
}
