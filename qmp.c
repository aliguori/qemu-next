#include "qemu-common.h"
#include "gdbstub.h"
#include "net.h"
#include "net/slirp.h"
#include "qemu-char.h"
#include "ui/qemu-spice.h"
#include "sysemu.h"
#include "monitor.h"
#include "readline.h"
#include "console.h"
#include "blockdev.h"
#include "audio/audio.h"
#include "qemu-timer.h"
#include "migration.h"
#include "kvm.h"
#include "acl.h"
#include "osdep.h"
#include "exec-all.h"
#ifdef CONFIG_SIMPLE_TRACE
#include "trace.h"
#endif
#include "ui/qemu-spice.h"
#include "qmp-core.h"

void qmp_qmp_capabilities(QmpState *state, Error **errp)
{
}

VersionInfo *qmp_query_version(Error **err)
{
    VersionInfo *info = qmp_alloc_version_info();
    const char *version = QEMU_VERSION;
    char *tmp;

    info->qemu.major = strtol(version, &tmp, 10);
    tmp++;
    info->qemu.minor = strtol(tmp, &tmp, 10);
    tmp++;
    info->qemu.micro = strtol(tmp, &tmp, 10);
    info->package = qemu_strdup(QEMU_PKGVERSION);

    return info;
}

NameInfo *qmp_query_name(Error **errp)
{
    NameInfo *info = qmp_alloc_name_info();

    if (qemu_name) {
        info->has_name = true;
        info->name = qemu_strdup(qemu_name);
    }

    return info;
}

UuidInfo *qmp_query_uuid(Error **errp)
{
    UuidInfo *info = qmp_alloc_uuid_info();
    char uuid[64];

    snprintf(uuid, sizeof(uuid), UUID_FMT, qemu_uuid[0], qemu_uuid[1],
                   qemu_uuid[2], qemu_uuid[3], qemu_uuid[4], qemu_uuid[5],
                   qemu_uuid[6], qemu_uuid[7], qemu_uuid[8], qemu_uuid[9],
                   qemu_uuid[10], qemu_uuid[11], qemu_uuid[12], qemu_uuid[13],
                   qemu_uuid[14], qemu_uuid[15]);

    info->UUID = qemu_strdup(uuid);
    return info;
}

CpuInfo *qmp_query_cpus(Error **errp)
{
    CPUState *env;
    CpuInfo *cpu_list = NULL;

    for(env = first_cpu; env != NULL; env = env->next_cpu) {
        CpuInfo *info;

        cpu_synchronize_state(env);

        info = qmp_alloc_cpu_info();
        info->CPU = env->cpu_index;
        info->current = (env == first_cpu);
        info->halted = env->halted;
#if defined(TARGET_I386)
        info->has_pc = true;
        info->pc = env->eip + env->segs[R_CS].base;
#elif defined(TARGET_PPC)
        info->has_nip = true;
        info->nip = env->nip;
#elif defined(TARGET_SPARC)
        info->has_pc = true;
        info->pc = env->pc;
        info->has_npc = true;
        info->npc = env->npc;
#elif defined(TARGET_MIPS)
        info->has_PC = true;
        info->PC = env->active_tc.PC;
#endif

        info->next = cpu_list;
        cpu_list = info;
    }

    return cpu_list;
}

void qmp_cpu(int64_t index, Error **errp)
{
    /* Just do nothing */
}

void qmp_quit(Error **err)
{
    no_shutdown = 0;
    qemu_system_shutdown_request();
}

void qmp_change_vnc_password(const char *password, Error **err)
{
    if (vnc_display_password(NULL, password) < 0) {
        error_set(err, QERR_SET_PASSWD_FAILED);
    }
}

void qmp_change_vnc_listen(const char *target, Error **err)
{
    if (vnc_display_open(NULL, target) < 0) {
        error_set(err, QERR_VNC_SERVER_FAILED, target);
    }
}

void qmp_change(const char *device, const char *target,
                bool has_arg, const char *arg, Error **err)
{
    if (strcmp(device, "vnc") == 0) {
        if (strcmp(target, "passwd") == 0 || strcmp(target, "password") == 0) {
            if (!has_arg || !arg[0]) {
                vnc_display_disable_login(NULL);
            } else {
                qmp_change_vnc_password(arg, err);
            }
        } else {
            qmp_change_vnc_listen(target, err);
        }
    } else {
        deprecated_qmp_change_blockdev(device, target, has_arg, arg, err);
    }
}

void qmp_set_password(const char *protocol, const char *password,
                      bool has_connected, const char *connected, Error **errp)
{
    int disconnect_if_connected = 0;
    int fail_if_connected = 0;
    int rc;

    if (has_connected) {
        if (strcmp(connected, "fail") == 0) {
            fail_if_connected = 1;
        } else if (strcmp(connected, "disconnect") == 0) {
            disconnect_if_connected = 1;
        } else if (strcmp(connected, "keep") == 0) {
            /* nothing */
        } else {
            error_set(errp, QERR_INVALID_PARAMETER, "connected");
            return;
        }
    }

    if (strcmp(protocol, "spice") == 0) {
        if (!using_spice) {
            /* correct one? spice isn't a device ,,, */
            error_set(errp, QERR_DEVICE_NOT_ACTIVE, "spice");
            return;
        }
        rc = qemu_spice_set_passwd(password, fail_if_connected,
                                   disconnect_if_connected);
        if (rc != 0) {
            error_set(errp, QERR_SET_PASSWD_FAILED);
        }
        return;
    }

    if (strcmp(protocol, "vnc") == 0) {
        if (fail_if_connected || disconnect_if_connected) {
            /* vnc supports "connected=keep" only */
            error_set(errp, QERR_INVALID_PARAMETER, "connected");
            return;
        }
        /* Note that setting an empty password will not disable login through
         * this interface. */
        rc = vnc_display_password(NULL, password);
        if (rc != 0) {
            error_set(errp, QERR_SET_PASSWD_FAILED);
        }
        return;
    }

    error_set(errp, QERR_INVALID_PARAMETER, "protocol");
}

