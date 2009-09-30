/*
 * Host-side implementation of GL/GLX API
 * 
 * Copyright (c) 2006,2007 Even Rouault
 * Copyright (c) 2009 Nokia Corporation
 *
 * NOTE: in its current state this code works only for 32bit guests AND
 * when guest endianess equals host endianess.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "helper_opengl.h"
#include "opengl_func_parse.h"
#include "opengl_exec.h"

//#define GL_EXCESS_DEBUG

#ifdef QEMUGL_MULTITHREADED
#include <pthread.h>
#include "sys-queue.h"
typedef struct opengl_thread_state_s {
    struct helper_opengl_s state;
    pthread_t thread;
    pthread_mutex_t runlock, finishlock;
    pthread_cond_t runcond, finishcond;
    int runstate, finishstate;
    uint32_t regbase;
    LIST_ENTRY(opengl_thread_state_s) link;
} OpenGLThreadState;
typedef struct OpenGLState {
    CPUState *env;
    uint32_t pidquery;
    LIST_HEAD(guest_list_s, opengl_thread_state_s) guest_list;
} OpenGLState;
#else
typedef struct helper_opengl_s OpenGLState;
#endif

#define GL_ERROR(fmt,...) fprintf(stderr, "%s@%d: " fmt "\n", __FUNCTION__, \
                                  __LINE__, ##__VA_ARGS__)

#ifdef GL_EXCESS_DEBUG
#define TRACE(fmt,...) GL_ERROR(fmt, ##__VA_ARGS__)
#else
#define TRACE(...)
#endif

#define MAX_GLFUNC_NB_ARGS 50

#ifndef QEMUGL_MULTITHREADED
extern int last_process_id;
static int last_func_number = -1;
#define SET_LAST_PROCESS_ID(n) last_process_id = n
#else
#define SET_LAST_PROCESS_ID(n)
#endif
static size_t (*my_strlen)(const char *) = NULL;

int get_phys_addr(CPUState *env, uint32_t address,
                  int access_type, int is_user,
                  uint32_t *phys_ptr, int *prot);

static inline target_ulong get_phys_mem_addr(CPUState *env, target_ulong addr)
{
    int prot;
    target_ulong phys_addr = 0;
    get_phys_addr(env, addr, 0, 1, &phys_addr, &prot);
    return phys_addr;
/*  int is_user, index;
  index = (addr >> TARGET_PAGE_BITS) & (CPU_TLB_SIZE - 1);
  is_user = ((env->hflags & HF_CPL_MASK) == 3);
  if (is_user == 0)
  {
    fprintf(stderr, "not in userland !!!\n");
    return NULL;
  }
  if (__builtin_expect(env->tlb_table[is_user][index].addr_code != 
      (addr & TARGET_PAGE_MASK), 0))
  {
    target_ulong ret = cpu_get_phys_page_debug((CPUState *)env, addr);
    if (ret == -1)
    {
      fprintf(stderr, "cpu_get_phys_page_debug(env, %x) == %x\n", addr, ret);
      fprintf(stderr, "not in phys mem %x (%x %x)\n", addr, env->tlb_table[is_user][index].addr_code, addr & TARGET_PAGE_MASK);
      fprintf(stderr, "cpu_x86_handle_mmu_fault = %d\n",
              cpu_x86_handle_mmu_fault((CPUState*)env, addr, 0, 1, 1));
      return NULL;
    }
    else
    {
      if (ret + TARGET_PAGE_SIZE <= phys_ram_size)
      {
        return phys_ram_base + ret + (((target_ulong)addr) & (TARGET_PAGE_SIZE - 1));
      }
      else
      {
        fprintf(stderr, "cpu_get_phys_page_debug(env, %x) == %xp\n", addr, ret);
        fprintf(stderr, "ret=%x phys_ram_size=%x\n", ret, phys_ram_size);
        return NULL;
      }
    }
  }
  else
  {
    return (void*)(addr + env->tlb_table[is_user][index].addend);
  }*/
}

#ifndef MIN
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif

enum {
    NOT_MAPPED,
    MAPPED_CONTIGUOUS,
    MAPPED_NOT_CONTIGUOUS
};

#define TARGET_ADDR_LOW_ALIGN(x)  ((target_ulong)(x) & ~(TARGET_PAGE_SIZE - 1))

/* Return NOT_MAPPED if a page is not mapped into target physical memory */
/*        MAPPED_CONTIGUOUS if all pages are mapped into target physical memory and contiguous */
/*        MAPPED_NOT_CONTIGUOUS if all pages are mapped into target physical memory but not contiguous */
static int get_target_mem_state(CPUState *env,
                                target_ulong target_addr,
                                target_ulong len)
{
    target_ulong aligned_target_addr = TARGET_ADDR_LOW_ALIGN(target_addr);
    int to_end_page = (long)aligned_target_addr + TARGET_PAGE_SIZE - (long)target_addr;
    int ret = MAPPED_CONTIGUOUS;
    
    if (aligned_target_addr != target_addr) {
        target_ulong phys_addr = get_phys_mem_addr(env, aligned_target_addr);
        target_ulong last_phys_addr = phys_addr;
        if (phys_addr == 0) {
            return NOT_MAPPED;
        }
        if (len > to_end_page) {
            len -= to_end_page;
            aligned_target_addr += TARGET_PAGE_SIZE;
            int i;
            for (i = 0; i < len; i += TARGET_PAGE_SIZE) {
                get_phys_mem_addr(env, aligned_target_addr + i);
                if (phys_addr == 0) {
                    return NOT_MAPPED;
                }
                if (phys_addr != last_phys_addr + TARGET_PAGE_SIZE) {
                    ret = MAPPED_NOT_CONTIGUOUS;
                }
                last_phys_addr = phys_addr;
            }
        }
    } else {
        target_ulong last_phys_addr = 0;
        int i;
        for (i = 0; i < len; i += TARGET_PAGE_SIZE) {
            target_ulong phys_addr = get_phys_mem_addr(env, target_addr + i);
            if (phys_addr == 0) {
                return NOT_MAPPED;
            }
            if (i != 0 && phys_addr != last_phys_addr + TARGET_PAGE_SIZE) {
                ret = MAPPED_NOT_CONTIGUOUS;
            }
            last_phys_addr = phys_addr;
        }
    }
    return ret;
}

