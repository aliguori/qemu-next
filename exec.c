/*
 *  virtual page mapping and translated block handling
 *
 *  Copyright (c) 2003 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */
#include "config.h"
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/types.h>
#include <sys/mman.h>
#endif

#include "qemu-common.h"
#include "cpu.h"
#include "tcg.h"
#include "hw/hw.h"
#include "hw/qdev.h"
#include "osdep.h"
#include "kvm.h"
#include "hw/xen.h"
#include "qemu-timer.h"
#include "memory.h"
#include "exec-memory.h"
#if defined(CONFIG_USER_ONLY)
#include <qemu.h>
#if defined(__FreeBSD__) || defined(__FreeBSD_kernel__)
#include <sys/param.h>
#if __FreeBSD_version >= 700104
#define HAVE_KINFO_GETVMMAP
#define sigqueue sigqueue_freebsd  /* avoid redefinition */
#include <sys/time.h>
#include <sys/proc.h>
#include <machine/profile.h>
#define _KERNEL
#include <sys/user.h>
#undef _KERNEL
#undef sigqueue
#include <libutil.h>
#endif
#endif
#else /* !CONFIG_USER_ONLY */
#include "xen-mapcache.h"
#include "trace.h"
#endif

//#define DEBUG_IOPORT
//#define DEBUG_SUBPAGE

#if !defined(CONFIG_USER_ONLY)
int phys_ram_fd;
static int in_migration;

RAMList ram_list = { .blocks = QLIST_HEAD_INITIALIZER(ram_list.blocks) };

static MemoryRegion *system_memory;
static MemoryRegion *system_io;

#endif

CPUState *first_cpu;
/* current CPU in the current thread. It is only valid inside
   cpu_exec() */
CPUState *cpu_single_env;
/* 0 = Do not count executed instructions.
   1 = Precise instruction counting.
   2 = Adaptive rate instruction counting.  */
int use_icount = 0;
/* Current instruction counter.  While executing translated code this may
   include some instructions that have not yet been executed.  */
int64_t qemu_icount;

/* The bits remaining after N lower levels of page tables.  */
#define P_L1_BITS_REM \
    ((TARGET_PHYS_ADDR_SPACE_BITS - TARGET_PAGE_BITS) % L2_BITS)

/* Size of the L1 page table.  Avoid silly small sizes.  */
#if P_L1_BITS_REM < 4
#define P_L1_BITS  (P_L1_BITS_REM + L2_BITS)
#else
#define P_L1_BITS  P_L1_BITS_REM
#endif

#define P_L1_SIZE  ((target_phys_addr_t)1 << P_L1_BITS)

#define P_L1_SHIFT (TARGET_PHYS_ADDR_SPACE_BITS - TARGET_PAGE_BITS - P_L1_BITS)

unsigned long qemu_real_host_page_size;
unsigned long qemu_host_page_bits;
unsigned long qemu_host_page_size;
unsigned long qemu_host_page_mask;

#if !defined(CONFIG_USER_ONLY)

/* This is a multi-level map on the physical address space.
   The bottom level has pointers to PhysPageDesc.  */
static void *l1_phys_map[P_L1_SIZE];

static void io_mem_init(void);
static void memory_map_init(void);

/* io memory support */
CPUWriteMemoryFunc *io_mem_write[IO_MEM_NB_ENTRIES][4];
CPUReadMemoryFunc *io_mem_read[IO_MEM_NB_ENTRIES][4];
void *io_mem_opaque[IO_MEM_NB_ENTRIES];
static char io_mem_used[IO_MEM_NB_ENTRIES];
int io_mem_watch;
#endif

/* log support */
#ifdef WIN32
static const char *logfilename = "qemu.log";
#else
static const char *logfilename = "/tmp/qemu.log";
#endif
FILE *logfile;
int loglevel;
static int log_append = 0;

volatile sig_atomic_t exit_request;

bool qemu_cpu_has_work(CPUState *env)
{
    return cpu_has_work(env);
}

#if !defined(CONFIG_USER_ONLY)
static PhysPageDesc *phys_page_find_alloc(target_phys_addr_t index, int alloc)
{
    PhysPageDesc *pd;
    void **lp;
    int i;

    /* Level 1.  Always allocated.  */
    lp = l1_phys_map + ((index >> P_L1_SHIFT) & (P_L1_SIZE - 1));

    /* Level 2..N-1.  */
    for (i = P_L1_SHIFT / L2_BITS - 1; i > 0; i--) {
        void **p = *lp;
        if (p == NULL) {
            if (!alloc) {
                return NULL;
            }
            *lp = p = g_malloc0(sizeof(void *) * L2_SIZE);
        }
        lp = p + ((index >> (i * L2_BITS)) & (L2_SIZE - 1));
    }

    pd = *lp;
    if (pd == NULL) {
        int i;

        if (!alloc) {
            return NULL;
        }

        *lp = pd = g_malloc(sizeof(PhysPageDesc) * L2_SIZE);

        for (i = 0; i < L2_SIZE; i++) {
            pd[i].phys_offset = IO_MEM_UNASSIGNED;
            pd[i].region_offset = (index + i) << TARGET_PAGE_BITS;
        }
    }

    return pd + (index & (L2_SIZE - 1));
}

PhysPageDesc *phys_page_find(target_phys_addr_t index)
{
    return phys_page_find_alloc(index, 0);
}
#endif

void cpu_exec_init_all(void)
{
#if !defined(CONFIG_USER_ONLY)
    memory_map_init();
    io_mem_init();
#endif
}

#if defined(CPU_SAVE_VERSION) && !defined(CONFIG_USER_ONLY)

static int cpu_common_post_load(void *opaque, int version_id)
{
    CPUState *env = opaque;

    /* 0x01 was CPU_INTERRUPT_EXIT. This line can be removed when the
       version_id is increased. */
    env->interrupt_request &= ~0x01;
    tlb_flush(env, 1);

    return 0;
}

static const VMStateDescription vmstate_cpu_common = {
    .name = "cpu_common",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .post_load = cpu_common_post_load,
    .fields      = (VMStateField []) {
        VMSTATE_UINT32(halted, CPUState),
        VMSTATE_UINT32(interrupt_request, CPUState),
        VMSTATE_END_OF_LIST()
    }
};
#endif

CPUState *qemu_get_cpu(int cpu)
{
    CPUState *env = first_cpu;

    while (env) {
        if (env->cpu_index == cpu)
            break;
        env = env->next_cpu;
    }

    return env;
}

void cpu_exec_init(CPUState *env)
{
    CPUState **penv;
    int cpu_index;

#if defined(CONFIG_USER_ONLY)
    cpu_list_lock();
#endif
    env->next_cpu = NULL;
    penv = &first_cpu;
    cpu_index = 0;
    while (*penv != NULL) {
        penv = &(*penv)->next_cpu;
        cpu_index++;
    }
    env->cpu_index = cpu_index;
    env->numa_node = 0;
    QTAILQ_INIT(&env->breakpoints);
    QTAILQ_INIT(&env->watchpoints);
#ifndef CONFIG_USER_ONLY
    env->thread_id = qemu_get_thread_id();
#endif
    *penv = env;
#if defined(CONFIG_USER_ONLY)
    cpu_list_unlock();
#endif
#if defined(CPU_SAVE_VERSION) && !defined(CONFIG_USER_ONLY)
    vmstate_register(NULL, cpu_index, &vmstate_cpu_common, env);
    register_savevm(NULL, "cpu", cpu_index, CPU_SAVE_VERSION,
                    cpu_save, cpu_load, env);
#endif
}

#if defined(CONFIG_USER_ONLY)
void cpu_watchpoint_remove_all(CPUState *env, int mask)

{
}

int cpu_watchpoint_insert(CPUState *env, target_ulong addr, target_ulong len,
                          int flags, CPUWatchpoint **watchpoint)
{
    return -ENOSYS;
}
#else
/* Add a watchpoint.  */
int cpu_watchpoint_insert(CPUState *env, target_ulong addr, target_ulong len,
                          int flags, CPUWatchpoint **watchpoint)
{
    target_ulong len_mask = ~(len - 1);
    CPUWatchpoint *wp;

    /* sanity checks: allow power-of-2 lengths, deny unaligned watchpoints */
    if ((len != 1 && len != 2 && len != 4 && len != 8) || (addr & ~len_mask)) {
        fprintf(stderr, "qemu: tried to set invalid watchpoint at "
                TARGET_FMT_lx ", len=" TARGET_FMT_lu "\n", addr, len);
        return -EINVAL;
    }
    wp = g_malloc(sizeof(*wp));

    wp->vaddr = addr;
    wp->len_mask = len_mask;
    wp->flags = flags;

    /* keep all GDB-injected watchpoints in front */
    if (flags & BP_GDB)
        QTAILQ_INSERT_HEAD(&env->watchpoints, wp, entry);
    else
        QTAILQ_INSERT_TAIL(&env->watchpoints, wp, entry);

    tlb_flush_page(env, addr);

    if (watchpoint)
        *watchpoint = wp;
    return 0;
}

/* Remove a specific watchpoint.  */
int cpu_watchpoint_remove(CPUState *env, target_ulong addr, target_ulong len,
                          int flags)
{
    target_ulong len_mask = ~(len - 1);
    CPUWatchpoint *wp;

    QTAILQ_FOREACH(wp, &env->watchpoints, entry) {
        if (addr == wp->vaddr && len_mask == wp->len_mask
                && flags == (wp->flags & ~BP_WATCHPOINT_HIT)) {
            cpu_watchpoint_remove_by_ref(env, wp);
            return 0;
        }
    }
    return -ENOENT;
}

/* Remove a specific watchpoint by reference.  */
void cpu_watchpoint_remove_by_ref(CPUState *env, CPUWatchpoint *watchpoint)
{
    QTAILQ_REMOVE(&env->watchpoints, watchpoint, entry);

    tlb_flush_page(env, watchpoint->vaddr);

    g_free(watchpoint);
}

/* Remove all matching watchpoints.  */
void cpu_watchpoint_remove_all(CPUState *env, int mask)
{
    CPUWatchpoint *wp, *next;

    QTAILQ_FOREACH_SAFE(wp, &env->watchpoints, entry, next) {
        if (wp->flags & mask)
            cpu_watchpoint_remove_by_ref(env, wp);
    }
}
#endif

#if defined(TARGET_HAS_ICE)
#if defined(CONFIG_USER_ONLY)
static void breakpoint_invalidate(CPUState *env, target_ulong pc)
{
    tb_invalidate_phys_page_range(pc, pc + 1, 0);
}
#else
static void breakpoint_invalidate(CPUState *env, target_ulong pc)
{
    target_phys_addr_t addr;
    target_ulong pd;
    ram_addr_t ram_addr;
    PhysPageDesc *p;

    addr = cpu_get_phys_page_debug(env, pc);
    p = phys_page_find(addr >> TARGET_PAGE_BITS);
    if (!p) {
        pd = IO_MEM_UNASSIGNED;
    } else {
        pd = p->phys_offset;
    }
    ram_addr = (pd & TARGET_PAGE_MASK) | (pc & ~TARGET_PAGE_MASK);
    tb_invalidate_phys_page_range(ram_addr, ram_addr + 1, 0);
}
#endif
#endif /* TARGET_HAS_ICE */

/* Add a breakpoint.  */
int cpu_breakpoint_insert(CPUState *env, target_ulong pc, int flags,
                          CPUBreakpoint **breakpoint)
{
#if defined(TARGET_HAS_ICE)
    CPUBreakpoint *bp;

    bp = g_malloc(sizeof(*bp));

    bp->pc = pc;
    bp->flags = flags;

    /* keep all GDB-injected breakpoints in front */
    if (flags & BP_GDB)
        QTAILQ_INSERT_HEAD(&env->breakpoints, bp, entry);
    else
        QTAILQ_INSERT_TAIL(&env->breakpoints, bp, entry);

    if (tcg_enabled()) {
        breakpoint_invalidate(env, pc);
    }

    if (breakpoint)
        *breakpoint = bp;
    return 0;
#else
    return -ENOSYS;
#endif
}

