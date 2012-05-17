struct target_pt_regs {
    union {
        struct {
            /* Named registers */
            long  sr;   /* Stored in place of r0 */
             long  sp;   /* r1 */
         };
         struct {
             /* Old style */
             long offset[2];
             long gprs[30];
         };
         struct {
             /* New style */
             long gpr[32];
         };
     };
    long  pc;
    long  orig_gpr11;   /* For restarting system calls */
    long  syscallno;    /* Syscall number (used by strace) */
    long dummy;     /* Cheap alignment fix */
};

#define UNAME_MACHINE "openrisc"