/* copy len bytes from host memory at addr host_addr to target memory at logical addr target_addr */
/* Returns 1 if successfull, 0 if some target pages are not mapped into target physical memory */
static int memcpy_host_to_target(CPUState *env, target_ulong target_addr, const void *host_addr, int len)
{
    void *p;
    target_phys_addr_t plen;
    target_ulong aligned_target_addr;
    int i;
    
    if (len)
        switch (get_target_mem_state(env, target_addr, len)) {
            case NOT_MAPPED:
                return 0;
            case MAPPED_CONTIGUOUS:
                plen = len;
                p = cpu_physical_memory_map(get_phys_mem_addr(env, target_addr), &plen, 1);
                if (p && plen)
                    memcpy(p, host_addr, plen);
                cpu_physical_memory_unmap(p, plen, 1, plen);
                break;
            case MAPPED_NOT_CONTIGUOUS:
                aligned_target_addr = TARGET_ADDR_LOW_ALIGN(target_addr);
                if (aligned_target_addr != target_addr) {
                    int to_end_page = (long)aligned_target_addr + TARGET_PAGE_SIZE - (long)target_addr;
                    plen = to_end_page;
                    p = cpu_physical_memory_map(get_phys_mem_addr(env, target_addr), &plen, 1);
                    if (p && plen)
                        memcpy(p, host_addr, MIN(len, to_end_page));
                    cpu_physical_memory_unmap(p, plen, 1, plen);
                    if (len <= to_end_page)
                        break;
                    len -= to_end_page;
                    host_addr += to_end_page;
                    target_addr = aligned_target_addr + TARGET_PAGE_SIZE;
                }
                for (i = 0; i < len; i += TARGET_PAGE_SIZE) {
                    plen = TARGET_PAGE_SIZE;
                    p = cpu_physical_memory_map(get_phys_mem_addr(env, target_addr + i), &plen, 1);
                    if (p && plen)
                        memcpy(p, host_addr + i, (i + TARGET_PAGE_SIZE <= len) ? TARGET_PAGE_SIZE : len & (TARGET_PAGE_SIZE - 1));
                    cpu_physical_memory_unmap(p, plen, 1, plen);
                }
                break;
            default:
                return 0;
        }
    return 1;
}

static int memcpy_target_to_host(CPUState *env, void *host_addr, target_ulong target_addr, int len)
{
    void *p;
    target_phys_addr_t plen;
    target_ulong aligned_target_addr;
    int i;
    
    if (len)
        switch (get_target_mem_state(env, target_addr, len)) {
            case NOT_MAPPED:
                return 0;
            case MAPPED_CONTIGUOUS:
                plen = len;
                p = cpu_physical_memory_map(get_phys_mem_addr(env, target_addr), &plen, 0);
                if (p && plen)
                    memcpy(host_addr, p, plen);
                cpu_physical_memory_unmap(p, plen, 0, plen);
                break;
            case MAPPED_NOT_CONTIGUOUS:
                aligned_target_addr = TARGET_ADDR_LOW_ALIGN(target_addr);
                if (aligned_target_addr != target_addr) {
                    int to_end_page = (long)aligned_target_addr + TARGET_PAGE_SIZE - (long)target_addr;
                    plen = to_end_page;
                    p = cpu_physical_memory_map(get_phys_mem_addr(env, target_addr), &plen, 0);
                    if (p && plen)
                        memcpy(host_addr, p, MIN(len, to_end_page));
                    cpu_physical_memory_unmap(p, plen, 0, plen);
                    if (len <= to_end_page)
                        break;
                    len -= to_end_page;
                    host_addr += to_end_page;
                    target_addr = aligned_target_addr + TARGET_PAGE_SIZE;
                }
                for (i = 0; i < len; i += TARGET_PAGE_SIZE) {
                    plen = TARGET_PAGE_SIZE;
                    p = cpu_physical_memory_map(get_phys_mem_addr(env, target_addr + i), &plen, 0);
                    if (p && plen)
                        memcpy(host_addr + i, p, (i + TARGET_PAGE_SIZE <= len) ? TARGET_PAGE_SIZE : len & (TARGET_PAGE_SIZE - 1));
                    cpu_physical_memory_unmap(p, plen, 0, plen);
                }
                break;
            default:
                return 0;
        }
    return 1;
}

static int host_offset = 0;
static int host_mem_size = 0;
static void *host_mem = NULL;
static target_phys_addr_t host_read_len = 0;

static void reset_host_offset(void)
{
    host_offset = 0;
}

static void free_host_read_pointer(const void *p)
{
    /* if the ptr is not inside our host_mem buffer, it must point to
     * a region previously mapped with cpu_physical_memory_map so let's
     * unmap that now... */
    if (p < host_mem || p >= host_mem + host_mem_size) {
        cpu_physical_memory_unmap((void *)p, host_read_len, 0, host_read_len);
    }
}

/* Return a host pointer with the content of [target_addr, target_addr + len bytes[ */
/* Do not modify but must be freed with free_host_read_pointer! */
static const void *get_host_read_pointer(CPUState *env, const target_ulong target_addr, int len)
{
    void *p;
    
    switch (get_target_mem_state(env, target_addr, len)) {
        case NOT_MAPPED:
            break;
        case MAPPED_CONTIGUOUS:
            host_read_len = len;
            return cpu_physical_memory_map(get_phys_mem_addr(env, target_addr), &host_read_len, 0);
        case MAPPED_NOT_CONTIGUOUS:
            if (host_mem_size < host_offset + len) {
                host_mem_size = 2 * host_mem_size + host_offset + len;
                host_mem = realloc(host_mem, host_mem_size);
            }
            p = host_mem + host_offset;
            memcpy_target_to_host(env, p, target_addr, len);
            host_offset += len;
            return p;
        default:
            break;
    }
    return NULL;
}
 
