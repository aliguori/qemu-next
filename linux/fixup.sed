# Header file fix ups.  This is tailored for importing the kvm linux headers

# Expand asm/ includes to avoid having to do symlink trickery
s:^#include <asm/\(.*\)>:                              \
#if defined(__x86_64__) || defined(__i386__)            \
#include "asm-x86/\1"                                   \
#elif defined(__powerpc__)                              \
#include "asm-powerpc/\1"                               \
#elif defined(__sparc__)                                \
#include "asm-sparc/\1"                                 \
#elif defined(__hppa__)                                 \
#include "asm-parisc/\1"                                \
#elif defined(__arm__)                                  \
#include "asm-arm/\1"                                   \
#else                                                   \
#error Using Linux headers on unknown architecture      \
#endif                                                  \
:g
s:^#include <linux/\(.*\)>:#include "linux/\1":g
