#include <stdio.h>

int main(void)
{
    int a, b;
    int result;

    b = 0x83;
    result = 0xffffff83;
    __asm
    ("l.extbs  %0, %1\n\t"
     : "=r"(a)
     : "r"(b)
    );
    if (a != result) {
        printf("extbs error\n");
    }

    result = 0x83;
    __asm
    ("l.extbz  %0, %1\n\t"
     : "=r"(a)
     : "r"(b)
    );
    if (a != result) {
        printf("extbz error\n");
    }

    b = 0x8083;
    result = 0xffff8083;
    __asm
    ("l.exths  %0, %1\n\t"
     : "=r"(a)
     : "r"(b)
    );
    if (a != result) {
        printf("exths error\n");
    }

    result = 0x8083;
    __asm
    ("l.exthz  %0, %1\n\t"
     : "=r"(a)
     : "r"(b)
    );
    if (a != result) {
        printf("exthz error\n");
    }

    b = 0x80830080;
    result = 0x80830080;
    __asm
    ("l.extws  %0, %1\n\t"
     : "=r"(a)
     : "r"(b)
    );
    if (a != result) {
        printf("extws error\n");
    }

    __asm
    ("l.extwz  %0, %1\n\t"
     : "=r"(a)
     : "r"(b)
    );
    if (a != result) {
        printf("extwz error\n");
    }

    return 0;
}