static int decode_call_int(struct helper_opengl_s *s)
{
    int func_number = s->fid;
    int pid = s->pid;
    target_ulong target_ret_string = s->rsp;
    target_ulong in_args = s->iap;
    target_ulong in_args_size = s->ias;
    TRACE("func=%d(\"%s\"), pid=%d, trs=0x%08x, in_args=0x%08x, in_args_size=0x%08x",
          func_number, tab_opengl_calls_name[func_number], pid, target_ret_string,
          in_args, in_args_size);
    Signature *signature = (Signature*)tab_opengl_calls[func_number];
    int ret_type = signature->ret_type;
    int nb_args = signature->nb_args;
    int *args_type = signature->args_type;
    int i;
    int ret;
    int args_size[MAX_GLFUNC_NB_ARGS];
    target_ulong saved_out_ptr[MAX_GLFUNC_NB_ARGS];
    static char *ret_string = NULL;
    static target_ulong args[MAX_GLFUNC_NB_ARGS];
    static target_phys_addr_t pargs[MAX_GLFUNC_NB_ARGS];
    
#ifndef QEMUGL_MULTITHREADED
    if (last_func_number == _exit_process_func && func_number == _exit_process_func) {
        last_func_number = -1;
        return 0;
    }

    if (last_process_id == 0) {
        SET_LAST_PROCESS_ID(pid);
    } else if (last_process_id != pid) {
        GL_ERROR("opengl calls from parallel processes are not supported");
        return 0;
    }
#endif
    
    if (!ret_string) {
        init_process_tab();
        ret_string = qemu_mallocz(32768);
        my_strlen = strlen;
    }
    
    reset_host_offset();
  
    if (nb_args) {
        TRACE("nb_args=%d", nb_args);
        if (memcpy_target_to_host(s->env, args, in_args, sizeof(target_ulong) * nb_args) == 0) {
            GL_ERROR("call %s pid=%d\ncannot get call parameters",
                     tab_opengl_calls_name[func_number], pid);
            SET_LAST_PROCESS_ID(0);
            return 0;
        }
        if (memcpy_target_to_host(s->env, args_size, in_args_size, sizeof(target_ulong) * nb_args) == 0) {
            GL_ERROR("call %s pid=%d\ncannot get call parameters size",
                     tab_opengl_calls_name[func_number], pid);
            SET_LAST_PROCESS_ID(0);
            return 0;
        }
    }
    
    for (i = 0; i < nb_args; i++) {
        //TRACE("arg%d type=%d size=%d value=0x%08x", i, args_type[i], args_size[i], args[i]);
        switch (args_type[i]) {
            case TYPE_UNSIGNED_INT:
            case TYPE_INT:
            case TYPE_UNSIGNED_CHAR:
            case TYPE_CHAR:
            case TYPE_UNSIGNED_SHORT:
            case TYPE_SHORT:
            case TYPE_FLOAT:
#ifdef GL_EXCESS_DEBUG
                switch (args_type[i]) {
                    case TYPE_UNSIGNED_INT: TRACE("arg%d is unsigned int, value %u", i, args[i]); break;
                    case TYPE_INT: TRACE("arg%d is signed int, value %d", i, args[i]); break;
                    case TYPE_UNSIGNED_CHAR: TRACE("arg%d is unsigned char, value %d", i, args[i]); break;
                    case TYPE_CHAR: TRACE("arg%d is signed char, value %d", i, args[i]); break;
                    case TYPE_UNSIGNED_SHORT: TRACE("arg%d is unsigned short, value %d", i, args[i]); break;
                    case TYPE_SHORT: TRACE("arg%d is signed short, value %d", i, args[i]); break;
                    case TYPE_FLOAT: TRACE("arg%d is float, value %f", i, *(float *)&args[i]); break;
                    default: break;
                }
#endif
                pargs[i] = args[i];
                break;
            case TYPE_NULL_TERMINATED_STRING:
            CASE_IN_UNKNOWN_SIZE_POINTERS:
                TRACE("arg%d is string/unknown size ptr", i);
                if (args[i] == 0 && args_size[i] == 0) {
                    if (!IS_NULL_POINTER_OK_FOR_FUNC(func_number)) {
                        GL_ERROR("call %s arg %d pid=%d",
                                 tab_opengl_calls_name[func_number], i, pid);
                        SET_LAST_PROCESS_ID(0);
                        return 0;
                    }
                    pargs[i] = 0;
                } else if (args[i] == 0 && args_size[i] != 0) {
                    GL_ERROR("call %s arg %d pid=%d args[i] == 0 && args_size[i] != 0",
                             tab_opengl_calls_name[func_number], i, pid);
                    SET_LAST_PROCESS_ID(0);
                    return 0;
                } else if (args[i] != 0 && args_size[i] == 0) {
                    GL_ERROR("call %s arg %d pid=%d args[i] != 0 && args_size[i] == 0",
                             tab_opengl_calls_name[func_number], i, pid);
                    SET_LAST_PROCESS_ID(0);
                    return 0;
                }
                if (args[i]) {
                    const void *rp = get_host_read_pointer(s->env, args[i], args_size[i]);
                    TRACE("target_addr 0x%08x host_addr %p", args[i], rp);
                    pargs[i] = (target_phys_addr_t)rp;
                    if (pargs[i] == 0) {
                        GL_ERROR("call %s arg %d pid=%d can not get %d bytes",
                                 tab_opengl_calls_name[func_number], i, pid,
                                 args_size[i]);
                        SET_LAST_PROCESS_ID(0);
                        return 0;
                    }
                }
                break;
            CASE_IN_LENGTH_DEPENDING_ON_PREVIOUS_ARGS:
                TRACE("arg%d length depends on previous args", i);
                args_size[i] = compute_arg_length(stderr, func_number, i, args);
                if (args_size[i]) {
                    const void *rp = get_host_read_pointer(s->env, args[i], args_size[i]);
                    pargs[i] = (target_phys_addr_t)rp;
                } else {
                    pargs[i] = 0;
                }
                if (pargs[i] == 0 && args_size[i] != 0) {
                    GL_ERROR("call %s arg %d pid=%d can not get %d bytes",
                             tab_opengl_calls_name[func_number], i, pid,
                             args_size[i]);
                    SET_LAST_PROCESS_ID(0);
                    return 0;
                }
                break;
            CASE_OUT_POINTERS:
                TRACE("arg%d is out_pointer", i);
                if (args[i] == 0 && args_size[i] == 0) {
                    if (!IS_NULL_POINTER_OK_FOR_FUNC(func_number)) {
                        GL_ERROR("call %s arg %d pid=%d",
                                 tab_opengl_calls_name[func_number], i, pid);
                        SET_LAST_PROCESS_ID(0);
                        return 0;
                    }
                    pargs[i] = 0;
                    GL_ERROR("call %s arg %d pid=%d\n",
                             tab_opengl_calls_name[func_number], i, pid);
                    SET_LAST_PROCESS_ID(0);
                    return 0;
                } else if (args[i] == 0 && args_size[i] != 0) {
                    GL_ERROR("call %s arg %d pid=%d args[i] == 0 && args_size[i] != 0",
                             tab_opengl_calls_name[func_number], i, pid);
                    SET_LAST_PROCESS_ID(0);
                    return 0;
                } else if (args[i] != 0 && args_size[i] == 0) {
                    GL_ERROR("call %s arg %d pid=%d args[i] != 0 && args_size[i] == 0",
                             tab_opengl_calls_name[func_number], i, pid);
                    SET_LAST_PROCESS_ID(0);
                    return 0;
                }
                if (args[i]) { // XXX
                    target_phys_addr_t plen;
                    switch (get_target_mem_state(s->env, args[i], args_size[i])) {
                        case NOT_MAPPED:
                            GL_ERROR("call %s arg %d pid=%d addr=0x%x size=%d NOT_MAPPED",
                                     tab_opengl_calls_name[func_number], i, pid,
                                     args[i], args_size[i]);
                            SET_LAST_PROCESS_ID(0);
                            return 0;
                        case MAPPED_CONTIGUOUS:
                            saved_out_ptr[i] = 0;
                            plen = args_size[i];
                            if (!(pargs[i] = (target_phys_addr_t)cpu_physical_memory_map(get_phys_mem_addr(s->env, args[i]), &plen, 1))) {
                                GL_ERROR("call %s arg %d pid=%d addr=0x%x size=%d cpu_physical_memory_map failed!",
                                         tab_opengl_calls_name[func_number], i, pid,
                                         args[i], args_size[i]);
                                SET_LAST_PROCESS_ID(0);
                                return 0;
                            }
                            break;
                        case MAPPED_NOT_CONTIGUOUS:
                            saved_out_ptr[i] = args[i];
                            pargs[i] = (target_phys_addr_t)malloc(args_size[i]);
                            break;
                        default:
                            SET_LAST_PROCESS_ID(0);
                            return 0;
                    }
                } else {
                    saved_out_ptr[i] = 0;
                    pargs[i] = 0;
                }
                break;
            case TYPE_DOUBLE:
            CASE_IN_KNOWN_SIZE_POINTERS:
                TRACE("arg%d is double/known size pointer", i);
                if (args[i] == 0) {
                    GL_ERROR("call %s arg %d pid=%d can not get %d bytes",
                             tab_opengl_calls_name[func_number], i, pid,
                             tab_args_type_length[args_type[i]]);
                    SET_LAST_PROCESS_ID(0);
                    return 0;
                }
                pargs[i] = (target_phys_addr_t)get_host_read_pointer(s->env, args[i], tab_args_type_length[args_type[i]]);
                if (pargs[i] == 0) {
                    GL_ERROR("call %s arg %d pid=%d can not get %d bytes",
                             tab_opengl_calls_name[func_number], i, pid,
                             tab_args_type_length[args_type[i]]);
                    SET_LAST_PROCESS_ID(0);
                    return 0;
                }
                break;
            case TYPE_IN_IGNORED_POINTER:
                TRACE("arg%d is ignored pointer", i);
                pargs[i] = 0;
                break;
            default:
                GL_ERROR("unknown parameter type %d: call %s arg %d pid=%d",
                         args_type[i], tab_opengl_calls_name[func_number],
                         i, pid);
                SET_LAST_PROCESS_ID(0);
                return 0;
                break;
        }
    }
    
    if (ret_type == TYPE_CONST_CHAR) {
        ret_string[0] = 0;
    }
    
    if (func_number == _init_func) {
        TRACE("_init_func called");
        ret = 0x51;
    } else {
        ret = do_function_call(s, func_number, pid, pargs, ret_string);
    }

    for (i = 0; i < nb_args; i++) {
        switch (args_type[i]) {
            case TYPE_NULL_TERMINATED_STRING:
            CASE_IN_UNKNOWN_SIZE_POINTERS:
            CASE_IN_LENGTH_DEPENDING_ON_PREVIOUS_ARGS:
                if (pargs[i]) {
                    free_host_read_pointer((void *)pargs[i]);
                }
                break;
            CASE_OUT_POINTERS:
                if (saved_out_ptr[i]) {
                    if (memcpy_host_to_target(s->env, saved_out_ptr[i], (const void *)pargs[i], args_size[i]) == 0) {
                        GL_ERROR("cannot copy out parameters back to user space");
                        SET_LAST_PROCESS_ID(0);
                        return 0;
                    }
                    free((void*)pargs[i]);
                } else {
                    if (args[i]) {
                        cpu_physical_memory_unmap((void *)pargs[i], args_size[i], 1, args_size[i]);
                    }
                }  
                break;
            case TYPE_DOUBLE:
            CASE_IN_KNOWN_SIZE_POINTERS:
                free_host_read_pointer((void *)pargs[i]);
                break;
            default:
                break;
        }
    }
    
    if (ret_type == TYPE_CONST_CHAR) {
        if (target_ret_string) {
            /* the my_strlen stuff is a hack to workaround a GCC bug if using directly strlen... */
            if (memcpy_host_to_target(s->env, target_ret_string, ret_string, my_strlen(ret_string) + 1) == 0) {
                GL_ERROR("cannot copy out parameters back to user space");
                SET_LAST_PROCESS_ID(0);
                return 0;
            }
        }
    }
    
    return ret;
}