/* Remove a specific breakpoint.  */
int cpu_breakpoint_remove(CPUState *env, target_ulong pc, int flags)
{
#if defined(TARGET_HAS_ICE)
    CPUBreakpoint *bp;

    QTAILQ_FOREACH(bp, &env->breakpoints, entry) {
        if (bp->pc == pc && bp->flags == flags) {
            cpu_breakpoint_remove_by_ref(env, bp);
            return 0;
        }
    }
    return -ENOENT;
#else
    return -ENOSYS;
#endif
}

/* Remove a specific breakpoint by reference.  */
void cpu_breakpoint_remove_by_ref(CPUState *env, CPUBreakpoint *breakpoint)
{
#if defined(TARGET_HAS_ICE)
    QTAILQ_REMOVE(&env->breakpoints, breakpoint, entry);

    breakpoint_invalidate(env, breakpoint->pc);

    g_free(breakpoint);
#endif
}

/* Remove all matching breakpoints. */
void cpu_breakpoint_remove_all(CPUState *env, int mask)
{
#if defined(TARGET_HAS_ICE)
    CPUBreakpoint *bp, *next;

    QTAILQ_FOREACH_SAFE(bp, &env->breakpoints, entry, next) {
        if (bp->flags & mask)
            cpu_breakpoint_remove_by_ref(env, bp);
    }
#endif
}

/* enable or disable single step mode. EXCP_DEBUG is returned by the
   CPU loop after each instruction */
void cpu_single_step(CPUState *env, int enabled)
{
#if defined(TARGET_HAS_ICE)
    if (env->singlestep_enabled != enabled) {
        env->singlestep_enabled = enabled;
        if (kvm_enabled())
            kvm_update_guest_debug(env, 0);
        else {
            /* must flush all the translated code to avoid inconsistencies */
            /* XXX: only flush what is necessary */
            tb_flush(env);
        }
    }
#endif
}

/* enable or disable low levels log */
void cpu_set_log(int log_flags)
{
    loglevel = log_flags;
    if (loglevel && !logfile) {
        logfile = fopen(logfilename, log_append ? "a" : "w");
        if (!logfile) {
            perror(logfilename);
            _exit(1);
        }
#if !defined(CONFIG_SOFTMMU)
        /* must avoid mmap() usage of glibc by setting a buffer "by hand" */
        {
            static char logfile_buf[4096];
            setvbuf(logfile, logfile_buf, _IOLBF, sizeof(logfile_buf));
        }
#elif !defined(_WIN32)
        /* Win32 doesn't support line-buffering and requires size >= 2 */
        setvbuf(logfile, NULL, _IOLBF, 0);
#endif
        log_append = 1;
    }
    if (!loglevel && logfile) {
        fclose(logfile);
        logfile = NULL;
    }
}

void cpu_set_log_filename(const char *filename)
{
    logfilename = strdup(filename);
    if (logfile) {
        fclose(logfile);
        logfile = NULL;
    }
    cpu_set_log(loglevel);
}

void cpu_reset_interrupt(CPUState *env, int mask)
{
    env->interrupt_request &= ~mask;
}

void cpu_exit(CPUState *env)
{
    env->exit_request = 1;
    if (tcg_enabled()) {
        cpu_unlink_tb(env);
    }
}

const CPULogItem cpu_log_items[] = {
    { CPU_LOG_TB_OUT_ASM, "out_asm",
      "show generated host assembly code for each compiled TB" },
    { CPU_LOG_TB_IN_ASM, "in_asm",
      "show target assembly code for each compiled TB" },
    { CPU_LOG_TB_OP, "op",
      "show micro ops for each compiled TB" },
    { CPU_LOG_TB_OP_OPT, "op_opt",
      "show micro ops "
#ifdef TARGET_I386
      "before eflags optimization and "
#endif
      "after liveness analysis" },
    { CPU_LOG_INT, "int",
      "show interrupts/exceptions in short format" },
    { CPU_LOG_EXEC, "exec",
      "show trace before each executed TB (lots of logs)" },
    { CPU_LOG_TB_CPU, "cpu",
      "show CPU state before block translation" },
#ifdef TARGET_I386
    { CPU_LOG_PCALL, "pcall",
      "show protected mode far calls/returns/exceptions" },
    { CPU_LOG_RESET, "cpu_reset",
      "show CPU state before CPU resets" },
#endif
#ifdef DEBUG_IOPORT
    { CPU_LOG_IOPORT, "ioport",
      "show all i/o ports accesses" },
#endif
    { 0, NULL, NULL },
};

#ifndef CONFIG_USER_ONLY
static QLIST_HEAD(memory_client_list, CPUPhysMemoryClient) memory_client_list
    = QLIST_HEAD_INITIALIZER(memory_client_list);

static void cpu_notify_set_memory(target_phys_addr_t start_addr,
                                  ram_addr_t size,
                                  ram_addr_t phys_offset,
                                  bool log_dirty)
{
    CPUPhysMemoryClient *client;
    QLIST_FOREACH(client, &memory_client_list, list) {
        client->set_memory(client, start_addr, size, phys_offset, log_dirty);
    }
}

static int cpu_notify_sync_dirty_bitmap(target_phys_addr_t start,
                                        target_phys_addr_t end)
{
    CPUPhysMemoryClient *client;
    QLIST_FOREACH(client, &memory_client_list, list) {
        int r = client->sync_dirty_bitmap(client, start, end);
        if (r < 0)
            return r;
    }
    return 0;
}

static int cpu_notify_migration_log(int enable)
{
    CPUPhysMemoryClient *client;
    QLIST_FOREACH(client, &memory_client_list, list) {
        int r = client->migration_log(client, enable);
        if (r < 0)
            return r;
    }
    return 0;
}

struct last_map {
    target_phys_addr_t start_addr;
    ram_addr_t size;
    ram_addr_t phys_offset;
};

/* The l1_phys_map provides the upper P_L1_BITs of the guest physical
 * address.  Each intermediate table provides the next L2_BITs of guest
 * physical address space.  The number of levels vary based on host and
 * guest configuration, making it efficient to build the final guest
 * physical address by seeding the L1 offset and shifting and adding in
 * each L2 offset as we recurse through them. */
static void phys_page_for_each_1(CPUPhysMemoryClient *client, int level,
                                 void **lp, target_phys_addr_t addr,
                                 struct last_map *map)
{
    int i;

    if (*lp == NULL) {
        return;
    }
    if (level == 0) {
        PhysPageDesc *pd = *lp;
        addr <<= L2_BITS + TARGET_PAGE_BITS;
        for (i = 0; i < L2_SIZE; ++i) {
            if (pd[i].phys_offset != IO_MEM_UNASSIGNED) {
                target_phys_addr_t start_addr = addr | i << TARGET_PAGE_BITS;

                if (map->size &&
                    start_addr == map->start_addr + map->size &&
                    pd[i].phys_offset == map->phys_offset + map->size) {

                    map->size += TARGET_PAGE_SIZE;
                    continue;
                } else if (map->size) {
                    client->set_memory(client, map->start_addr,
                                       map->size, map->phys_offset, false);
                }

                map->start_addr = start_addr;
                map->size = TARGET_PAGE_SIZE;
                map->phys_offset = pd[i].phys_offset;
            }
        }
    } else {
        void **pp = *lp;
        for (i = 0; i < L2_SIZE; ++i) {
            phys_page_for_each_1(client, level - 1, pp + i,
                                 (addr << L2_BITS) | i, map);
        }
    }
}

static void phys_page_for_each(CPUPhysMemoryClient *client)
{
    int i;
    struct last_map map = { };

    for (i = 0; i < P_L1_SIZE; ++i) {
        phys_page_for_each_1(client, P_L1_SHIFT / L2_BITS - 1,
                             l1_phys_map + i, i, &map);
    }
    if (map.size) {
        client->set_memory(client, map.start_addr, map.size, map.phys_offset,
                           false);
    }
}

void cpu_register_phys_memory_client(CPUPhysMemoryClient *client)
{
    QLIST_INSERT_HEAD(&memory_client_list, client, list);
    phys_page_for_each(client);
}

void cpu_unregister_phys_memory_client(CPUPhysMemoryClient *client)
{
    QLIST_REMOVE(client, list);
}
#endif

static int cmp1(const char *s1, int n, const char *s2)
{
    if (strlen(s2) != n)
        return 0;
    return memcmp(s1, s2, n) == 0;
}

/* takes a comma separated list of log masks. Return 0 if error. */
int cpu_str_to_log_mask(const char *str)
{
    const CPULogItem *item;
    int mask;
    const char *p, *p1;

    p = str;
    mask = 0;
    for(;;) {
        p1 = strchr(p, ',');
        if (!p1)
            p1 = p + strlen(p);
        if(cmp1(p,p1-p,"all")) {
            for(item = cpu_log_items; item->mask != 0; item++) {
                mask |= item->mask;
            }
        } else {
            for(item = cpu_log_items; item->mask != 0; item++) {
                if (cmp1(p, p1 - p, item->name))
                    goto found;
            }
            return 0;
        }
    found:
        mask |= item->mask;
        if (*p1 != ',')
            break;
        p = p1 + 1;
    }
    return mask;
}

void cpu_abort(CPUState *env, const char *fmt, ...)
{
    va_list ap;
    va_list ap2;

    va_start(ap, fmt);
    va_copy(ap2, ap);
    fprintf(stderr, "qemu: fatal: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
#ifdef TARGET_I386
    cpu_dump_state(env, stderr, fprintf, X86_DUMP_FPU | X86_DUMP_CCOP);
#else
    cpu_dump_state(env, stderr, fprintf, 0);
#endif
    if (qemu_log_enabled()) {
        qemu_log("qemu: fatal: ");
        qemu_log_vprintf(fmt, ap2);
        qemu_log("\n");
#ifdef TARGET_I386
        log_cpu_state(env, X86_DUMP_FPU | X86_DUMP_CCOP);
#else
        log_cpu_state(env, 0);
#endif
        qemu_log_flush();
        qemu_log_close();
    }
    va_end(ap2);
    va_end(ap);
#if defined(CONFIG_USER_ONLY)
    {
        struct sigaction act;
        sigfillset(&act.sa_mask);
        act.sa_handler = SIG_DFL;
        sigaction(SIGABRT, &act, NULL);
    }
#endif
    abort();
}

CPUState *cpu_copy(CPUState *env)
{
    CPUState *new_env = cpu_init(env->cpu_model_str);
    CPUState *next_cpu = new_env->next_cpu;
    int cpu_index = new_env->cpu_index;
#if defined(TARGET_HAS_ICE)
    CPUBreakpoint *bp;
    CPUWatchpoint *wp;
#endif

    memcpy(new_env, env, sizeof(CPUState));

    /* Preserve chaining and index. */
    new_env->next_cpu = next_cpu;
    new_env->cpu_index = cpu_index;

    /* Clone all break/watchpoints.
       Note: Once we support ptrace with hw-debug register access, make sure
       BP_CPU break/watchpoints are handled correctly on clone. */
    QTAILQ_INIT(&env->breakpoints);
    QTAILQ_INIT(&env->watchpoints);
#if defined(TARGET_HAS_ICE)
    QTAILQ_FOREACH(bp, &env->breakpoints, entry) {
        cpu_breakpoint_insert(new_env, bp->pc, bp->flags, NULL);
    }
    QTAILQ_FOREACH(wp, &env->watchpoints, entry) {
        cpu_watchpoint_insert(new_env, wp->vaddr, (~wp->len_mask) + 1,
                              wp->flags, NULL);
    }
#endif

    return new_env;
}

/* Note: start and end must be within the same ram block.  */
void cpu_physical_memory_reset_dirty(ram_addr_t start, ram_addr_t end,
                                     int dirty_flags)
{
    CPUState *env;
    unsigned long length, start1;
    int i;

    start &= TARGET_PAGE_MASK;
    end = TARGET_PAGE_ALIGN(end);

    length = end - start;
    if (length == 0)
        return;
    cpu_physical_memory_mask_dirty_range(start, length, dirty_flags);

    /* we modify the TLB cache so that the dirty bit will be set again
       when accessing the range */
    start1 = (unsigned long)qemu_safe_ram_ptr(start);
    /* Check that we don't span multiple blocks - this breaks the
       address comparisons below.  */
    if ((unsigned long)qemu_safe_ram_ptr(end - 1) - start1
            != (end - 1) - start) {
        abort();
    }

    if (tcg_enabled()) {
        for(env = first_cpu; env != NULL; env = env->next_cpu) {
            int mmu_idx;
            for (mmu_idx = 0; mmu_idx < NB_MMU_MODES; mmu_idx++) {
                for(i = 0; i < CPU_TLB_SIZE; i++)
                    tlb_reset_dirty_range(&env->tlb_table[mmu_idx][i],
                                          start1, length);
            }
        }
    }
}

int cpu_physical_memory_set_dirty_tracking(int enable)
{
    int ret = 0;
    in_migration = enable;
    ret = cpu_notify_migration_log(!!enable);
    return ret;
}

int cpu_physical_memory_get_dirty_tracking(void)
{
    return in_migration;
}

int cpu_physical_sync_dirty_bitmap(target_phys_addr_t start_addr,
                                   target_phys_addr_t end_addr)
{
    int ret;

    ret = cpu_notify_sync_dirty_bitmap(start_addr, end_addr);
    return ret;
}

int cpu_physical_log_start(target_phys_addr_t start_addr,
                           ram_addr_t size)
{
    CPUPhysMemoryClient *client;
    QLIST_FOREACH(client, &memory_client_list, list) {
        if (client->log_start) {
            int r = client->log_start(client, start_addr, size);
            if (r < 0) {
                return r;
            }
        }
    }
    return 0;
}

int cpu_physical_log_stop(target_phys_addr_t start_addr,
                          ram_addr_t size)
{
    CPUPhysMemoryClient *client;
    QLIST_FOREACH(client, &memory_client_list, list) {
        if (client->log_stop) {
            int r = client->log_stop(client, start_addr, size);
            if (r < 0) {
                return r;
            }
        }
    }
    return 0;
}

#if !defined(CONFIG_USER_ONLY)

#define SUBPAGE_IDX(addr) ((addr) & ~TARGET_PAGE_MASK)
typedef struct subpage_t {
    target_phys_addr_t base;
    ram_addr_t sub_io_index[TARGET_PAGE_SIZE];
    ram_addr_t region_offset[TARGET_PAGE_SIZE];
} subpage_t;

static int subpage_register (subpage_t *mmio, uint32_t start, uint32_t end,
                             ram_addr_t memory, ram_addr_t region_offset);
static subpage_t *subpage_init (target_phys_addr_t base, ram_addr_t *phys,
                                ram_addr_t orig_memory,
                                ram_addr_t region_offset);
#define CHECK_SUBPAGE(addr, start_addr, start_addr2, end_addr, end_addr2, \
                      need_subpage)                                     \
    do {                                                                \
        if (addr > start_addr)                                          \
            start_addr2 = 0;                                            \
        else {                                                          \
            start_addr2 = start_addr & ~TARGET_PAGE_MASK;               \
            if (start_addr2 > 0)                                        \
                need_subpage = 1;                                       \
        }                                                               \
                                                                        \
        if ((start_addr + orig_size) - addr >= TARGET_PAGE_SIZE)        \
            end_addr2 = TARGET_PAGE_SIZE - 1;                           \
        else {                                                          \
            end_addr2 = (start_addr + orig_size - 1) & ~TARGET_PAGE_MASK; \
            if (end_addr2 < TARGET_PAGE_SIZE - 1)                       \
                need_subpage = 1;                                       \
        }                                                               \
    } while (0)

