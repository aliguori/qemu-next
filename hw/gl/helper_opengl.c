/*
 *  Host-side implementation of GL/GLX API
 * 
 *  Copyright (c) 2006,2007 Even Rouault
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

#include "qemu-common.h"
#include "opengl_func_parse.h"
#include "helper_opengl.h"
#include "opengl_exec.h"

//#define GL_EXCESS_DEBUG

#define GL_ERROR(fmt,...) fprintf(stderr, "%s@%d: " fmt "\n", __FUNCTION__, \
                                  __LINE__, ##__VA_ARGS__)

#ifdef GL_EXCESS_DEBUG
#define TRACE(fmt,...) GL_ERROR(fmt, ##__VA_ARGS__)
#else
#define TRACE(...)
#endif

#define MAX_GLFUNC_NB_ARGS 50

extern int last_process_id;

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
 
int doing_opengl = 0;
static int last_func_number = -1;
static size_t (*my_strlen)(const char *) = NULL;

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
    
    if (last_func_number == _exit_process_func && func_number == _exit_process_func) {
        last_func_number = -1;
        return 0;
    }
    
    if (last_process_id == 0) {
        last_process_id = pid;
    } else if (last_process_id != pid) {
        GL_ERROR("opengl calls from parallel processes are not supported");
        return 0;
    }
    
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
            last_process_id = 0;
            return 0;
        }
        if (memcpy_target_to_host(s->env, args_size, in_args_size, sizeof(target_ulong) * nb_args) == 0) {
            GL_ERROR("call %s pid=%d\ncannot get call parameters size",
                     tab_opengl_calls_name[func_number], pid);
            last_process_id = 0;
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
                        last_process_id = 0;
                        return 0;
                    }
                    pargs[i] = 0;
                } else if (args[i] == 0 && args_size[i] != 0) {
                    GL_ERROR("call %s arg %d pid=%d args[i] == 0 && args_size[i] != 0",
                             tab_opengl_calls_name[func_number], i, pid);
                    last_process_id = 0;
                    return 0;
                } else if (args[i] != 0 && args_size[i] == 0) {
                    GL_ERROR("call %s arg %d pid=%d args[i] != 0 && args_size[i] == 0",
                             tab_opengl_calls_name[func_number], i, pid);
                    last_process_id = 0;
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
                        last_process_id = 0;
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
                    last_process_id = 0;
                    return 0;
                }
                break;
            CASE_OUT_POINTERS:
                TRACE("arg%d is out_pointer", i);
                if (args[i] == 0 && args_size[i] == 0) {
                    if (!IS_NULL_POINTER_OK_FOR_FUNC(func_number)) {
                        GL_ERROR("call %s arg %d pid=%d",
                                 tab_opengl_calls_name[func_number], i, pid);
                        last_process_id = 0;
                        return 0;
                    }
                    pargs[i] = 0;
                    GL_ERROR("call %s arg %d pid=%d\n",
                             tab_opengl_calls_name[func_number], i, pid);
                    last_process_id = 0;
                    return 0;
                } else if (args[i] == 0 && args_size[i] != 0) {
                    GL_ERROR("call %s arg %d pid=%d args[i] == 0 && args_size[i] != 0",
                             tab_opengl_calls_name[func_number], i, pid);
                    last_process_id = 0;
                    return 0;
                } else if (args[i] != 0 && args_size[i] == 0) {
                    GL_ERROR("call %s arg %d pid=%d args[i] != 0 && args_size[i] == 0",
                             tab_opengl_calls_name[func_number], i, pid);
                    last_process_id = 0;
                    return 0;
                }
                if (args[i]) { // XXX
                    target_phys_addr_t plen;
                    switch (get_target_mem_state(s->env, args[i], args_size[i])) {
                        case NOT_MAPPED:
                            GL_ERROR("call %s arg %d pid=%d addr=0x%x size=%d NOT_MAPPED",
                                     tab_opengl_calls_name[func_number], i, pid,
                                     args[i], args_size[i]);
                            last_process_id = 0;
                            return 0;
                        case MAPPED_CONTIGUOUS:
                            saved_out_ptr[i] = 0;
                            plen = args_size[i];
                            if (!(pargs[i] = (target_phys_addr_t)cpu_physical_memory_map(get_phys_mem_addr(s->env, args[i]), &plen, 1))) {
                                GL_ERROR("call %s arg %d pid=%d addr=0x%x size=%d cpu_physical_memory_map failed!",
                                         tab_opengl_calls_name[func_number], i, pid,
                                         args[i], args_size[i]);
                                last_process_id = 0;
                                return 0;
                            }
                            break;
                        case MAPPED_NOT_CONTIGUOUS:
                            saved_out_ptr[i] = args[i];
                            pargs[i] = (target_phys_addr_t)malloc(args_size[i]);
                            break;
                        default:
                            last_process_id = 0;
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
                    last_process_id = 0;
                    return 0;
                }
                pargs[i] = (target_phys_addr_t)get_host_read_pointer(s->env, args[i], tab_args_type_length[args_type[i]]);
                if (pargs[i] == 0) {
                    GL_ERROR("call %s arg %d pid=%d can not get %d bytes",
                             tab_opengl_calls_name[func_number], i, pid,
                             tab_args_type_length[args_type[i]]);
                    last_process_id = 0;
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
                last_process_id = 0;
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
                        last_process_id = 0;
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
                last_process_id = 0;
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
#endif
#ifdef USE_OSMESA
        s->buf -= linesize * 2;
#else
#ifdef WIN32
        s->buf -= linesize + ((linesize + 3) & ~3);
#endif
#endif
        s->bufcol = 0;
    }
    s->bufcol++;
#endif
    switch (s->bufpixelsize) {
#ifndef USE_OSMESA
        case 1:
            {
                uint8_t *p = (uint8_t *)s->buf;
                s->buf++;
                return *p;
            }
#endif
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
#endif
#endif
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
#endif
            }
        default:
            GL_ERROR("unsupported pixel size %d bytes",
                     s->bufpixelsize);
    }
    return 0;
}

static void helper_opengl_write(void *opaque, target_phys_addr_t addr, uint32_t value)
{
    struct helper_opengl_s *s = (struct helper_opengl_s *)opaque;
    
    //fprintf(stderr, "%s: 0x%02x = 0x%08x\n", __FUNCTION__, (int)(addr & 0xff), value);
    switch (addr) {
        case 0x00: /* function id */
            s->fid = value;
            break;
        case 0x04: /* pid */
            s->pid = value;
            break;
        case 0x08: /* return string ptr */
            s->rsp = value;
            break;
        case 0x0c: /* input args ptr */
            s->iap = value;
            break;
        case 0x10: /* input args size */
            s->ias = value;
            break;
        case 0x14: /* launch */
            switch (value) {
                case 0xfeedcafe:
                    if (!last_process_id)
                        break;
                    s->fid = _exit_process_func;
                    s->pid = last_process_id;
                    s->rsp = 0;
                    s->iap = 0;
                    s->ias = 0;
                    /* fallthrough */
                case 0xdeadbeef:
                    doing_opengl = 1;
                    s->result = decode_call(s);
                    doing_opengl = 0;
                    break;
                default:
                    GL_ERROR("unknown launch command 0x%08x", value);
                    break;
            }
            break;
        case 0x18: /* result */
        case 0x1c: /* drawable buffer */
            /* read-only registers, ignore */
            break;
        default:
            GL_ERROR("unknown register " TARGET_FMT_plx, addr);
            break;
    }
}

static uint32_t helper_opengl_read(void *opaque, target_phys_addr_t addr)
{
    struct helper_opengl_s *s = (struct helper_opengl_s *)opaque;
    
    switch (addr) {
        case 0x00: return s->fid;
        case 0x04: return s->pid;
        case 0x08: return s->rsp;
        case 0x0c: return s->iap;
        case 0x10: return s->ias;
        case 0x14: return 0; /* write-only register */
        case 0x18: return s->result;
        case 0x1c: return opengl_buffer_read(s);
        default:
            GL_ERROR("unknown register " TARGET_FMT_plx, addr);
            break;
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

void *helper_opengl_init(CPUState *env, target_phys_addr_t base)
{
    struct helper_opengl_s *s = (struct helper_opengl_s *)qemu_mallocz(
        sizeof(struct helper_opengl_s));
    s->env=env;
    cpu_register_physical_memory(base, 0x100, 
                                 cpu_register_io_memory(helper_opengl_readfn,
                                                        helper_opengl_writefn,
                                                        s));
    TRACE("registered IO memory at base address 0x%08x", (target_ulong)base);
    return s;
}