static int decode_call(struct helper_opengl_s *s)
{
    int ret;
    int func_number=s->fid;
    if( ! (func_number >= 0 && func_number < GL_N_CALLS) ) {
        GL_ERROR("func_number >= 0 && func_number < GL_N_CALLS failed");
        return 0;
    }
    ret = decode_call_int(s);
    if (func_number == glXCreateContext_func) {
        TRACE("ret of glXCreateContext_func = %d", ret);
    }
    return ret;
}

static uint32_t opengl_buffer_read(struct helper_opengl_s *s)
{
    if (!s->bufsize) {
        GL_ERROR("buffer access out of bounds");
        return 0;
    }
    
    s->bufsize--;
#if defined(USE_OSMESA) || defined(WIN32)
    /* win32 and osmesa render scanlines in reverse order (bottom-up)
     * win32 further aligns buffer width on 32bit boundary */
    if (s->bufcol >= s->bufwidth) {
#if defined(USE_OSMESA) && defined(GLX_OSMESA_FORCE_32BPP)
        int linesize = s->bufwidth * 4;
#else
        int linesize = s->bufwidth * s->bufpixelsize;
#endif // USE_OSMESA && GLX_OSMESA_FORCE_32BPP
#ifdef USE_OSMESA
        s->buf -= linesize * 2;
#else
#ifdef WIN32
        s->buf -= linesize + ((linesize + 3) & ~3);
#endif // WIN32
#endif // USE_OSMESA
        s->bufcol = 0;
    }
    s->bufcol++;
#endif // USE_OSMESA || WIN32
    switch (s->bufpixelsize) {
#ifndef USE_OSMESA
        case 1:
            {
                uint8_t *p = (uint8_t *)s->buf;
                s->buf++;
                return *p;
            }
#endif // USE_OSMESA
        case 2:
            {
#if defined(USE_OSMESA) && defined(GLX_OSMESA_FORCE_32BPP)
                uint32_t v = *(uint32_t *)s->buf;
                s->buf += 4;
                v = ((v & 0x00f80000) >> 19) |
                    ((v & 0x0000fc00) >> 5) |
                    ((v & 0x000000f8) << 8);
                return v;
#else
                uint16_t *p = (uint16_t *)s->buf;
                s->buf += 2;
#if !defined(USE_OSMESA) && (defined(CONFIG_COCOA) || defined(WIN32))
                /* 16bit buffer actually contains 15bit data */
                uint16_t v = *p;
                return ((v & 0x7fe0) << 1) | (v & 0x001f);
#else
                return *p;
#endif // !USE_OSMESA && (CONFIG_COCOA || WIN32)
#endif // USE_OSMESA && GLX_OSMESA_FORCE_32BPP
            }
        case 4:
            {
                uint32_t *p = (uint32_t *)s->buf;
                s->buf += 4;
#ifdef USE_OSMESA
                uint32_t v = *p;
                v = ((v & 0x00ff0000) >> 16) |
                    (v & 0x0000ff00) |
                    ((v & 0x000000ff) << 16);
                return v;
#else
                return *p;
#endif // USE_OSMESA
            }
        default:
            GL_ERROR("unsupported pixel size %d bytes (guest pid %d)",
                     s->bufpixelsize, s->pid);
    }
    return 0;
}