/* register physical memory.
   For RAM, 'size' must be a multiple of the target page size.
   If (phys_offset & ~TARGET_PAGE_MASK) != 0, then it is an
   io memory page.  The address used when calling the IO function is
   the offset from the start of the region, plus region_offset.  Both
   start_addr and region_offset are rounded down to a page boundary
   before calculating this offset.  This should not be a problem unless
   the low bits of start_addr and region_offset differ.  */
void cpu_register_physical_memory_log(target_phys_addr_t start_addr,
                                         ram_addr_t size,
                                         ram_addr_t phys_offset,
                                         ram_addr_t region_offset,
                                         bool log_dirty)
{
    target_phys_addr_t addr, end_addr;
    PhysPageDesc *p;
    CPUState *env;
    ram_addr_t orig_size = size;
    subpage_t *subpage;

    assert(size);
    cpu_notify_set_memory(start_addr, size, phys_offset, log_dirty);

    if (phys_offset == IO_MEM_UNASSIGNED) {
        region_offset = start_addr;
    }
    region_offset &= TARGET_PAGE_MASK;
    size = (size + TARGET_PAGE_SIZE - 1) & TARGET_PAGE_MASK;
    end_addr = start_addr + (target_phys_addr_t)size;

    addr = start_addr;
    do {
        p = phys_page_find(addr >> TARGET_PAGE_BITS);
        if (p && p->phys_offset != IO_MEM_UNASSIGNED) {
            ram_addr_t orig_memory = p->phys_offset;
            target_phys_addr_t start_addr2, end_addr2;
            int need_subpage = 0;

            CHECK_SUBPAGE(addr, start_addr, start_addr2, end_addr, end_addr2,
                          need_subpage);
            if (need_subpage) {
                if (!(orig_memory & IO_MEM_SUBPAGE)) {
                    subpage = subpage_init((addr & TARGET_PAGE_MASK),
                                           &p->phys_offset, orig_memory,
                                           p->region_offset);
                } else {
                    subpage = io_mem_opaque[(orig_memory & ~TARGET_PAGE_MASK)
                                            >> IO_MEM_SHIFT];
                }
                subpage_register(subpage, start_addr2, end_addr2, phys_offset,
                                 region_offset);
                p->region_offset = 0;
            } else {
                p->phys_offset = phys_offset;
                if ((phys_offset & ~TARGET_PAGE_MASK) <= IO_MEM_ROM ||
                    (phys_offset & IO_MEM_ROMD))
                    phys_offset += TARGET_PAGE_SIZE;
            }
        } else {
            p = phys_page_find_alloc(addr >> TARGET_PAGE_BITS, 1);
            p->phys_offset = phys_offset;
            p->region_offset = region_offset;
            if ((phys_offset & ~TARGET_PAGE_MASK) <= IO_MEM_ROM ||
                (phys_offset & IO_MEM_ROMD)) {
                phys_offset += TARGET_PAGE_SIZE;
            } else {
                target_phys_addr_t start_addr2, end_addr2;
                int need_subpage = 0;

                CHECK_SUBPAGE(addr, start_addr, start_addr2, end_addr,
                              end_addr2, need_subpage);

                if (need_subpage) {
                    subpage = subpage_init((addr & TARGET_PAGE_MASK),
                                           &p->phys_offset, IO_MEM_UNASSIGNED,
                                           addr & TARGET_PAGE_MASK);
                    subpage_register(subpage, start_addr2, end_addr2,
                                     phys_offset, region_offset);
                    p->region_offset = 0;
                }
            }
        }
        region_offset += TARGET_PAGE_SIZE;
        addr += TARGET_PAGE_SIZE;
    } while (addr != end_addr);

    /* since each CPU stores ram addresses in its TLB cache, we must
       reset the modified entries */
    /* XXX: slow ! */
    for(env = first_cpu; env != NULL; env = env->next_cpu) {
        tlb_flush(env, 1);
    }
}

/* XXX: temporary until new memory mapping API */
ram_addr_t cpu_get_physical_page_desc(target_phys_addr_t addr)
{
    PhysPageDesc *p;

    p = phys_page_find(addr >> TARGET_PAGE_BITS);
    if (!p)
        return IO_MEM_UNASSIGNED;
    return p->phys_offset;
}

void qemu_register_coalesced_mmio(target_phys_addr_t addr, ram_addr_t size)
{
    if (kvm_enabled())
        kvm_coalesce_mmio_region(addr, size);
}

void qemu_unregister_coalesced_mmio(target_phys_addr_t addr, ram_addr_t size)
{
    if (kvm_enabled())
        kvm_uncoalesce_mmio_region(addr, size);
}

void qemu_flush_coalesced_mmio_buffer(void)
{
    if (kvm_enabled())
        kvm_flush_coalesced_mmio_buffer();
}

#if defined(__linux__) && !defined(TARGET_S390X)

#include <sys/vfs.h>

#define HUGETLBFS_MAGIC       0x958458f6

static long gethugepagesize(const char *path)
{
    struct statfs fs;
    int ret;

    do {
        ret = statfs(path, &fs);
    } while (ret != 0 && errno == EINTR);

    if (ret != 0) {
        perror(path);
        return 0;
    }

    if (fs.f_type != HUGETLBFS_MAGIC)
        fprintf(stderr, "Warning: path not on HugeTLBFS: %s\n", path);

    return fs.f_bsize;
}

