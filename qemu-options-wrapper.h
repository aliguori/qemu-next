
#if defined(QEMU_OPTIONS_GENERATE_ENUM)

#define DEF(option, opt_arg, opt_enum, opt_help, arch_mask)    \
    opt_enum,
#define GDEF(option, opt_arg, opt_enum, opt_help, arch_mask)    \
    DEF(option, opt_ag, opt_enum, opt_help, arch_mask)
#define DEFHEADING(text)
#define ARCHHEADING(text, arch_mask)

#elif defined(QEMU_OPTIONS_GENERATE_HELP)

#define DEF(option, opt_arg, opt_enum, opt_help, arch_mask)    \
    if ((arch_mask) & arch_type)                               \
        fputs(opt_help, stdout);
#define GDEF(option, opt_arg, opt_enum, opt_help, arch_mask)    \
    DEF(option, opt_ag, opt_enum, opt_help, arch_mask)

#define ARCHHEADING(text, arch_mask) \
    if ((arch_mask) & arch_type)    \
        puts(stringify(text));

#define DEFHEADING(text) ARCHHEADING(text, QEMU_ARCH_ALL)

#elif defined(QEMU_OPTIONS_GENERATE_OPTIONS)

#define DEF(option, opt_arg, opt_enum, opt_help, arch_mask)     \
    { option, opt_arg, opt_enum, arch_mask, false },
#define GDEF(option, opt_arg, opt_enum, opt_help, arch_mask)    \
    { option, opt_arg, opt_enum, arch_mask, true },
#define DEFHEADING(text)
#define ARCHHEADING(text, arch_mask)

#elif defined(QEMU_OPTIONS_GENERATE_QEMUOPTS)

#define DEF(option, opt_arg, opt_enum, opt_help, arch_mask) \
    { .name = option, .type = opt_arg ? QEMU_OPT_STRING : QEMU_OPT_BOOL, .help = opt_help },
#define GDEF(option, opt_arg, opt_enum, opt_help, arch_mask)    \
    DEF(option, opt_arg, opt_enum, opt_help, arch_mask)
#define DEFHEADING(text)
#define ARCHHEADING(text, arch_mask)

#else
#error "qemu-options-wrapper.h included with no option defined"
#endif

#include "qemu-options.def"

#undef DEF
#undef GDEF
#undef DEFHEADING
#undef ARCHHEADING
#undef GEN_DOCS

#undef QEMU_OPTIONS_GENERATE_ENUM
#undef QEMU_OPTIONS_GENERATE_HELP
#undef QEMU_OPTIONS_GENERATE_OPTIONS
#undef QEMU_OPTIONS_GENERATE_QEMUOPTS