#ifndef QEMUGL_IO_FRAMEBUFFER
static void opengl_map_copyframe(struct helper_opengl_s *s, uint32_t vaddr)
{
    target_ulong paddr = get_phys_mem_addr(s->env, vaddr);
    if (paddr) {
        s->framecopy.ptr = s->framecopy.mapped_ptr =
            cpu_physical_memory_map(paddr, &s->framecopy.mapped_len, 1);
        if (!s->framecopy.ptr) {
            TRACE("unable to map guest (pid %d) physical memory address "
                  "0x%08x", s->pid, paddr);
        }
    } else {
        TRACE("unable to get guest (pid %d) physical memory address for "
              "virtual address 0x%08x", s->pid, vaddr);
        s->framecopy.ptr = s->framecopy.mapped_ptr = NULL;
    }
}

static void opengl_init_copyframe(struct helper_opengl_s *s)
{
    target_ulong a = TARGET_ADDR_LOW_ALIGN(s->qemugl_buf);
    if (a != s->qemugl_buf) {
        s->framecopy.count = a + TARGET_PAGE_SIZE - s->qemugl_buf;
    } else {
        s->framecopy.count = TARGET_PAGE_SIZE;
    }
    s->framecopy.addr = a;
    opengl_map_copyframe(s, s->qemugl_buf);
}

static void opengl_finish_copyframe(struct helper_opengl_s *s)
{
    if (s->framecopy.mapped_ptr) {
        cpu_physical_memory_unmap(s->framecopy.mapped_ptr,
                                  s->framecopy.mapped_len, 1,
                                  s->framecopy.mapped_len);
        s->framecopy.ptr = s->framecopy.mapped_ptr = NULL;
    }
}