static void *file_ram_alloc(RAMBlock *block,
                            ram_addr_t memory,
                            const char *path)
{
    char *filename;
    void *area;
    int fd;
#ifdef MAP_POPULATE
    int flags;
#endif
    unsigned long hpagesize;

    hpagesize = gethugepagesize(path);
    if (!hpagesize) {
        return NULL;
    }

    if (memory < hpagesize) {
        return NULL;
    }

    if (kvm_enabled() && !kvm_has_sync_mmu()) {
        fprintf(stderr, "host lacks kvm mmu notifiers, -mem-path unsupported\n");
        return NULL;
    }

    if (asprintf(&filename, "%s/qemu_back_mem.XXXXXX", path) == -1) {
        return NULL;
    }

    fd = mkstemp(filename);
    if (fd < 0) {
        perror("unable to create backing store for hugepages");
        free(filename);
        return NULL;
    }
    unlink(filename);
    free(filename);

    memory = (memory+hpagesize-1) & ~(hpagesize-1);

    /*
     * ftruncate is not supported by hugetlbfs in older
     * hosts, so don't bother bailing out on errors.
     * If anything goes wrong with it under other filesystems,
     * mmap will fail.
     */
    if (ftruncate(fd, memory))
        perror("ftruncate");

#ifdef MAP_POPULATE
    /* NB: MAP_POPULATE won't exhaustively alloc all phys pages in the case
     * MAP_PRIVATE is requested.  For mem_prealloc we mmap as MAP_SHARED
     * to sidestep this quirk.
     */
    flags = mem_prealloc ? MAP_POPULATE | MAP_SHARED : MAP_PRIVATE;
    area = mmap(0, memory, PROT_READ | PROT_WRITE, flags, fd, 0);
#else
    area = mmap(0, memory, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
#endif
    if (area == MAP_FAILED) {
        perror("file_ram_alloc: can't mmap RAM pages");
        close(fd);
        return (NULL);
    }
    block->fd = fd;
    return area;
}
#endif

static ram_addr_t find_ram_offset(ram_addr_t size)
{
    RAMBlock *block, *next_block;
    ram_addr_t offset = 0, mingap = RAM_ADDR_MAX;

    if (QLIST_EMPTY(&ram_list.blocks))
        return 0;

    QLIST_FOREACH(block, &ram_list.blocks, next) {
        ram_addr_t end, next = RAM_ADDR_MAX;

        end = block->offset + block->length;

        QLIST_FOREACH(next_block, &ram_list.blocks, next) {
            if (next_block->offset >= end) {
                next = MIN(next, next_block->offset);
            }
        }
        if (next - end >= size && next - end < mingap) {
            offset =  end;
            mingap = next - end;
        }
    }
    return offset;
}

static ram_addr_t last_ram_offset(void)
{
    RAMBlock *block;
    ram_addr_t last = 0;

    QLIST_FOREACH(block, &ram_list.blocks, next)
        last = MAX(last, block->offset + block->length);

    return last;
}

ram_addr_t qemu_ram_alloc_from_ptr(DeviceState *dev, const char *name,
                                   ram_addr_t size, void *host)
{
    RAMBlock *new_block, *block;

    size = TARGET_PAGE_ALIGN(size);
    new_block = g_malloc0(sizeof(*new_block));

    if (dev && dev->parent_bus && dev->parent_bus->info->get_dev_path) {
        char *id = dev->parent_bus->info->get_dev_path(dev);
        if (id) {
            snprintf(new_block->idstr, sizeof(new_block->idstr), "%s/", id);
            g_free(id);
        }
    }
    pstrcat(new_block->idstr, sizeof(new_block->idstr), name);

    QLIST_FOREACH(block, &ram_list.blocks, next) {
        if (!strcmp(block->idstr, new_block->idstr)) {
            fprintf(stderr, "RAMBlock \"%s\" already registered, abort!\n",
                    new_block->idstr);
            abort();
        }
    }

    new_block->offset = find_ram_offset(size);
    if (host) {
        new_block->host = host;
        new_block->flags |= RAM_PREALLOC_MASK;
    } else {
        if (mem_path) {
#if defined (__linux__) && !defined(TARGET_S390X)
            new_block->host = file_ram_alloc(new_block, size, mem_path);
            if (!new_block->host) {
                new_block->host = qemu_vmalloc(size);
                qemu_madvise(new_block->host, size, QEMU_MADV_MERGEABLE);
            }
#else
            fprintf(stderr, "-mem-path option unsupported\n");
            exit(1);
#endif
        } else {
#if defined(TARGET_S390X) && defined(CONFIG_KVM)
            /* S390 KVM requires the topmost vma of the RAM to be smaller than
               an system defined value, which is at least 256GB. Larger systems
               have larger values. We put the guest between the end of data
               segment (system break) and this value. We use 32GB as a base to
               have enough room for the system break to grow. */
            new_block->host = mmap((void*)0x800000000, size,
                                   PROT_EXEC|PROT_READ|PROT_WRITE,
                                   MAP_SHARED | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
            if (new_block->host == MAP_FAILED) {
                fprintf(stderr, "Allocating RAM failed\n");
                abort();
            }
#else
            if (xen_enabled()) {
                xen_ram_alloc(new_block->offset, size);
            } else {
                new_block->host = qemu_vmalloc(size);
            }
#endif
            qemu_madvise(new_block->host, size, QEMU_MADV_MERGEABLE);
        }
    }
    new_block->length = size;

    QLIST_INSERT_HEAD(&ram_list.blocks, new_block, next);

    ram_list.phys_dirty = g_realloc(ram_list.phys_dirty,
                                       last_ram_offset() >> TARGET_PAGE_BITS);
    memset(ram_list.phys_dirty + (new_block->offset >> TARGET_PAGE_BITS),
           0xff, size >> TARGET_PAGE_BITS);

    if (kvm_enabled())
        kvm_setup_guest_memory(new_block->host, size);

    return new_block->offset;
}

ram_addr_t qemu_ram_alloc(DeviceState *dev, const char *name, ram_addr_t size)
{
    return qemu_ram_alloc_from_ptr(dev, name, size, NULL);
}

void qemu_ram_free_from_ptr(ram_addr_t addr)
{
    RAMBlock *block;

    QLIST_FOREACH(block, &ram_list.blocks, next) {
        if (addr == block->offset) {
            QLIST_REMOVE(block, next);
            g_free(block);
            return;
        }
    }
}

void qemu_ram_free(ram_addr_t addr)
{
    RAMBlock *block;

    QLIST_FOREACH(block, &ram_list.blocks, next) {
        if (addr == block->offset) {
            QLIST_REMOVE(block, next);
            if (block->flags & RAM_PREALLOC_MASK) {
                ;
            } else if (mem_path) {
#if defined (__linux__) && !defined(TARGET_S390X)
                if (block->fd) {
                    munmap(block->host, block->length);
                    close(block->fd);
                } else {
                    qemu_vfree(block->host);
                }
#else
                abort();
#endif
            } else {
#if defined(TARGET_S390X) && defined(CONFIG_KVM)
                munmap(block->host, block->length);
#else
                if (xen_enabled()) {
                    xen_invalidate_map_cache_entry(block->host);
                } else {
                    qemu_vfree(block->host);
                }
#endif
            }
            g_free(block);
            return;
        }
    }

}

#ifndef _WIN32
void qemu_ram_remap(ram_addr_t addr, ram_addr_t length)
{
    RAMBlock *block;
    ram_addr_t offset;
    int flags;
    void *area, *vaddr;

    QLIST_FOREACH(block, &ram_list.blocks, next) {
        offset = addr - block->offset;
        if (offset < block->length) {
            vaddr = block->host + offset;
            if (block->flags & RAM_PREALLOC_MASK) {
                ;
            } else {
                flags = MAP_FIXED;
                munmap(vaddr, length);
                if (mem_path) {
#if defined(__linux__) && !defined(TARGET_S390X)
                    if (block->fd) {
#ifdef MAP_POPULATE
                        flags |= mem_prealloc ? MAP_POPULATE | MAP_SHARED :
                            MAP_PRIVATE;
#else
                        flags |= MAP_PRIVATE;
#endif
                        area = mmap(vaddr, length, PROT_READ | PROT_WRITE,
                                    flags, block->fd, offset);
                    } else {
                        flags |= MAP_PRIVATE | MAP_ANONYMOUS;
                        area = mmap(vaddr, length, PROT_READ | PROT_WRITE,
                                    flags, -1, 0);
                    }
#else
                    abort();
#endif
                } else {
#if defined(TARGET_S390X) && defined(CONFIG_KVM)
                    flags |= MAP_SHARED | MAP_ANONYMOUS;
                    area = mmap(vaddr, length, PROT_EXEC|PROT_READ|PROT_WRITE,
                                flags, -1, 0);
#else
                    flags |= MAP_PRIVATE | MAP_ANONYMOUS;
                    area = mmap(vaddr, length, PROT_READ | PROT_WRITE,
                                flags, -1, 0);
#endif
                }
                if (area != vaddr) {
                    fprintf(stderr, "Could not remap addr: "
                            RAM_ADDR_FMT "@" RAM_ADDR_FMT "\n",
                            length, addr);
                    exit(1);
                }
                qemu_madvise(vaddr, length, QEMU_MADV_MERGEABLE);
            }
            return;
        }
    }
}
#endif /* !_WIN32 */

/* Return a host pointer to ram allocated with qemu_ram_alloc.
   With the exception of the softmmu code in this file, this should
   only be used for local memory (e.g. video ram) that the device owns,
   and knows it isn't going to access beyond the end of the block.

   It should not be used for general purpose DMA.
   Use cpu_physical_memory_map/cpu_physical_memory_rw instead.
 */
void *qemu_get_ram_ptr(ram_addr_t addr)
{
    RAMBlock *block;

    QLIST_FOREACH(block, &ram_list.blocks, next) {
        if (addr - block->offset < block->length) {
            /* Move this entry to to start of the list.  */
            if (block != QLIST_FIRST(&ram_list.blocks)) {
                QLIST_REMOVE(block, next);
                QLIST_INSERT_HEAD(&ram_list.blocks, block, next);
            }
            if (xen_enabled()) {
                /* We need to check if the requested address is in the RAM
                 * because we don't want to map the entire memory in QEMU.
                 * In that case just map until the end of the page.
                 */
                if (block->offset == 0) {
                    return xen_map_cache(addr, 0, 0);
                } else if (block->host == NULL) {
                    block->host =
                        xen_map_cache(block->offset, block->length, 1);
                }
            }
            return block->host + (addr - block->offset);
        }
    }

    fprintf(stderr, "Bad ram offset %" PRIx64 "\n", (uint64_t)addr);
    abort();

    return NULL;
}

/* Return a host pointer to ram allocated with qemu_ram_alloc.
 * Same as qemu_get_ram_ptr but avoid reordering ramblocks.
 */
void *qemu_safe_ram_ptr(ram_addr_t addr)
{
    RAMBlock *block;

    QLIST_FOREACH(block, &ram_list.blocks, next) {
        if (addr - block->offset < block->length) {
            if (xen_enabled()) {
                /* We need to check if the requested address is in the RAM
                 * because we don't want to map the entire memory in QEMU.
                 * In that case just map until the end of the page.
                 */
                if (block->offset == 0) {
                    return xen_map_cache(addr, 0, 0);
                } else if (block->host == NULL) {
                    block->host =
                        xen_map_cache(block->offset, block->length, 1);
                }
            }
            return block->host + (addr - block->offset);
        }
    }

    fprintf(stderr, "Bad ram offset %" PRIx64 "\n", (uint64_t)addr);
    abort();

    return NULL;
}

/* Return a host pointer to guest's ram. Similar to qemu_get_ram_ptr
 * but takes a size argument */
void *qemu_ram_ptr_length(ram_addr_t addr, ram_addr_t *size)
{
    if (*size == 0) {
        return NULL;
    }
    if (xen_enabled()) {
        return xen_map_cache(addr, *size, 1);
    } else {
        RAMBlock *block;

        QLIST_FOREACH(block, &ram_list.blocks, next) {
            if (addr - block->offset < block->length) {
                if (addr - block->offset + *size > block->length)
                    *size = block->length - addr + block->offset;
                return block->host + (addr - block->offset);
            }
        }

        fprintf(stderr, "Bad ram offset %" PRIx64 "\n", (uint64_t)addr);
        abort();
    }
}

void qemu_put_ram_ptr(void *addr)
{
    trace_qemu_put_ram_ptr(addr);
}

int qemu_ram_addr_from_host(void *ptr, ram_addr_t *ram_addr)
{
    RAMBlock *block;
    uint8_t *host = ptr;

    if (xen_enabled()) {
        *ram_addr = xen_ram_addr_from_mapcache(ptr);
        return 0;
    }

    QLIST_FOREACH(block, &ram_list.blocks, next) {
        /* This case append when the block is not mapped. */
        if (block->host == NULL) {
            continue;
        }
        if (host - block->host < block->length) {
            *ram_addr = block->offset + (host - block->host);
            return 0;
        }
    }

    return -1;
}

/* Some of the softmmu routines need to translate from a host pointer
   (typically a TLB entry) back to a ram offset.  */
ram_addr_t qemu_ram_addr_from_host_nofail(void *ptr)
{
    ram_addr_t ram_addr;

    if (qemu_ram_addr_from_host(ptr, &ram_addr)) {
        fprintf(stderr, "Bad ram pointer %p\n", ptr);
        abort();
    }
    return ram_addr;
}

static uint32_t unassigned_mem_readb(void *opaque, target_phys_addr_t addr)
{
#ifdef DEBUG_UNASSIGNED
    printf("Unassigned mem read " TARGET_FMT_plx "\n", addr);
#endif
#if defined(TARGET_ALPHA) || defined(TARGET_SPARC) || defined(TARGET_MICROBLAZE)
    cpu_unassigned_access(cpu_single_env, addr, 0, 0, 0, 1);
#endif
    return 0;
}

static uint32_t unassigned_mem_readw(void *opaque, target_phys_addr_t addr)
{
#ifdef DEBUG_UNASSIGNED
    printf("Unassigned mem read " TARGET_FMT_plx "\n", addr);
#endif
#if defined(TARGET_ALPHA) || defined(TARGET_SPARC) || defined(TARGET_MICROBLAZE)
    cpu_unassigned_access(cpu_single_env, addr, 0, 0, 0, 2);
#endif
    return 0;
}

static uint32_t unassigned_mem_readl(void *opaque, target_phys_addr_t addr)
{
#ifdef DEBUG_UNASSIGNED
    printf("Unassigned mem read " TARGET_FMT_plx "\n", addr);
#endif
#if defined(TARGET_ALPHA) || defined(TARGET_SPARC) || defined(TARGET_MICROBLAZE)
    cpu_unassigned_access(cpu_single_env, addr, 0, 0, 0, 4);
#endif
    return 0;
}

static void unassigned_mem_writeb(void *opaque, target_phys_addr_t addr, uint32_t val)
{
#ifdef DEBUG_UNASSIGNED
    printf("Unassigned mem write " TARGET_FMT_plx " = 0x%x\n", addr, val);
#endif
#if defined(TARGET_ALPHA) || defined(TARGET_SPARC) || defined(TARGET_MICROBLAZE)
    cpu_unassigned_access(cpu_single_env, addr, 1, 0, 0, 1);
#endif
}

static void unassigned_mem_writew(void *opaque, target_phys_addr_t addr, uint32_t val)
{
#ifdef DEBUG_UNASSIGNED
    printf("Unassigned mem write " TARGET_FMT_plx " = 0x%x\n", addr, val);
#endif
#if defined(TARGET_ALPHA) || defined(TARGET_SPARC) || defined(TARGET_MICROBLAZE)
    cpu_unassigned_access(cpu_single_env, addr, 1, 0, 0, 2);
#endif
}

static void unassigned_mem_writel(void *opaque, target_phys_addr_t addr, uint32_t val)
{
#ifdef DEBUG_UNASSIGNED
    printf("Unassigned mem write " TARGET_FMT_plx " = 0x%x\n", addr, val);
#endif
#if defined(TARGET_ALPHA) || defined(TARGET_SPARC) || defined(TARGET_MICROBLAZE)
    cpu_unassigned_access(cpu_single_env, addr, 1, 0, 0, 4);
#endif
}

static CPUReadMemoryFunc * const unassigned_mem_read[3] = {
    unassigned_mem_readb,
    unassigned_mem_readw,
    unassigned_mem_readl,
};

static CPUWriteMemoryFunc * const unassigned_mem_write[3] = {
    unassigned_mem_writeb,
    unassigned_mem_writew,
    unassigned_mem_writel,
};

static void notdirty_mem_writeb(void *opaque, target_phys_addr_t ram_addr,
                                uint32_t val)
{
    int dirty_flags;
    dirty_flags = cpu_physical_memory_get_dirty_flags(ram_addr);
    if (!(dirty_flags & CODE_DIRTY_FLAG)) {
#if !defined(CONFIG_USER_ONLY)
        if (tcg_enabled()) {
            tb_invalidate_phys_page_fast(ram_addr, 1);
        }
        dirty_flags = cpu_physical_memory_get_dirty_flags(ram_addr);
#endif
    }
    stb_p(qemu_get_ram_ptr(ram_addr), val);
    dirty_flags |= (0xff & ~CODE_DIRTY_FLAG);
    cpu_physical_memory_set_dirty_flags(ram_addr, dirty_flags);
    /* we remove the notdirty callback only if the code has been
       flushed */
    if (dirty_flags == 0xff && tcg_enabled()) {
        tlb_set_dirty(cpu_single_env, cpu_single_env->mem_io_vaddr);
    }
}

static void notdirty_mem_writew(void *opaque, target_phys_addr_t ram_addr,
                                uint32_t val)
{
    int dirty_flags;
    dirty_flags = cpu_physical_memory_get_dirty_flags(ram_addr);
    if (!(dirty_flags & CODE_DIRTY_FLAG)) {
#if !defined(CONFIG_USER_ONLY)
        if (tcg_enabled()) {
            tb_invalidate_phys_page_fast(ram_addr, 2);
        }
        dirty_flags = cpu_physical_memory_get_dirty_flags(ram_addr);
#endif
    }
    stw_p(qemu_get_ram_ptr(ram_addr), val);
    dirty_flags |= (0xff & ~CODE_DIRTY_FLAG);
    cpu_physical_memory_set_dirty_flags(ram_addr, dirty_flags);
    /* we remove the notdirty callback only if the code has been
       flushed */
    if (dirty_flags == 0xff && tcg_enabled()) {
        tlb_set_dirty(cpu_single_env, cpu_single_env->mem_io_vaddr);
    }
}

static void notdirty_mem_writel(void *opaque, target_phys_addr_t ram_addr,
                                uint32_t val)
{
    int dirty_flags;
    dirty_flags = cpu_physical_memory_get_dirty_flags(ram_addr);
    if (!(dirty_flags & CODE_DIRTY_FLAG)) {
#if !defined(CONFIG_USER_ONLY)
        if (tcg_enabled()) {
            tb_invalidate_phys_page_fast(ram_addr, 4);
        }
        dirty_flags = cpu_physical_memory_get_dirty_flags(ram_addr);
#endif
    }
    stl_p(qemu_get_ram_ptr(ram_addr), val);
    dirty_flags |= (0xff & ~CODE_DIRTY_FLAG);
    cpu_physical_memory_set_dirty_flags(ram_addr, dirty_flags);
    /* we remove the notdirty callback only if the code has been
       flushed */
    if (dirty_flags == 0xff && tcg_enabled()) {
        tlb_set_dirty(cpu_single_env, cpu_single_env->mem_io_vaddr);
    }
}

static CPUReadMemoryFunc * const error_mem_read[3] = {
    NULL, /* never used */
    NULL, /* never used */
    NULL, /* never used */
};

static CPUWriteMemoryFunc * const notdirty_mem_write[3] = {
    notdirty_mem_writeb,
    notdirty_mem_writew,
    notdirty_mem_writel,
};

/* Generate a debug exception if a watchpoint has been hit.  */
static void check_watchpoint(int offset, int len_mask, int flags)
{
    CPUState *env = cpu_single_env;
    target_ulong pc, cs_base;
    TranslationBlock *tb;
    target_ulong vaddr;
    CPUWatchpoint *wp;
    int cpu_flags;

    if (env->watchpoint_hit) {
        /* We re-entered the check after replacing the TB. Now raise
         * the debug interrupt so that is will trigger after the
         * current instruction. */
        cpu_interrupt(env, CPU_INTERRUPT_DEBUG);
        return;
    }
    vaddr = (env->mem_io_vaddr & TARGET_PAGE_MASK) + offset;
    QTAILQ_FOREACH(wp, &env->watchpoints, entry) {
        if ((vaddr == (wp->vaddr & len_mask) ||
             (vaddr & wp->len_mask) == wp->vaddr) && (wp->flags & flags)) {
            wp->flags |= BP_WATCHPOINT_HIT;
            if (!env->watchpoint_hit) {
                env->watchpoint_hit = wp;
                tb = tb_find_pc(env->mem_io_pc);
                if (!tb) {
                    cpu_abort(env, "check_watchpoint: could not find TB for "
                              "pc=%p", (void *)env->mem_io_pc);
                }
                if (tcg_enabled()) {
                    cpu_restore_state(tb, env, env->mem_io_pc);
                }
                tb_phys_invalidate(tb, -1);
                if (wp->flags & BP_STOP_BEFORE_ACCESS) {
                    env->exception_index = EXCP_DEBUG;
                } else {
                    cpu_get_tb_cpu_state(env, &pc, &cs_base, &cpu_flags);
                    tb_gen_code(env, pc, cs_base, cpu_flags, 1);
                }
                if (tcg_enabled()) {
                    cpu_resume_from_signal(env, NULL);
                }
            }
        } else {
            wp->flags &= ~BP_WATCHPOINT_HIT;
        }
    }
}

/* Watchpoint access routines.  Watchpoints are inserted using TLB tricks,
   so these check for a hit then pass through to the normal out-of-line
   phys routines.  */
static uint32_t watch_mem_readb(void *opaque, target_phys_addr_t addr)
{
    check_watchpoint(addr & ~TARGET_PAGE_MASK, ~0x0, BP_MEM_READ);
    return ldub_phys(addr);
}

static uint32_t watch_mem_readw(void *opaque, target_phys_addr_t addr)
{
    check_watchpoint(addr & ~TARGET_PAGE_MASK, ~0x1, BP_MEM_READ);
    return lduw_phys(addr);
}

static uint32_t watch_mem_readl(void *opaque, target_phys_addr_t addr)
{
    check_watchpoint(addr & ~TARGET_PAGE_MASK, ~0x3, BP_MEM_READ);
    return ldl_phys(addr);
}

static void watch_mem_writeb(void *opaque, target_phys_addr_t addr,
                             uint32_t val)
{
    check_watchpoint(addr & ~TARGET_PAGE_MASK, ~0x0, BP_MEM_WRITE);
    stb_phys(addr, val);
}

static void watch_mem_writew(void *opaque, target_phys_addr_t addr,
                             uint32_t val)
{
    check_watchpoint(addr & ~TARGET_PAGE_MASK, ~0x1, BP_MEM_WRITE);
    stw_phys(addr, val);
}

static void watch_mem_writel(void *opaque, target_phys_addr_t addr,
                             uint32_t val)
{
    check_watchpoint(addr & ~TARGET_PAGE_MASK, ~0x3, BP_MEM_WRITE);
    stl_phys(addr, val);
}

static CPUReadMemoryFunc * const watch_mem_read[3] = {
    watch_mem_readb,
    watch_mem_readw,
    watch_mem_readl,
};

static CPUWriteMemoryFunc * const watch_mem_write[3] = {
    watch_mem_writeb,
    watch_mem_writew,
    watch_mem_writel,
};

static inline uint32_t subpage_readlen (subpage_t *mmio,
                                        target_phys_addr_t addr,
                                        unsigned int len)
{
    unsigned int idx = SUBPAGE_IDX(addr);
#if defined(DEBUG_SUBPAGE)
    printf("%s: subpage %p len %d addr " TARGET_FMT_plx " idx %d\n", __func__,
           mmio, len, addr, idx);
#endif

    addr += mmio->region_offset[idx];
    idx = mmio->sub_io_index[idx];
    return io_mem_read[idx][len](io_mem_opaque[idx], addr);
}

static inline void subpage_writelen (subpage_t *mmio, target_phys_addr_t addr,
                                     uint32_t value, unsigned int len)
{
    unsigned int idx = SUBPAGE_IDX(addr);
#if defined(DEBUG_SUBPAGE)
    printf("%s: subpage %p len %d addr " TARGET_FMT_plx " idx %d value %08x\n",
           __func__, mmio, len, addr, idx, value);
#endif

    addr += mmio->region_offset[idx];
    idx = mmio->sub_io_index[idx];
    io_mem_write[idx][len](io_mem_opaque[idx], addr, value);
}

static uint32_t subpage_readb (void *opaque, target_phys_addr_t addr)
{
    return subpage_readlen(opaque, addr, 0);
}

static void subpage_writeb (void *opaque, target_phys_addr_t addr,
                            uint32_t value)
{
    subpage_writelen(opaque, addr, value, 0);
}

static uint32_t subpage_readw (void *opaque, target_phys_addr_t addr)
{
    return subpage_readlen(opaque, addr, 1);
}

static void subpage_writew (void *opaque, target_phys_addr_t addr,
                            uint32_t value)
{
    subpage_writelen(opaque, addr, value, 1);
}

static uint32_t subpage_readl (void *opaque, target_phys_addr_t addr)
{
    return subpage_readlen(opaque, addr, 2);
}

static void subpage_writel (void *opaque, target_phys_addr_t addr,
                            uint32_t value)
{
    subpage_writelen(opaque, addr, value, 2);
}

static CPUReadMemoryFunc * const subpage_read[] = {
    &subpage_readb,
    &subpage_readw,
    &subpage_readl,
};

static CPUWriteMemoryFunc * const subpage_write[] = {
    &subpage_writeb,
    &subpage_writew,
    &subpage_writel,
};

static int subpage_register (subpage_t *mmio, uint32_t start, uint32_t end,
                             ram_addr_t memory, ram_addr_t region_offset)
{
    int idx, eidx;

    if (start >= TARGET_PAGE_SIZE || end >= TARGET_PAGE_SIZE)
        return -1;
    idx = SUBPAGE_IDX(start);
    eidx = SUBPAGE_IDX(end);
#if defined(DEBUG_SUBPAGE)
    printf("%s: %p start %08x end %08x idx %08x eidx %08x mem %ld\n", __func__,
           mmio, start, end, idx, eidx, memory);
#endif
    if ((memory & ~TARGET_PAGE_MASK) == IO_MEM_RAM)
        memory = IO_MEM_UNASSIGNED;
    memory = (memory >> IO_MEM_SHIFT) & (IO_MEM_NB_ENTRIES - 1);
    for (; idx <= eidx; idx++) {
        mmio->sub_io_index[idx] = memory;
        mmio->region_offset[idx] = region_offset;
    }

    return 0;
}

static subpage_t *subpage_init (target_phys_addr_t base, ram_addr_t *phys,
                                ram_addr_t orig_memory,
                                ram_addr_t region_offset)
{
    subpage_t *mmio;
    int subpage_memory;

    mmio = g_malloc0(sizeof(subpage_t));

    mmio->base = base;
    subpage_memory = cpu_register_io_memory(subpage_read, subpage_write, mmio,
                                            DEVICE_NATIVE_ENDIAN);
#if defined(DEBUG_SUBPAGE)
    printf("%s: %p base " TARGET_FMT_plx " len %08x %d\n", __func__,
           mmio, base, TARGET_PAGE_SIZE, subpage_memory);
#endif
    *phys = subpage_memory | IO_MEM_SUBPAGE;
    subpage_register(mmio, 0, TARGET_PAGE_SIZE-1, orig_memory, region_offset);

    return mmio;
}

static int get_free_io_mem_idx(void)
{
    int i;

    for (i = 0; i<IO_MEM_NB_ENTRIES; i++)
        if (!io_mem_used[i]) {
            io_mem_used[i] = 1;
            return i;
        }
    fprintf(stderr, "RAN out out io_mem_idx, max %d !\n", IO_MEM_NB_ENTRIES);
    return -1;
}

/*
 * Usually, devices operate in little endian mode. There are devices out
 * there that operate in big endian too. Each device gets byte swapped
 * mmio if plugged onto a CPU that does the other endianness.
 *
 * CPU          Device           swap?
 *
 * little       little           no
 * little       big              yes
 * big          little           yes
 * big          big              no
 */

typedef struct SwapEndianContainer {
    CPUReadMemoryFunc *read[3];
    CPUWriteMemoryFunc *write[3];
    void *opaque;
} SwapEndianContainer;

static uint32_t swapendian_mem_readb (void *opaque, target_phys_addr_t addr)
{
    uint32_t val;
    SwapEndianContainer *c = opaque;
    val = c->read[0](c->opaque, addr);
    return val;
}

static uint32_t swapendian_mem_readw(void *opaque, target_phys_addr_t addr)
{
    uint32_t val;
    SwapEndianContainer *c = opaque;
    val = bswap16(c->read[1](c->opaque, addr));
    return val;
}

static uint32_t swapendian_mem_readl(void *opaque, target_phys_addr_t addr)
{
    uint32_t val;
    SwapEndianContainer *c = opaque;
    val = bswap32(c->read[2](c->opaque, addr));
    return val;
}

static CPUReadMemoryFunc * const swapendian_readfn[3]={
    swapendian_mem_readb,
    swapendian_mem_readw,
    swapendian_mem_readl
};

static void swapendian_mem_writeb(void *opaque, target_phys_addr_t addr,
                                  uint32_t val)
{
    SwapEndianContainer *c = opaque;
    c->write[0](c->opaque, addr, val);
}

static void swapendian_mem_writew(void *opaque, target_phys_addr_t addr,
                                  uint32_t val)
{
    SwapEndianContainer *c = opaque;
    c->write[1](c->opaque, addr, bswap16(val));
}

static void swapendian_mem_writel(void *opaque, target_phys_addr_t addr,
                                  uint32_t val)
{
    SwapEndianContainer *c = opaque;
    c->write[2](c->opaque, addr, bswap32(val));
}

static CPUWriteMemoryFunc * const swapendian_writefn[3]={
    swapendian_mem_writeb,
    swapendian_mem_writew,
    swapendian_mem_writel
};

static void swapendian_init(int io_index)
{
    SwapEndianContainer *c = g_malloc(sizeof(SwapEndianContainer));
    int i;

    /* Swap mmio for big endian targets */
    c->opaque = io_mem_opaque[io_index];
    for (i = 0; i < 3; i++) {
        c->read[i] = io_mem_read[io_index][i];
        c->write[i] = io_mem_write[io_index][i];

        io_mem_read[io_index][i] = swapendian_readfn[i];
        io_mem_write[io_index][i] = swapendian_writefn[i];
    }
    io_mem_opaque[io_index] = c;
}

static void swapendian_del(int io_index)
{
    if (io_mem_read[io_index][0] == swapendian_readfn[0]) {
        g_free(io_mem_opaque[io_index]);
    }
}

/* mem_read and mem_write are arrays of functions containing the
   function to access byte (index 0), word (index 1) and dword (index
   2). Functions can be omitted with a NULL function pointer.
   If io_index is non zero, the corresponding io zone is
   modified. If it is zero, a new io zone is allocated. The return
   value can be used with cpu_register_physical_memory(). (-1) is
   returned if error. */
static int cpu_register_io_memory_fixed(int io_index,
                                        CPUReadMemoryFunc * const *mem_read,
                                        CPUWriteMemoryFunc * const *mem_write,
                                        void *opaque, enum device_endian endian)
{
    int i;

    if (io_index <= 0) {
        io_index = get_free_io_mem_idx();
        if (io_index == -1)
            return io_index;
    } else {
        io_index >>= IO_MEM_SHIFT;
        if (io_index >= IO_MEM_NB_ENTRIES)
            return -1;
    }

    for (i = 0; i < 3; ++i) {
        io_mem_read[io_index][i]
            = (mem_read[i] ? mem_read[i] : unassigned_mem_read[i]);
    }
    for (i = 0; i < 3; ++i) {
        io_mem_write[io_index][i]
            = (mem_write[i] ? mem_write[i] : unassigned_mem_write[i]);
    }
    io_mem_opaque[io_index] = opaque;

    switch (endian) {
    case DEVICE_BIG_ENDIAN:
#ifndef TARGET_WORDS_BIGENDIAN
        swapendian_init(io_index);
#endif
        break;
    case DEVICE_LITTLE_ENDIAN:
#ifdef TARGET_WORDS_BIGENDIAN
        swapendian_init(io_index);
#endif
        break;
    case DEVICE_NATIVE_ENDIAN:
    default:
        break;
    }

    return (io_index << IO_MEM_SHIFT);
}

int cpu_register_io_memory(CPUReadMemoryFunc * const *mem_read,
                           CPUWriteMemoryFunc * const *mem_write,
                           void *opaque, enum device_endian endian)
{
    return cpu_register_io_memory_fixed(0, mem_read, mem_write, opaque, endian);
}

void cpu_unregister_io_memory(int io_table_address)
{
    int i;
    int io_index = io_table_address >> IO_MEM_SHIFT;

    swapendian_del(io_index);

    for (i=0;i < 3; i++) {
        io_mem_read[io_index][i] = unassigned_mem_read[i];
        io_mem_write[io_index][i] = unassigned_mem_write[i];
    }
    io_mem_opaque[io_index] = NULL;
    io_mem_used[io_index] = 0;
}

static void io_mem_init(void)
{
    int i;

    cpu_register_io_memory_fixed(IO_MEM_ROM, error_mem_read,
                                 unassigned_mem_write, NULL,
                                 DEVICE_NATIVE_ENDIAN);
    cpu_register_io_memory_fixed(IO_MEM_UNASSIGNED, unassigned_mem_read,
                                 unassigned_mem_write, NULL,
                                 DEVICE_NATIVE_ENDIAN);
    cpu_register_io_memory_fixed(IO_MEM_NOTDIRTY, error_mem_read,
                                 notdirty_mem_write, NULL,
                                 DEVICE_NATIVE_ENDIAN);
    for (i=0; i<5; i++)
        io_mem_used[i] = 1;

    io_mem_watch = cpu_register_io_memory(watch_mem_read,
                                          watch_mem_write, NULL,
                                          DEVICE_NATIVE_ENDIAN);
}

static void memory_map_init(void)
{
    system_memory = g_malloc(sizeof(*system_memory));
    memory_region_init(system_memory, "system", INT64_MAX);
    set_system_memory_map(system_memory);

    system_io = g_malloc(sizeof(*system_io));
    memory_region_init(system_io, "io", 65536);
    set_system_io_map(system_io);
}

MemoryRegion *get_system_memory(void)
{
    return system_memory;
}

MemoryRegion *get_system_io(void)
{
    return system_io;
}

#endif /* !defined(CONFIG_USER_ONLY) */

/* physical memory access (slow version, mainly for debug) */
#if defined(CONFIG_USER_ONLY)
int cpu_memory_rw_debug(CPUState *env, target_ulong addr,
                        uint8_t *buf, int len, int is_write)
{
    int l, flags;
    target_ulong page;
    void * p;

    while (len > 0) {
        page = addr & TARGET_PAGE_MASK;
        l = (page + TARGET_PAGE_SIZE) - addr;
        if (l > len)
            l = len;
        flags = page_get_flags(page);
        if (!(flags & PAGE_VALID))
            return -1;
        if (is_write) {
            if (!(flags & PAGE_WRITE))
                return -1;
            /* XXX: this code should not depend on lock_user */
            if (!(p = lock_user(VERIFY_WRITE, addr, l, 0)))
                return -1;
            memcpy(p, buf, l);
            unlock_user(p, addr, l);
        } else {
            if (!(flags & PAGE_READ))
                return -1;
            /* XXX: this code should not depend on lock_user */
            if (!(p = lock_user(VERIFY_READ, addr, l, 1)))
                return -1;
            memcpy(buf, p, l);
            unlock_user(p, addr, 0);
        }
        len -= l;
        buf += l;
        addr += l;
    }
    return 0;
}

#else
void cpu_physical_memory_rw(target_phys_addr_t addr, uint8_t *buf,
                            int len, int is_write)
{
    int l, io_index;
    uint8_t *ptr;
    uint32_t val;
    target_phys_addr_t page;
    ram_addr_t pd;
    PhysPageDesc *p;

    while (len > 0) {
        page = addr & TARGET_PAGE_MASK;
        l = (page + TARGET_PAGE_SIZE) - addr;
        if (l > len)
            l = len;
        p = phys_page_find(page >> TARGET_PAGE_BITS);
        if (!p) {
            pd = IO_MEM_UNASSIGNED;
        } else {
            pd = p->phys_offset;
        }

        if (is_write) {
            if ((pd & ~TARGET_PAGE_MASK) != IO_MEM_RAM) {
                target_phys_addr_t addr1 = addr;
                io_index = (pd >> IO_MEM_SHIFT) & (IO_MEM_NB_ENTRIES - 1);
                if (p)
                    addr1 = (addr & ~TARGET_PAGE_MASK) + p->region_offset;
                /* XXX: could force cpu_single_env to NULL to avoid
                   potential bugs */
                if (l >= 4 && ((addr1 & 3) == 0)) {
                    /* 32 bit write access */
                    val = ldl_p(buf);
                    io_mem_write[io_index][2](io_mem_opaque[io_index], addr1, val);
                    l = 4;
                } else if (l >= 2 && ((addr1 & 1) == 0)) {
                    /* 16 bit write access */
                    val = lduw_p(buf);
                    io_mem_write[io_index][1](io_mem_opaque[io_index], addr1, val);
                    l = 2;
                } else {
                    /* 8 bit write access */
                    val = ldub_p(buf);
                    io_mem_write[io_index][0](io_mem_opaque[io_index], addr1, val);
                    l = 1;
                }
            } else {
                ram_addr_t addr1;
                addr1 = (pd & TARGET_PAGE_MASK) + (addr & ~TARGET_PAGE_MASK);
                /* RAM case */
                ptr = qemu_get_ram_ptr(addr1);
                memcpy(ptr, buf, l);
                if (!cpu_physical_memory_is_dirty(addr1)) {
                    /* invalidate code */
                    tb_invalidate_phys_page_range(addr1, addr1 + l, 0);
                    /* set dirty bit */
                    cpu_physical_memory_set_dirty_flags(
                        addr1, (0xff & ~CODE_DIRTY_FLAG));
                }
                qemu_put_ram_ptr(ptr);
            }
        } else {
            if ((pd & ~TARGET_PAGE_MASK) > IO_MEM_ROM &&
                !(pd & IO_MEM_ROMD)) {
                target_phys_addr_t addr1 = addr;
                /* I/O case */
                io_index = (pd >> IO_MEM_SHIFT) & (IO_MEM_NB_ENTRIES - 1);
                if (p)
                    addr1 = (addr & ~TARGET_PAGE_MASK) + p->region_offset;
                if (l >= 4 && ((addr1 & 3) == 0)) {
                    /* 32 bit read access */
                    val = io_mem_read[io_index][2](io_mem_opaque[io_index], addr1);
                    stl_p(buf, val);
                    l = 4;
                } else if (l >= 2 && ((addr1 & 1) == 0)) {
                    /* 16 bit read access */
                    val = io_mem_read[io_index][1](io_mem_opaque[io_index], addr1);
                    stw_p(buf, val);
                    l = 2;
                } else {
                    /* 8 bit read access */
                    val = io_mem_read[io_index][0](io_mem_opaque[io_index], addr1);
                    stb_p(buf, val);
                    l = 1;
                }
            } else {
                /* RAM case */
                ptr = qemu_get_ram_ptr(pd & TARGET_PAGE_MASK);
                memcpy(buf, ptr + (addr & ~TARGET_PAGE_MASK), l);
                qemu_put_ram_ptr(ptr);
            }
        }
        len -= l;
        buf += l;
        addr += l;
    }
}

/* used for ROM loading : can write in RAM and ROM */
void cpu_physical_memory_write_rom(target_phys_addr_t addr,
                                   const uint8_t *buf, int len)
{
    int l;
    uint8_t *ptr;
    target_phys_addr_t page;
    unsigned long pd;
    PhysPageDesc *p;

    while (len > 0) {
        page = addr & TARGET_PAGE_MASK;
        l = (page + TARGET_PAGE_SIZE) - addr;
        if (l > len)
            l = len;
        p = phys_page_find(page >> TARGET_PAGE_BITS);
        if (!p) {
            pd = IO_MEM_UNASSIGNED;
        } else {
            pd = p->phys_offset;
        }

        if ((pd & ~TARGET_PAGE_MASK) != IO_MEM_RAM &&
            (pd & ~TARGET_PAGE_MASK) != IO_MEM_ROM &&
            !(pd & IO_MEM_ROMD)) {
            /* do nothing */
        } else {
            unsigned long addr1;
            addr1 = (pd & TARGET_PAGE_MASK) + (addr & ~TARGET_PAGE_MASK);
            /* ROM/RAM case */
            ptr = qemu_get_ram_ptr(addr1);
            memcpy(ptr, buf, l);
            qemu_put_ram_ptr(ptr);
        }
        len -= l;
        buf += l;
        addr += l;
    }
}

typedef struct {
    void *buffer;
    target_phys_addr_t addr;
    target_phys_addr_t len;
} BounceBuffer;

static BounceBuffer bounce;

typedef struct MapClient {
    void *opaque;
    void (*callback)(void *opaque);
    QLIST_ENTRY(MapClient) link;
} MapClient;

static QLIST_HEAD(map_client_list, MapClient) map_client_list
    = QLIST_HEAD_INITIALIZER(map_client_list);

void *cpu_register_map_client(void *opaque, void (*callback)(void *opaque))
{
    MapClient *client = g_malloc(sizeof(*client));

    client->opaque = opaque;
    client->callback = callback;
    QLIST_INSERT_HEAD(&map_client_list, client, link);
    return client;
}

void cpu_unregister_map_client(void *_client)
{
    MapClient *client = (MapClient *)_client;

    QLIST_REMOVE(client, link);
    g_free(client);
}

static void cpu_notify_map_clients(void)
{
    MapClient *client;

    while (!QLIST_EMPTY(&map_client_list)) {
        client = QLIST_FIRST(&map_client_list);
        client->callback(client->opaque);
        cpu_unregister_map_client(client);
    }
}

/* Map a physical memory region into a host virtual address.
 * May map a subset of the requested range, given by and returned in *plen.
 * May return NULL if resources needed to perform the mapping are exhausted.
 * Use only for reads OR writes - not for read-modify-write operations.
 * Use cpu_register_map_client() to know when retrying the map operation is
 * likely to succeed.
 */
void *cpu_physical_memory_map(target_phys_addr_t addr,
                              target_phys_addr_t *plen,
                              int is_write)
{
    target_phys_addr_t len = *plen;
    target_phys_addr_t todo = 0;
    int l;
    target_phys_addr_t page;
    unsigned long pd;
    PhysPageDesc *p;
    ram_addr_t raddr = RAM_ADDR_MAX;
    ram_addr_t rlen;
    void *ret;

    while (len > 0) {
        page = addr & TARGET_PAGE_MASK;
        l = (page + TARGET_PAGE_SIZE) - addr;
        if (l > len)
            l = len;
        p = phys_page_find(page >> TARGET_PAGE_BITS);
        if (!p) {
            pd = IO_MEM_UNASSIGNED;
        } else {
            pd = p->phys_offset;
        }

        if ((pd & ~TARGET_PAGE_MASK) != IO_MEM_RAM) {
            if (todo || bounce.buffer) {
                break;
            }
            bounce.buffer = qemu_memalign(TARGET_PAGE_SIZE, TARGET_PAGE_SIZE);
            bounce.addr = addr;
            bounce.len = l;
            if (!is_write) {
                cpu_physical_memory_read(addr, bounce.buffer, l);
            }

            *plen = l;
            return bounce.buffer;
        }
        if (!todo) {
            raddr = (pd & TARGET_PAGE_MASK) + (addr & ~TARGET_PAGE_MASK);
        }

        len -= l;
        addr += l;
        todo += l;
    }
    rlen = todo;
    ret = qemu_ram_ptr_length(raddr, &rlen);
    *plen = rlen;
    return ret;
}

/* Unmaps a memory region previously mapped by cpu_physical_memory_map().
 * Will also mark the memory as dirty if is_write == 1.  access_len gives
 * the amount of memory that was actually read or written by the caller.
 */
void cpu_physical_memory_unmap(void *buffer, target_phys_addr_t len,
                               int is_write, target_phys_addr_t access_len)
{
    if (buffer != bounce.buffer) {
        if (is_write) {
            ram_addr_t addr1 = qemu_ram_addr_from_host_nofail(buffer);
            while (access_len) {
                unsigned l;
                l = TARGET_PAGE_SIZE;
                if (l > access_len)
                    l = access_len;
                if (!cpu_physical_memory_is_dirty(addr1)) {
                    /* invalidate code */
                    tb_invalidate_phys_page_range(addr1, addr1 + l, 0);
                    /* set dirty bit */
                    cpu_physical_memory_set_dirty_flags(
                        addr1, (0xff & ~CODE_DIRTY_FLAG));
                }
                addr1 += l;
                access_len -= l;
            }
        }
        if (xen_enabled()) {
            xen_invalidate_map_cache_entry(buffer);
        }
        return;
    }
    if (is_write) {
        cpu_physical_memory_write(bounce.addr, bounce.buffer, access_len);
    }
    qemu_vfree(bounce.buffer);
    bounce.buffer = NULL;
    cpu_notify_map_clients();
}

/* warning: addr must be aligned */
static inline uint32_t ldl_phys_internal(target_phys_addr_t addr,
                                         enum device_endian endian)
{
    int io_index;
    uint8_t *ptr;
    uint32_t val;
    unsigned long pd;
    PhysPageDesc *p;

    p = phys_page_find(addr >> TARGET_PAGE_BITS);
    if (!p) {
        pd = IO_MEM_UNASSIGNED;
    } else {
        pd = p->phys_offset;
    }

    if ((pd & ~TARGET_PAGE_MASK) > IO_MEM_ROM &&
        !(pd & IO_MEM_ROMD)) {
        /* I/O case */
        io_index = (pd >> IO_MEM_SHIFT) & (IO_MEM_NB_ENTRIES - 1);
        if (p)
            addr = (addr & ~TARGET_PAGE_MASK) + p->region_offset;
        val = io_mem_read[io_index][2](io_mem_opaque[io_index], addr);
#if defined(TARGET_WORDS_BIGENDIAN)
        if (endian == DEVICE_LITTLE_ENDIAN) {
            val = bswap32(val);
        }
#else
        if (endian == DEVICE_BIG_ENDIAN) {
            val = bswap32(val);
        }
#endif
    } else {
        /* RAM case */
        ptr = qemu_get_ram_ptr(pd & TARGET_PAGE_MASK) +
            (addr & ~TARGET_PAGE_MASK);
        switch (endian) {
        case DEVICE_LITTLE_ENDIAN:
            val = ldl_le_p(ptr);
            break;
        case DEVICE_BIG_ENDIAN:
            val = ldl_be_p(ptr);
            break;
        default:
            val = ldl_p(ptr);
            break;
        }
    }
    return val;
}

uint32_t ldl_phys(target_phys_addr_t addr)
{
    return ldl_phys_internal(addr, DEVICE_NATIVE_ENDIAN);
}

uint32_t ldl_le_phys(target_phys_addr_t addr)
{
    return ldl_phys_internal(addr, DEVICE_LITTLE_ENDIAN);
}

uint32_t ldl_be_phys(target_phys_addr_t addr)
{
    return ldl_phys_internal(addr, DEVICE_BIG_ENDIAN);
}

/* warning: addr must be aligned */
static inline uint64_t ldq_phys_internal(target_phys_addr_t addr,
                                         enum device_endian endian)
{
    int io_index;
    uint8_t *ptr;
    uint64_t val;
    unsigned long pd;
    PhysPageDesc *p;

    p = phys_page_find(addr >> TARGET_PAGE_BITS);
    if (!p) {
        pd = IO_MEM_UNASSIGNED;
    } else {
        pd = p->phys_offset;
    }

    if ((pd & ~TARGET_PAGE_MASK) > IO_MEM_ROM &&
        !(pd & IO_MEM_ROMD)) {
        /* I/O case */
        io_index = (pd >> IO_MEM_SHIFT) & (IO_MEM_NB_ENTRIES - 1);
        if (p)
            addr = (addr & ~TARGET_PAGE_MASK) + p->region_offset;

        /* XXX This is broken when device endian != cpu endian.
               Fix and add "endian" variable check */
#ifdef TARGET_WORDS_BIGENDIAN
        val = (uint64_t)io_mem_read[io_index][2](io_mem_opaque[io_index], addr) << 32;
        val |= io_mem_read[io_index][2](io_mem_opaque[io_index], addr + 4);
#else
        val = io_mem_read[io_index][2](io_mem_opaque[io_index], addr);
        val |= (uint64_t)io_mem_read[io_index][2](io_mem_opaque[io_index], addr + 4) << 32;
#endif
    } else {
        /* RAM case */
        ptr = qemu_get_ram_ptr(pd & TARGET_PAGE_MASK) +
            (addr & ~TARGET_PAGE_MASK);
        switch (endian) {
        case DEVICE_LITTLE_ENDIAN:
            val = ldq_le_p(ptr);
            break;
        case DEVICE_BIG_ENDIAN:
            val = ldq_be_p(ptr);
            break;
        default:
            val = ldq_p(ptr);
            break;
        }
    }
    return val;
}

uint64_t ldq_phys(target_phys_addr_t addr)
{
    return ldq_phys_internal(addr, DEVICE_NATIVE_ENDIAN);
}

uint64_t ldq_le_phys(target_phys_addr_t addr)
{
    return ldq_phys_internal(addr, DEVICE_LITTLE_ENDIAN);
}

uint64_t ldq_be_phys(target_phys_addr_t addr)
{
    return ldq_phys_internal(addr, DEVICE_BIG_ENDIAN);
}

/* XXX: optimize */
uint32_t ldub_phys(target_phys_addr_t addr)
{
    uint8_t val;
    cpu_physical_memory_read(addr, &val, 1);
    return val;
}

/* warning: addr must be aligned */
static inline uint32_t lduw_phys_internal(target_phys_addr_t addr,
                                          enum device_endian endian)
{
    int io_index;
    uint8_t *ptr;
    uint64_t val;
    unsigned long pd;
    PhysPageDesc *p;

    p = phys_page_find(addr >> TARGET_PAGE_BITS);
    if (!p) {
        pd = IO_MEM_UNASSIGNED;
    } else {
        pd = p->phys_offset;
    }

    if ((pd & ~TARGET_PAGE_MASK) > IO_MEM_ROM &&
        !(pd & IO_MEM_ROMD)) {
        /* I/O case */
        io_index = (pd >> IO_MEM_SHIFT) & (IO_MEM_NB_ENTRIES - 1);
        if (p)
            addr = (addr & ~TARGET_PAGE_MASK) + p->region_offset;
        val = io_mem_read[io_index][1](io_mem_opaque[io_index], addr);
#if defined(TARGET_WORDS_BIGENDIAN)
        if (endian == DEVICE_LITTLE_ENDIAN) {
            val = bswap16(val);
        }
#else
        if (endian == DEVICE_BIG_ENDIAN) {
            val = bswap16(val);
        }
#endif
    } else {
        /* RAM case */
        ptr = qemu_get_ram_ptr(pd & TARGET_PAGE_MASK) +
            (addr & ~TARGET_PAGE_MASK);
        switch (endian) {
        case DEVICE_LITTLE_ENDIAN:
            val = lduw_le_p(ptr);
            break;
        case DEVICE_BIG_ENDIAN:
            val = lduw_be_p(ptr);
            break;
        default:
            val = lduw_p(ptr);
            break;
        }
    }
    return val;
}

uint32_t lduw_phys(target_phys_addr_t addr)
{
    return lduw_phys_internal(addr, DEVICE_NATIVE_ENDIAN);
}

uint32_t lduw_le_phys(target_phys_addr_t addr)
{
    return lduw_phys_internal(addr, DEVICE_LITTLE_ENDIAN);
}

uint32_t lduw_be_phys(target_phys_addr_t addr)
{
    return lduw_phys_internal(addr, DEVICE_BIG_ENDIAN);
}

/* warning: addr must be aligned. The ram page is not masked as dirty
   and the code inside is not invalidated. It is useful if the dirty
   bits are used to track modified PTEs */
void stl_phys_notdirty(target_phys_addr_t addr, uint32_t val)
{
    int io_index;
    uint8_t *ptr;
    unsigned long pd;
    PhysPageDesc *p;

    p = phys_page_find(addr >> TARGET_PAGE_BITS);
    if (!p) {
        pd = IO_MEM_UNASSIGNED;
    } else {
        pd = p->phys_offset;
    }

    if ((pd & ~TARGET_PAGE_MASK) != IO_MEM_RAM) {
        io_index = (pd >> IO_MEM_SHIFT) & (IO_MEM_NB_ENTRIES - 1);
        if (p)
            addr = (addr & ~TARGET_PAGE_MASK) + p->region_offset;
        io_mem_write[io_index][2](io_mem_opaque[io_index], addr, val);
    } else {
        unsigned long addr1 = (pd & TARGET_PAGE_MASK) + (addr & ~TARGET_PAGE_MASK);
        ptr = qemu_get_ram_ptr(addr1);
        stl_p(ptr, val);

        if (unlikely(in_migration)) {
            if (!cpu_physical_memory_is_dirty(addr1)) {
                /* invalidate code */
                tb_invalidate_phys_page_range(addr1, addr1 + 4, 0);
                /* set dirty bit */
                cpu_physical_memory_set_dirty_flags(
                    addr1, (0xff & ~CODE_DIRTY_FLAG));
            }
        }
    }
}

void stq_phys_notdirty(target_phys_addr_t addr, uint64_t val)
{
    int io_index;
    uint8_t *ptr;
    unsigned long pd;
    PhysPageDesc *p;

    p = phys_page_find(addr >> TARGET_PAGE_BITS);
    if (!p) {
        pd = IO_MEM_UNASSIGNED;
    } else {
        pd = p->phys_offset;
    }

    if ((pd & ~TARGET_PAGE_MASK) != IO_MEM_RAM) {
        io_index = (pd >> IO_MEM_SHIFT) & (IO_MEM_NB_ENTRIES - 1);
        if (p)
            addr = (addr & ~TARGET_PAGE_MASK) + p->region_offset;
#ifdef TARGET_WORDS_BIGENDIAN
        io_mem_write[io_index][2](io_mem_opaque[io_index], addr, val >> 32);
        io_mem_write[io_index][2](io_mem_opaque[io_index], addr + 4, val);
#else
        io_mem_write[io_index][2](io_mem_opaque[io_index], addr, val);
        io_mem_write[io_index][2](io_mem_opaque[io_index], addr + 4, val >> 32);
#endif
    } else {
        ptr = qemu_get_ram_ptr(pd & TARGET_PAGE_MASK) +
            (addr & ~TARGET_PAGE_MASK);
        stq_p(ptr, val);
    }
}

/* warning: addr must be aligned */
static inline void stl_phys_internal(target_phys_addr_t addr, uint32_t val,
                                     enum device_endian endian)
{
    int io_index;
    uint8_t *ptr;
    unsigned long pd;
    PhysPageDesc *p;

    p = phys_page_find(addr >> TARGET_PAGE_BITS);
    if (!p) {
        pd = IO_MEM_UNASSIGNED;
    } else {
        pd = p->phys_offset;
    }

    if ((pd & ~TARGET_PAGE_MASK) != IO_MEM_RAM) {
        io_index = (pd >> IO_MEM_SHIFT) & (IO_MEM_NB_ENTRIES - 1);
        if (p)
            addr = (addr & ~TARGET_PAGE_MASK) + p->region_offset;
#if defined(TARGET_WORDS_BIGENDIAN)
        if (endian == DEVICE_LITTLE_ENDIAN) {
            val = bswap32(val);
        }
#else
        if (endian == DEVICE_BIG_ENDIAN) {
            val = bswap32(val);
        }
#endif
        io_mem_write[io_index][2](io_mem_opaque[io_index], addr, val);
    } else {
        unsigned long addr1;
        addr1 = (pd & TARGET_PAGE_MASK) + (addr & ~TARGET_PAGE_MASK);
        /* RAM case */
        ptr = qemu_get_ram_ptr(addr1);
        switch (endian) {
        case DEVICE_LITTLE_ENDIAN:
            stl_le_p(ptr, val);
            break;
        case DEVICE_BIG_ENDIAN:
            stl_be_p(ptr, val);
            break;
        default:
            stl_p(ptr, val);
            break;
        }
        if (!cpu_physical_memory_is_dirty(addr1)) {
            /* invalidate code */
            tb_invalidate_phys_page_range(addr1, addr1 + 4, 0);
            /* set dirty bit */
            cpu_physical_memory_set_dirty_flags(addr1,
                (0xff & ~CODE_DIRTY_FLAG));
        }
    }
}

void stl_phys(target_phys_addr_t addr, uint32_t val)
{
    stl_phys_internal(addr, val, DEVICE_NATIVE_ENDIAN);
}

void stl_le_phys(target_phys_addr_t addr, uint32_t val)
{
    stl_phys_internal(addr, val, DEVICE_LITTLE_ENDIAN);
}

void stl_be_phys(target_phys_addr_t addr, uint32_t val)
{
    stl_phys_internal(addr, val, DEVICE_BIG_ENDIAN);
}

/* XXX: optimize */
void stb_phys(target_phys_addr_t addr, uint32_t val)
{
    uint8_t v = val;
    cpu_physical_memory_write(addr, &v, 1);
}

/* warning: addr must be aligned */
static inline void stw_phys_internal(target_phys_addr_t addr, uint32_t val,
                                     enum device_endian endian)
{
    int io_index;
    uint8_t *ptr;
    unsigned long pd;
    PhysPageDesc *p;

    p = phys_page_find(addr >> TARGET_PAGE_BITS);
    if (!p) {
        pd = IO_MEM_UNASSIGNED;
    } else {
        pd = p->phys_offset;
    }

    if ((pd & ~TARGET_PAGE_MASK) != IO_MEM_RAM) {
        io_index = (pd >> IO_MEM_SHIFT) & (IO_MEM_NB_ENTRIES - 1);
        if (p)
            addr = (addr & ~TARGET_PAGE_MASK) + p->region_offset;
#if defined(TARGET_WORDS_BIGENDIAN)
        if (endian == DEVICE_LITTLE_ENDIAN) {
            val = bswap16(val);
        }
#else
        if (endian == DEVICE_BIG_ENDIAN) {
            val = bswap16(val);
        }
#endif
        io_mem_write[io_index][1](io_mem_opaque[io_index], addr, val);
    } else {
        unsigned long addr1;
        addr1 = (pd & TARGET_PAGE_MASK) + (addr & ~TARGET_PAGE_MASK);
        /* RAM case */
        ptr = qemu_get_ram_ptr(addr1);
        switch (endian) {
        case DEVICE_LITTLE_ENDIAN:
            stw_le_p(ptr, val);
            break;
        case DEVICE_BIG_ENDIAN:
            stw_be_p(ptr, val);
            break;
        default:
            stw_p(ptr, val);
            break;
        }
        if (!cpu_physical_memory_is_dirty(addr1)) {
            /* invalidate code */
            tb_invalidate_phys_page_range(addr1, addr1 + 2, 0);
            /* set dirty bit */
            cpu_physical_memory_set_dirty_flags(addr1,
                (0xff & ~CODE_DIRTY_FLAG));
        }
    }
}

void stw_phys(target_phys_addr_t addr, uint32_t val)
{
    stw_phys_internal(addr, val, DEVICE_NATIVE_ENDIAN);
}

void stw_le_phys(target_phys_addr_t addr, uint32_t val)
{
    stw_phys_internal(addr, val, DEVICE_LITTLE_ENDIAN);
}

void stw_be_phys(target_phys_addr_t addr, uint32_t val)
{
    stw_phys_internal(addr, val, DEVICE_BIG_ENDIAN);
}

/* XXX: optimize */
void stq_phys(target_phys_addr_t addr, uint64_t val)
{
    val = tswap64(val);
    cpu_physical_memory_write(addr, &val, 8);
}

void stq_le_phys(target_phys_addr_t addr, uint64_t val)
{
    val = cpu_to_le64(val);
    cpu_physical_memory_write(addr, &val, 8);
}

void stq_be_phys(target_phys_addr_t addr, uint64_t val)
{
    val = cpu_to_be64(val);
    cpu_physical_memory_write(addr, &val, 8);
}

/* virtual memory access for debug (includes writing to ROM) */
int cpu_memory_rw_debug(CPUState *env, target_ulong addr,
                        uint8_t *buf, int len, int is_write)
{
    int l;
    target_phys_addr_t phys_addr;
    target_ulong page;

    while (len > 0) {
        page = addr & TARGET_PAGE_MASK;
        phys_addr = cpu_get_phys_page_debug(env, page);
        /* if no physical page mapped, return an error */
        if (phys_addr == -1)
            return -1;
        l = (page + TARGET_PAGE_SIZE) - addr;
        if (l > len)
            l = len;
        phys_addr += (addr & ~TARGET_PAGE_MASK);
        if (is_write)
            cpu_physical_memory_write_rom(phys_addr, buf, l);
        else
            cpu_physical_memory_rw(phys_addr, buf, l, is_write);
        len -= l;
        buf += l;
        addr += l;
    }
    return 0;
}
#endif

/* in deterministic execution mode, instructions doing device I/Os
   must be at the end of the TB */
void cpu_io_recompile(CPUState *env, void *retaddr)
{
    TranslationBlock *tb;
    uint32_t n, cflags;
    target_ulong pc, cs_base;
    uint64_t flags;

    tb = tb_find_pc((unsigned long)retaddr);
    if (!tb) {
        cpu_abort(env, "cpu_io_recompile: could not find TB for pc=%p", 
                  retaddr);
    }
    n = env->icount_decr.u16.low + tb->icount;
    if (tcg_enabled()) {
        cpu_restore_state(tb, env, (unsigned long)retaddr);
    }
    /* Calculate how many instructions had been executed before the fault
       occurred.  */
    n = n - env->icount_decr.u16.low;
    /* Generate a new TB ending on the I/O insn.  */
    n++;
    /* On MIPS and SH, delay slot instructions can only be restarted if
       they were already the first instruction in the TB.  If this is not
       the first instruction in a TB then re-execute the preceding
       branch.  */
#if defined(TARGET_MIPS)
    if ((env->hflags & MIPS_HFLAG_BMASK) != 0 && n > 1) {
        env->active_tc.PC -= 4;
        env->icount_decr.u16.low++;
        env->hflags &= ~MIPS_HFLAG_BMASK;
    }
#elif defined(TARGET_SH4)
    if ((env->flags & ((DELAY_SLOT | DELAY_SLOT_CONDITIONAL))) != 0
            && n > 1) {
        env->pc -= 2;
        env->icount_decr.u16.low++;
        env->flags &= ~(DELAY_SLOT | DELAY_SLOT_CONDITIONAL);
    }
#endif
    /* This should never happen.  */
    if (n > CF_COUNT_MASK)
        cpu_abort(env, "TB too big during recompile");

    cflags = n | CF_LAST_IO;
    pc = tb->pc;
    cs_base = tb->cs_base;
    flags = tb->flags;
    tb_phys_invalidate(tb, -1);
    /* FIXME: In theory this could raise an exception.  In practice
       we have already translated the block once so it's probably ok.  */
    tb_gen_code(env, pc, cs_base, flags, cflags);
    /* TODO: If env->pc != tb->pc (i.e. the faulting instruction was not
       the first in the TB) then we end up generating a whole new TB and
       repeating the fault, which is horribly inefficient.
       Better would be to execute just this insn uncached, or generate a
       second new TB.  */
    if (tcg_enabled()) {
        cpu_resume_from_signal(env, NULL);
    }
}
