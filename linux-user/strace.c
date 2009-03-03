#include <stdio.h>
#include <errno.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/select.h>
#include <sys/types.h>
#include <unistd.h>
#include "qemu.h"

int do_strace=0;

struct syscallname {
    int nr;
    const char *name;
    const char *format;
    void (*call)(const struct syscallname *,
                 abi_long, abi_long, abi_long,
                 abi_long, abi_long, abi_long);
    void (*result)(const struct syscallname *, abi_long);
};

/*
 * Utility functions
 */
static void
print_ipc_cmd(int cmd)
{
#define output_cmd(val) \
if( cmd == val ) { \
    gemu_log(#val); \
    return; \
}

    cmd &= 0xff;

    /* General IPC commands */
    output_cmd( IPC_RMID );
    output_cmd( IPC_SET );
    output_cmd( IPC_STAT );
    output_cmd( IPC_INFO );
    /* msgctl() commands */
    #ifdef __USER_MISC
    output_cmd( MSG_STAT );
    output_cmd( MSG_INFO );
    #endif
    /* shmctl() commands */
    output_cmd( SHM_LOCK );
    output_cmd( SHM_UNLOCK );
    output_cmd( SHM_STAT );
    output_cmd( SHM_INFO );
    /* semctl() commands */
    output_cmd( GETPID );
    output_cmd( GETVAL );
    output_cmd( GETALL );
    output_cmd( GETNCNT );
    output_cmd( GETZCNT );
    output_cmd( SETVAL );
    output_cmd( SETALL );
    output_cmd( SEM_STAT );
    output_cmd( SEM_INFO );
    output_cmd( IPC_RMID );
    output_cmd( IPC_RMID );
    output_cmd( IPC_RMID );
    output_cmd( IPC_RMID );
    output_cmd( IPC_RMID );
    output_cmd( IPC_RMID );
    output_cmd( IPC_RMID );
    output_cmd( IPC_RMID );
    output_cmd( IPC_RMID );

    /* Some value we don't recognize */
    gemu_log("%d",cmd);
}

#ifdef TARGET_NR__newselect
static void
print_fdset(int n, abi_ulong target_fds_addr)
{
    int i;

    gemu_log("[");
    if( target_fds_addr ) {
        abi_long *target_fds;

        target_fds = lock_user(VERIFY_READ,
                               target_fds_addr,
                               sizeof(*target_fds)*(n / TARGET_ABI_BITS + 1),
                               1);

        if (!target_fds)
            return;

        for (i=n; i>=0; i--) {
            if ((tswapl(target_fds[i / TARGET_ABI_BITS]) >> (i & (TARGET_ABI_BITS - 1))) & 1)
                gemu_log("%d,", i );
            }
        unlock_user(target_fds, target_fds_addr, 0);
    }
    gemu_log("]");
}

static void
print_timeval(abi_ulong tv_addr)
{
    if( tv_addr ) {
        struct target_timeval *tv;

        tv = lock_user(VERIFY_READ, tv_addr, sizeof(*tv), 1);
        if (!tv)
            return;
        gemu_log("{" TARGET_ABI_FMT_ld "," TARGET_ABI_FMT_ld "}",
        	 tv->tv_sec, tv->tv_usec);
        unlock_user(tv, tv_addr, 0);
    } else
        gemu_log("NULL");
}
#endif

/*
 * Sysycall specific output functions
 */

/* select */
#ifdef TARGET_NR__newselect
static long newselect_arg1 = 0;
static long newselect_arg2 = 0;
static long newselect_arg3 = 0;
static long newselect_arg4 = 0;
static long newselect_arg5 = 0;

static void
print_newselect(const struct syscallname *name,
                abi_long arg1, abi_long arg2, abi_long arg3,
                abi_long arg4, abi_long arg5, abi_long arg6)
{
    gemu_log("%s(" TARGET_ABI_FMT_ld ",", name->name, arg1);
    print_fdset(arg1, arg2);
    gemu_log(",");
    print_fdset(arg1, arg3);
    gemu_log(",");
    print_fdset(arg1, arg4);
    gemu_log(",");
    print_timeval(arg5);
    gemu_log(")");

    /* save for use in the return output function below */
    newselect_arg1=arg1;
    newselect_arg2=arg2;
    newselect_arg3=arg3;
    newselect_arg4=arg4;
    newselect_arg5=arg5;
}
#endif

#ifdef TARGET_NR_semctl
static void
print_semctl(const struct syscallname *name,
             abi_long arg1, abi_long arg2, abi_long arg3,
             abi_long arg4, abi_long arg5, abi_long arg6)
{
    gemu_log("%s(" TARGET_ABI_FMT_ld "," TARGET_ABI_FMT_ld ",", name->name, arg1, arg2);
    print_ipc_cmd(arg3);
    gemu_log(",0x" TARGET_ABI_FMT_lx ")", arg4);
}
#endif

static void
print_execve(const struct syscallname *name,
             abi_long arg1, abi_long arg2, abi_long arg3,
             abi_long arg4, abi_long arg5, abi_long arg6)
{
    abi_ulong arg_ptr_addr;
    char *s;

    if (!(s = lock_user_string(arg1)))
        return;
    gemu_log("%s(\"%s\",{", name->name, s);
    unlock_user(s, arg1, 0);

    for (arg_ptr_addr = arg2; ; arg_ptr_addr += sizeof(abi_ulong)) {
        abi_ulong *arg_ptr, arg_addr;

	arg_ptr = lock_user(VERIFY_READ, arg_ptr_addr, sizeof(abi_ulong), 1);
        if (!arg_ptr)
            return;
	arg_addr = tswapl(*arg_ptr);
	unlock_user(arg_ptr, arg_ptr_addr, 0);
        if (!arg_addr)
            break;
        if ((s = lock_user_string(arg_addr))) {
            gemu_log("\"%s\",", s);
            unlock_user(s, arg_addr, 0);
        }
    }

    gemu_log("NULL})");
}

#ifdef TARGET_NR_ipc
static void
print_ipc(const struct syscallname *name,
          abi_long arg1, abi_long arg2, abi_long arg3,
          abi_long arg4, abi_long arg5, abi_long arg6)
{
    switch(arg1) {
    case IPCOP_semctl:
        gemu_log("semctl(" TARGET_ABI_FMT_ld "," TARGET_ABI_FMT_ld ",", arg1, arg2);
        print_ipc_cmd(arg3);
        gemu_log(",0x" TARGET_ABI_FMT_lx ")", arg4);
        break;
    default:
        gemu_log("%s(" TARGET_ABI_FMT_ld "," TARGET_ABI_FMT_ld "," TARGET_ABI_FMT_ld "," TARGET_ABI_FMT_ld ")",
                 name->name, arg1, arg2, arg3, arg4);
    }
}
#endif

/*
 * Variants for the return value output function
 */

static void
print_syscall_ret_addr(const struct syscallname *name, abi_long ret)
{
if( ret == -1 ) {
        gemu_log(" = -1 errno=%d (%s)\n", errno, target_strerror(errno));
    } else {
        gemu_log(" = 0x" TARGET_ABI_FMT_lx "\n", ret);
    }
}

#if 0 /* currently unused */
static void
print_syscall_ret_raw(struct syscallname *name, abi_long ret)
{
        gemu_log(" = 0x" TARGET_ABI_FMT_lx "\n", ret);
}
#endif

#ifdef TARGET_NR__newselect
static void
print_syscall_ret_newselect(const struct syscallname *name, abi_long ret)
{
    gemu_log(" = 0x" TARGET_ABI_FMT_lx " (", ret);
    print_fdset(newselect_arg1,newselect_arg2);
    gemu_log(",");
    print_fdset(newselect_arg1,newselect_arg3);
    gemu_log(",");
    print_fdset(newselect_arg1,newselect_arg4);
    gemu_log(",");
    print_timeval(newselect_arg5);
    gemu_log(")\n");
}
#endif

#define LOCKED_ARG0	(1 << 0)
#define LOCKED_ARG1	(1 << 1)
#define LOCKED_ARG2	(1 << 2)
#define LOCKED_ARG3	(1 << 3)
#define LOCKED_ARG4	(1 << 4)
#define LOCKED_ARG5	(1 << 5)

struct args {
    abi_long	arg_guest;	/* guest argument */
    uintptr_t	arg_host;	/* host argument */
    int		arg_locked;	/* is this argument locked? */
};

/*
 * This function locks strings from guest memory and prints
 * strace output according to format specified in strace.list.
 *
 * First parameter specifies, which guest arguments should be
 * locked (LOCKED_ARG0 - LOCKED_ARG5).
 */
static void
print_locked(unsigned int locked, const struct syscallname *name,
    abi_long arg0, abi_long arg1, abi_long arg2,
    abi_long arg3, abi_long arg4, abi_long arg5)
{
    struct args args[6] = {
        { arg0, 0, (locked & LOCKED_ARG0) },
        { arg1, 0, (locked & LOCKED_ARG1) },
        { arg2, 0, (locked & LOCKED_ARG2) },
        { arg3, 0, (locked & LOCKED_ARG3) },
        { arg4, 0, (locked & LOCKED_ARG4) },
        { arg5, 0, (locked & LOCKED_ARG5) },
    };
    struct args *a;
    int i;

    for (i = 0; i < 6; i++) {
        a = &args[i];
        if (a->arg_locked) {
            a->arg_host = (uintptr_t)lock_user_string(a->arg_guest);
            if (a->arg_host == 0)
                goto out;
        } else {
            a->arg_host = (uintptr_t)a->arg_guest;
        }
    }

    /*
     * Now we can have all strings locked and converted into host
     * addresses.
     */
    gemu_log(name->format,
        name->name,
        args[0].arg_host,
        args[1].arg_host,
        args[2].arg_host,
        args[3].arg_host,
        args[4].arg_host,
        args[5].arg_host);

out:
    for (i = 0; i < 6; i++) {
        a = &args[i];
        if (a->arg_locked)
            unlock_user((void *)a->arg_host, a->arg_guest, 0);
    }
}

static void
print_1st_locked(const struct syscallname *name,
    abi_long arg0, abi_long arg1, abi_long arg2,
    abi_long arg3, abi_long arg4, abi_long arg5)
{
    print_locked(LOCKED_ARG0, name, arg0, arg1, arg2, arg3, arg4, arg5);
}

static void
print_2nd_locked(const struct syscallname *name,
    abi_long arg0, abi_long arg1, abi_long arg2,
    abi_long arg3, abi_long arg4, abi_long arg5)
{
    print_locked(LOCKED_ARG1, name, arg0, arg1, arg2, arg3, arg4, arg5);
}

static void
print_1st_and_2nd_locked(const struct syscallname *name,
    abi_long arg0, abi_long arg1, abi_long arg2,
    abi_long arg3, abi_long arg4, abi_long arg5)
{
    print_locked(LOCKED_ARG0 | LOCKED_ARG1, name, arg0, arg1, arg2,
        arg3, arg4, arg5);
}

static void
print_1st_and_3rd_locked(const struct syscallname *name,
    abi_long arg0, abi_long arg1, abi_long arg2,
    abi_long arg3, abi_long arg4, abi_long arg5)
{
    print_locked(LOCKED_ARG0 | LOCKED_ARG2, name, arg0, arg1, arg2,
        arg3, arg4, arg5);
}

static void
print_1st_2nd_and_3rd_locked(const struct syscallname *name,
    abi_long arg0, abi_long arg1, abi_long arg2,
    abi_long arg3, abi_long arg4, abi_long arg5)
{
    print_locked(LOCKED_ARG0 | LOCKED_ARG1 | LOCKED_ARG2, name,
        arg0, arg1, arg2, arg3, arg4, arg5);
}

static void
print_2nd_and_4th_locked(const struct syscallname *name,
    abi_long arg0, abi_long arg1, abi_long arg2,
    abi_long arg3, abi_long arg4, abi_long arg5)
{
    print_locked(LOCKED_ARG1 | LOCKED_ARG3, name, arg0, arg1, arg2,
        arg3, arg4, arg5);
}

/*
 * Here is list of syscalls that we support reading in (locking)
 * strings from guest addresses.  Every syscall that has "%s" in its
 * parameter list and doesn't have specific print function, should
 * be defined here.
 */
#define print_access	print_1st_locked
#define print_chdir	print_1st_locked
#define print_chmod	print_1st_locked
#define print_creat	print_1st_locked
#define print_execv	print_1st_locked
#define print_faccessat print_2nd_locked
#define print_fchmodat	print_2nd_locked
#define print_fchown	print_1st_locked
#define print_fchownat	print_2nd_locked
#define print_futimesat	print_2nd_locked
#define print_link	print_1st_and_2nd_locked
#define print_linkat	print_2nd_and_4th_locked
#define print_lstat	print_1st_locked
#define print_lstat64	print_1st_locked
#define print_mkdir	print_1st_locked
#define print_mkdirat	print_2nd_locked
#define print_mknod	print_1st_locked
#define print_mknodat	print_2nd_locked
#define print_mq_open	print_1st_locked
#define print_mq_unlink	print_1st_locked
#define print_fstatat64	print_2nd_locked
#define print_newfstatat print_2nd_locked
#define print_open	print_1st_locked
#define print_openat	print_2nd_locked
#define print_readlink	print_1st_locked
#define print_readlinkat print_2nd_locked
#define print_rename	print_1st_and_2nd_locked
#define print_renameat	print_2nd_and_4th_locked
#define print_stat	print_1st_locked
#define print_stat64	print_1st_locked
#define print_statfs	print_1st_locked
#define print_statfs64	print_1st_locked
#define print_symlink	print_1st_and_2nd_locked
#define print_symlinkat	print_1st_and_3rd_locked
#define print_umount	print_1st_2nd_and_3rd_locked
#define print_unlink	print_1st_locked
#define print_unlinkat	print_2nd_locked
#define print_utime	print_1st_locked
#define print_utimensat	print_2nd_locked

/*
 * An array of all of the syscalls we know about
 */

static const struct syscallname scnames[] = {
#include "strace.list"
};

static int nsyscalls = ARRAY_SIZE(scnames);

/*
 * The public interface to this module.
 */
void
print_syscall(int num,
              abi_long arg1, abi_long arg2, abi_long arg3,
              abi_long arg4, abi_long arg5, abi_long arg6)
{
    int i;
    const char *format="%s(" TARGET_ABI_FMT_ld "," TARGET_ABI_FMT_ld "," TARGET_ABI_FMT_ld "," TARGET_ABI_FMT_ld "," TARGET_ABI_FMT_ld "," TARGET_ABI_FMT_ld ")";

    gemu_log("%d ", getpid() );

    for(i=0;i<nsyscalls;i++)
        if( scnames[i].nr == num ) {
            if( scnames[i].call != NULL ) {
                scnames[i].call(&scnames[i],arg1,arg2,arg3,arg4,arg5,arg6);
            } else {
                /* XXX: this format system is broken because it uses
                   host types and host pointers for strings */
                /*
                 * It now works when it has print_xxx_locked function
                 * as its printing function.
                 */
                if( scnames[i].format != NULL )
                    format = scnames[i].format;
                gemu_log(format,scnames[i].name, arg1,arg2,arg3,arg4,arg5,arg6);
            }
            return;
        }
    gemu_log("Unknown syscall %d\n", num);
}


void
print_syscall_ret(int num, abi_long ret)
{
    int i;

    for(i=0;i<nsyscalls;i++)
        if( scnames[i].nr == num ) {
            if( scnames[i].result != NULL ) {
                scnames[i].result(&scnames[i],ret);
            } else {
                if( ret < 0 ) {
                    gemu_log(" = -1 errno=" TARGET_ABI_FMT_ld " (%s)\n", -ret, target_strerror(-ret));
                } else {
                    gemu_log(" = " TARGET_ABI_FMT_ld "\n", ret);
                }
            }
            break;
        }
}