void qmp_expire_password(const char *protocol, const char *whenstr,
                         Error **errp)
{
    time_t when;
    int rc;

    if (strcmp(whenstr, "now")) {
        when = 0;
    } else if (strcmp(whenstr, "never")) {
        when = TIME_MAX;
    } else if (whenstr[0] == '+') {
        when = time(NULL) + strtoull(whenstr+1, NULL, 10);
    } else {
        when = strtoull(whenstr, NULL, 10);
    }

    if (strcmp(protocol, "spice") == 0) {
        if (!using_spice) {
            /* correct one? spice isn't a device ,,, */
            error_set(errp, QERR_DEVICE_NOT_ACTIVE, "spice");
            return;
        }
        rc = qemu_spice_set_pw_expire(when);
        if (rc != 0) {
            error_set(errp, QERR_SET_PASSWD_FAILED);
        }
        return;
    }

    if (strcmp(protocol, "vnc") == 0) {
        rc = vnc_display_pw_expire(NULL, when);
        if (rc != 0) {
            error_set(errp, QERR_SET_PASSWD_FAILED);
        }
        return;
    }

    error_set(errp, QERR_INVALID_PARAMETER, "protocol");
}

void qmp_screendump(const char *filename, Error **errp)
{
    vga_hw_screen_dump(filename, errp);
}

void qmp_stop(Error **errp)
{
    vm_stop(EXCP_INTERRUPT);
}

static void qmp_encrypted_bdrv_it(void *opaque, BlockDriverState *bs)
{
    Error **err = opaque;

    if (!error_is_set(err) && bdrv_key_required(bs)) {
        error_set(err, QERR_DEVICE_ENCRYPTED, bdrv_get_device_name(bs),
                  bdrv_get_encrypted_filename(bs));
        return;
    }
}

void qmp_cont(Error **errp)
{
    Error *local_err = NULL;

    if (incoming_expected) {
        error_set(errp, QERR_MIGRATION_EXPECTED);
        return;
    }

    bdrv_iterate(qmp_encrypted_bdrv_it, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    vm_start();
}

void qmp_memsave(int64_t addr, int64_t size, const char *filename,
                 bool has_cpu, int64_t cpu, Error **errp)
{
    FILE *f;
    uint32_t l;
    CPUState *env;
    uint8_t buf[1024];

    if (!has_cpu) {
        cpu = 0;
    }

    for (env = first_cpu; env; env = env->next_cpu) {
        if (cpu == env->cpu_index) {
            break;
        }
    }

    if (env == NULL) {
        error_set(errp, QERR_INVALID_PARAMETER_VALUE, "cpu", "a valid cpu");
        return;
    }

    f = fopen(filename, "wb");
    if (!f) {
        error_set(errp, QERR_OPEN_FILE_FAILED, filename);
        return;
    }
    while (size != 0) {
        l = sizeof(buf);
        if (l > size)
            l = size;
        cpu_memory_rw_debug(env, addr, buf, l, 0);
        if (fwrite(buf, 1, l, f) != l) {
            error_set(errp, QERR_IO_ERROR);
            goto exit;
        }
        addr += l;
        size -= l;
    }

exit:
    fclose(f);
}

void qmp_pmemsave(int64_t addr, int64_t size, const char *filename,
                  Error **errp)
{
    FILE *f;
    uint32_t l;
    uint8_t buf[1024];

    f = fopen(filename, "wb");
    if (!f) {
        error_set(errp, QERR_OPEN_FILE_FAILED, filename);
        return;
    }
    while (size != 0) {
        l = sizeof(buf);
        if (l > size)
            l = size;
        cpu_physical_memory_rw(addr, buf, l, 0);
        if (fwrite(buf, 1, l, f) != l) {
            error_set(errp, QERR_IO_ERROR);
            goto exit;
        }
        addr += l;
        size -= l;
    }

exit:
    fclose(f);
}

void qmp_system_reset(Error **errp)
{
    qemu_system_reset_request();
}

void qmp_system_powerdown(Error **erp)
{
    qemu_system_powerdown_request();
}

KvmInfo *qmp_query_kvm(Error **errp)
{
    KvmInfo *info = qmp_alloc_kvm_info();
    info->enabled = kvm_enabled();
#ifdef CONFIG_KVM
    info->present = true;
#endif
    return info;
}

StatusInfo *qmp_query_status(Error **errp)
{
    StatusInfo *info = qmp_alloc_status_info();

    info->running = vm_running;
    info->singlestep = singlestep;

    return info;
}