static void opengl_copyframe_bytes(struct helper_opengl_s *s,
                                   unsigned int le_data, int nbytes)
{
    for (; nbytes--; s->framecopy.count--, le_data >>= 8) {
        if (!s->framecopy.count) {
            opengl_finish_copyframe(s);
            s->framecopy.count = TARGET_PAGE_SIZE;
            s->framecopy.addr += s->framecopy.count;
            s->framecopy.mapped_len = s->framecopy.count;
            opengl_map_copyframe(s, s->framecopy.addr);
        }
        if (s->framecopy.ptr) {
            *(s->framecopy.ptr++) = (uint8_t)(le_data & 0xff);
        }
    }
}

void helper_opengl_copyframe(struct helper_opengl_s *s)
{
    if (s->qemugl_bufbytesperline < s->bufwidth * s->bufpixelsize) {
        GL_ERROR("invalid guest (pid %d) OpenGL framebuffer pitch", s->pid);
        return;
    }
    opengl_init_copyframe(s);
    const uint32_t pixelsize = s->bufpixelsize;
    uint32_t extra = s->qemugl_bufbytesperline - s->bufwidth * pixelsize;
    while (s->bufsize) { /* this decreases as we call opengl_buffer_read() */
        uint32_t n = s->bufwidth;
        while (n--) {
            uint32_t x = opengl_buffer_read(s);
            opengl_copyframe_bytes(s, x, pixelsize);
        }
        for (n = extra; n--;) {
            opengl_copyframe_bytes(s, 0, pixelsize);
        }
    }
    opengl_finish_copyframe(s);
}
#endif // QEMUGL_IO_FRAMEBUFFER

#ifdef QEMUGL_MULTITHREADED
static void *helper_opengl_thread(void *opaque)
{
    OpenGLThreadState *thread_state = opaque;
    do {
        if (pthread_mutex_lock(&thread_state->runlock)) {
            GL_ERROR("pthread_mutex_lock failed for runlock");
        }
        while (!thread_state->runstate) {
            if (pthread_cond_wait(&thread_state->runcond,
                                  &thread_state->runlock)) {
                GL_ERROR("pthread_cond_wait failed for runcond");
            }
        }
        thread_state->runstate = 0;
        if (pthread_mutex_unlock(&thread_state->runlock)) {
            GL_ERROR("pthread_mutex_unlock failed for runlock");
        }
        TRACE("processing request from pid %d (fid=%d)",
              thread_state->state.pid, thread_state->state.fid);
        thread_state->state.result = decode_call(&thread_state->state);
        TRACE("finished request from pid %d (fid=%d), result=%d",
              thread_state->state.pid, thread_state->state.fid,
              thread_state->state.result);
        if (pthread_mutex_lock(&thread_state->finishlock)) {
            GL_ERROR("pthread_mutex_lock failed for finishlock");
        }
        thread_state->finishstate = 1;
        if (pthread_cond_signal(&thread_state->finishcond)) {
            GL_ERROR("pthread_cond_signal failed for finishcond");
        }
        if (pthread_mutex_unlock(&thread_state->finishlock)) {
            GL_ERROR("pthread_mutex_unlock failed for finishlock");
        }
    } while (thread_state->state.fid != _exit_process_func);
    return NULL;
}

static OpenGLThreadState *helper_opengl_newthread(OpenGLState *s, uint32_t pid)
{
    OpenGLThreadState *thread_state = qemu_mallocz(sizeof(*thread_state));
    thread_state->state.env = s->env;
    thread_state->state.pid = pid;
    thread_state->regbase = QEMUGL_GLOB_HWREG_SIZE;
    if (LIST_EMPTY(&s->guest_list)) {
        LIST_INSERT_HEAD(&s->guest_list, thread_state, link);
    } else {
        OpenGLThreadState *old_state;
        LIST_FOREACH(old_state, &s->guest_list, link) {
            if (old_state->regbase > thread_state->regbase) {
                LIST_INSERT_BEFORE(old_state, thread_state, link);
                break;
            }
            thread_state->regbase += QEMUGL_HWREG_MASK + 1;
            if (thread_state->regbase >
                QEMUGL_HWREG_REGIONSIZE - (QEMUGL_HWREG_MASK + 1)) {
                GL_ERROR("too many opengl guest processes");
                qemu_free(thread_state);
                return NULL;
            }
            if (!LIST_NEXT(old_state, link)) {
                LIST_INSERT_AFTER(old_state, thread_state, link);
                break;
            }
        }
    }
    if (pthread_mutex_init(&thread_state->runlock, NULL) ||
        pthread_mutex_init(&thread_state->finishlock, NULL)) {
        hw_error("%s@%d: pthread_mutex_init failed", __FUNCTION__, __LINE__);
    }
    if (pthread_cond_init(&thread_state->runcond, NULL) ||
        pthread_cond_init(&thread_state->finishcond, NULL)) {
        hw_error("%s@%d: pthread_cond_init failed", __FUNCTION__, __LINE__);
    }
    if (pthread_create(&thread_state->thread, NULL, helper_opengl_thread,
                       thread_state)) {
        hw_error("%s@%d: pthread_create failed", __FUNCTION__, __LINE__);
    }
    s->pidquery = thread_state->regbase;
    return thread_state;
}

static void helper_opengl_removethread(OpenGLState *s,
                                       OpenGLThreadState *thread_state)
{
    pthread_join(thread_state->thread, NULL);
    LIST_REMOVE(thread_state, link);
    if (pthread_cond_destroy(&thread_state->runcond)) {
        GL_ERROR("unable to destroy opengl thread runcond for guest "
                 "process %d", thread_state->state.pid);
    }
    if (pthread_cond_destroy(&thread_state->finishcond)) {
        GL_ERROR("unable to destroy opengl thread finishcond for guest "
                 "process %d", thread_state->state.pid);
    }
    if (pthread_mutex_destroy(&thread_state->runlock)) {
        GL_ERROR("unable to destroy opengl thread runlock for guest "
                 "process %d", thread_state->state.pid);
    }
    if (pthread_mutex_destroy(&thread_state->finishlock)) {
        GL_ERROR("unable to destroy opengl thread finishlock for guest "
                 "process %d", thread_state->state.pid);
    }
    qemu_free(thread_state);
}

static void helper_opengl_runthread(OpenGLThreadState *thread_state)
{
    if (pthread_mutex_lock(&thread_state->runlock)) {
        hw_error("%s@%d: pthread_mutex_lock failed (guest pid %d)",
                 __FUNCTION__, __LINE__, thread_state->state.pid);
    }
    thread_state->runstate = 1;
    if (pthread_cond_signal(&thread_state->runcond)) {
        hw_error("%s@%d: pthread_cond_signal failed (guest pid %d)",
                 __FUNCTION__, __LINE__, thread_state->state.pid);
    }
    if (pthread_mutex_unlock(&thread_state->runlock)) {
        hw_error("%s@%d: pthread_mutex_unlock failed (guest pid %d)",
                 __FUNCTION__, __LINE__, thread_state->state.pid);
    }
}

static void helper_opengl_waitthread(OpenGLThreadState *thread_state)
{
    if (pthread_mutex_lock(&thread_state->finishlock)) {
        hw_error("%s@%d: pthread_mutex_lock failed (guest pid %d)",
                 __FUNCTION__, __LINE__, thread_state->state.pid);
    }
    while (!thread_state->finishstate) {
        if (pthread_cond_wait(&thread_state->finishcond,
                              &thread_state->finishlock)) {
            hw_error("%s@%d: pthread_cond_wait failed (guest pid %d)",
                     __FUNCTION__, __LINE__, thread_state->state.pid);
        }
    }
    thread_state->finishstate = 0;
    if (pthread_mutex_unlock(&thread_state->finishlock)) {
        hw_error("%s@%d: pthread_mutex_unlock failed (guest pid %d)",
                 __FUNCTION__, __LINE__, thread_state->state.pid);
    }
}

static OpenGLThreadState *helper_opengl_threadstate(OpenGLState *s,
                                                    target_phys_addr_t addr)
{
    OpenGLThreadState *thread_state;
    uint32_t base = ((addr - QEMUGL_GLOB_HWREG_SIZE) & ~QEMUGL_HWREG_MASK)
                    + QEMUGL_GLOB_HWREG_SIZE;
    LIST_FOREACH(thread_state, &s->guest_list, link) {
        if (thread_state->regbase == base) {
            return thread_state;
        }
    }
    return NULL;
}
#endif

static void helper_opengl_write(void *opaque, target_phys_addr_t addr,
                                uint32_t value)
{
    struct helper_opengl_s *s;
#ifdef QEMUGL_MULTITHREADED
    OpenGLThreadState *ts = helper_opengl_threadstate(opaque, addr);
    if (addr < QEMUGL_GLOB_HWREG_SIZE) {
        switch (addr) {
            case QEMUGL_GLOB_HWREG_PID:
                TRACE("request new guest for pid %d", value);
                if (!ts && !(ts = helper_opengl_newthread(opaque, value))) {
                    GL_ERROR("unable to create new thread for pid 0x%08x",
                             value);
                }
                break;
            default:
                GL_ERROR("unknown global opengl register " TARGET_FMT_plx,
                         addr);
                break;
        }
    } else if (!ts) {
        GL_ERROR("unknown guest process accessing local opengl register "
                 "0x%04x", (uint32_t)addr);
    } else {
        s = &ts->state;
#else
    s = opaque;
    if (addr < QEMUGL_GLOB_HWREG_SIZE) {
        switch (addr) {
            case QEMUGL_GLOB_HWREG_PID:
                TRACE("pid = 0x%08x", value);
                s->pid = value;
                break;
            default:
                GL_ERROR("unknown global opengl register " TARGET_FMT_plx,
                         addr);
                break;
        }
    } else {
#endif // QEMUGL_MULTITHREADED
        addr = (addr - QEMUGL_GLOB_HWREG_SIZE) & QEMUGL_HWREG_MASK;
        switch (addr) {
            case QEMUGL_HWREG_FID:
#ifdef QEMUGL_MULTITHREADED
                TRACE("fid = 0x%08x (guest pid %d)", value, s->pid);
#else
                TRACE("fid = 0x%08x", value);
#endif
                s->fid = value;
                break;
            case QEMUGL_HWREG_RSP:
#ifdef QEMUGL_MULTITHREADED
                TRACE("rsp = 0x%08x (guest pid %d)", value, s->pid);
#else
                TRACE("rsp = 0x%08x", value);
#endif
                s->rsp = value;
                break;
            case QEMUGL_HWREG_IAP:
#ifdef QEMUGL_MULTITHREADED
                TRACE("iap = 0x%08x (guest pid %d)", value, s->pid);
#else
                TRACE("iap = 0x%08x", value);
#endif
                s->iap = value;
                break;
            case QEMUGL_HWREG_IAS:
#ifdef QEMUGL_MULTITHREADED
                TRACE("ias = 0x%08x (guest pid %d)", value, s->pid);
#else
                TRACE("ias = 0x%08x", value);
#endif
                s->ias = value;
                break;
            case QEMUGL_HWREG_CMD:
                switch (value) {
                    case QEMUGL_HWCMD_RESET:
#ifdef QEMUGL_MULTITHREADED
                        TRACE("cmd = reset (guest pid %d)", s->pid);
#else
                        TRACE("cmd = reset");
#endif
#ifndef QEMUGL_MULTITHREADED
#ifndef QEMUGL_MODULE
                        if (!last_process_id)
                            break;
                        s->pid = last_process_id;
#endif // QEMUGL_MODULE
#endif // QEMUGL_MULTITHREADED
                        s->fid = _exit_process_func;
                        s->rsp = 0;
                        s->iap = 0;
                        s->ias = 0;
#ifdef QEMUGL_MULTITHREADED
                        helper_opengl_runthread(ts);
                        helper_opengl_waitthread(ts);
                        helper_opengl_removethread(opaque, ts);
                        break;
#else
                        /* fallthrough */
#endif // QEMUGL_MULTITHREADED
                    case QEMUGL_HWCMD_GLCALL:
#ifdef QEMUGL_MULTITHREADED
                        TRACE("cmd = glcall (guest pid %d)", s->pid);
                        helper_opengl_runthread(ts);
                        // we must run this synchronously and wait for
                        // completion here, otherwise guest memory mappings
                        // may change while opengl thread is processing
                        helper_opengl_waitthread(ts);
#else
                        if (value != QEMUGL_HWCMD_RESET) {
                            TRACE("cmd = glcall");
                        }
                        s->result = decode_call(s);
#endif // QEMUGL_MULTITHREADED
                        break;
                    case QEMUGL_HWCMD_SETBUF:
#ifdef QEMUGL_MULTITHREADED
                        TRACE("cmd = setbuf (guest pid %d), guest "
                              "bytes/line=%d", s->pid, s->ias);
#else
                        TRACE("cmd = setbuf, guest bytes/line=%d", s->ias);
#endif
#ifndef QEMUGL_IO_FRAMEBUFFER
                        s->qemugl_bufbytesperline = s->ias;
#else
                        /* ignored */
                        GL_ERROR("guest issuing meaningless buffer "
                                 "definition");
#endif // QEMUGL_IO_FRAMEBUFFER
                        break;
                    default:
                        GL_ERROR("unknown command 0x%08x", value);
                        break;
                }
                break;
            case QEMUGL_HWREG_STA: /* result */
            case QEMUGL_HWREG_BUF: /* drawable buffer */
                /* read-only registers, ignore */
                break;
            default:
                GL_ERROR("unknown register " TARGET_FMT_plx, addr);
                break;
        }
    }
}
    
static uint32_t helper_opengl_read(void *opaque, target_phys_addr_t addr)
{
    if (addr < QEMUGL_GLOB_HWREG_SIZE) {
        switch (addr) {
            case QEMUGL_GLOB_HWREG_PID:
#ifdef QEMUGL_MULTITHREADED
                if (((OpenGLState *)opaque)->pidquery
                    != QEMUGL_PID_SIGNATURE) {
                    uint32_t base = ((OpenGLState *)opaque)->pidquery;
                    ((OpenGLState *)opaque)->pidquery = QEMUGL_PID_SIGNATURE;
                    TRACE("PID register read, new guest base = 0x%04x", base);
                    return base;
                }
#endif
                TRACE("PID check, return 0x%08x", QEMUGL_PID_SIGNATURE);
                return QEMUGL_PID_SIGNATURE;
            default:
                GL_ERROR("unknown global opengl register " TARGET_FMT_plx,
                         addr);
                break;
        }
    } else {
#ifdef QEMUGL_MULTITHREADED
        OpenGLThreadState *ts = helper_opengl_threadstate(opaque, addr);
        if (!ts) {
            GL_ERROR("unknown guest process accessing local opengl register "
                     "0x%04x", (uint32_t)addr);
            return 0;
        }
        struct helper_opengl_s *s = &ts->state;
#else
        struct helper_opengl_s *s = opaque;
#endif
        addr = (addr - QEMUGL_GLOB_HWREG_SIZE) & QEMUGL_HWREG_MASK;
        switch (addr) {
            case QEMUGL_HWREG_FID: return s->fid;
            case QEMUGL_HWREG_RSP: return s->rsp;
            case QEMUGL_HWREG_IAP: return s->iap;
            case QEMUGL_HWREG_IAS: return s->ias;
            case QEMUGL_HWREG_CMD: return 0; /* write-only register */
            case QEMUGL_HWREG_STA:
/*#ifdef QEMUGL_MULTITHREADED
                helper_opengl_waitthread(ts);
                TRACE("result = %d (guest pid %d)", s->result, s->pid);
#else
                TRACE("result = %d", s->result);
#endif*/
                TRACE("result = %d", s->result);
                return s->result;
            case QEMUGL_HWREG_BUF:
#ifdef QEMUGL_IO_FRAMEBUFFER
                return opengl_buffer_read(s);
#else
                GL_ERROR("guest trying to access OpenGL framebuffer through I/O");
                break;
#endif // QEMUGL_IO_FRAMEBUFFER
            default:
                GL_ERROR("unknown local opengl register " TARGET_FMT_plx, addr);
                break;
        }
    }
    return 0;
}

static CPUReadMemoryFunc *helper_opengl_readfn[] = {
    helper_opengl_read,
    helper_opengl_read,
    helper_opengl_read,
};

static CPUWriteMemoryFunc *helper_opengl_writefn[] = {
    helper_opengl_write,
    helper_opengl_write,
    helper_opengl_write,
};

void *helper_opengl_init(CPUState *env)
{
    OpenGLState *s = qemu_mallocz(sizeof(*s));
    s->env = env;
#ifdef QEMUGL_MULTITHREADED
    s->pidquery = QEMUGL_PID_SIGNATURE;
    LIST_INIT(&s->guest_list);
#endif
    cpu_register_physical_memory(QEMUGL_HWREG_REGIONBASE,
                                 QEMUGL_HWREG_REGIONSIZE, 
                                 cpu_register_io_memory(helper_opengl_readfn,
                                                        helper_opengl_writefn,
                                                        s));
    return s;
}
