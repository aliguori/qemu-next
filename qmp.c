
static QObject *qmp_commit(const QDict *args)
{
    int all_devices;
    DriveInfo *dinfo;
    const char *device = qdict_get_str(args, "device");

    all_devices = !strcmp(device, "all");
    QTAILQ_FOREACH(dinfo, &drives, next) {
        if (!all_devices)
            if (strcmp(bdrv_get_device_name(dinfo->bdrv), device))
                continue;
        bdrv_commit(dinfo->bdrv);
    }

    return NULL;
}

/**
 * do_info_version(): Show QEMU version
 *
 * Return a QDict with the following information:
 *
 * - "qemu": QEMU's version
 * - "package": package's version
 *
 * Example:
 *
 * { "qemu": "0.11.50", "package": "" }
 */
static QObject *qmp_info_version(const QDict *args)
{
    return qobject_from_jsonf("{ 'qemu': %s, 'package': %s }",
                              QEMU_VERSION, QEMU_PKGVERSION);
}

/**
 * do_info_name(): Show VM name
 *
 * Return a QDict with the following information:
 *
 * - "name": VM's name (optional)
 *
 * Example:
 *
 * { "name": "qemu-name" }
 */
static QObject *qmp_info_name(const QDict *args)
{
    if (qemu_name) {
        return qobject_from_jsonf("{'name': %s }", qemu_name);
    }

    return NULL;
}

#if defined(TARGET_I386)
/**
 * do_info_hpet(): Show HPET state
 *
 * Return a QDict with the following information:
 *
 * - "enabled": true if hpet if enabled, false otherwise
 *
 * Example:
 *
 * { "enabled": true }
 */
static QObject *qmp_info_hpet(const QDict *args)
{
    return qobject_from_jsonf("{ 'enabled': %i }", !no_hpet);
}
#endif

/**
 * do_info_uuid(): Show VM UUID
 *
 * Return a QDict with the following information:
 *
 * - "UUID": Universally Unique Identifier
 *
 * Example:
 *
 * { "UUID": "550e8400-e29b-41d4-a716-446655440000" }
 */
static QObject *qmp_info_uuid(const QDict *args)
{
    char uuid[64];

    snprintf(uuid, sizeof(uuid), UUID_FMT, qemu_uuid[0], qemu_uuid[1],
                   qemu_uuid[2], qemu_uuid[3], qemu_uuid[4], qemu_uuid[5],
                   qemu_uuid[6], qemu_uuid[7], qemu_uuid[8], qemu_uuid[9],
                   qemu_uuid[10], qemu_uuid[11], qemu_uuid[12], qemu_uuid[13],
                   qemu_uuid[14], qemu_uuid[15]);
    return qobject_from_jsonf("{ 'UUID': %s }", uuid);
}

static CPUState *find_cpu(int cpu_index)
{
    CPUState *env;

    for(env = first_cpu; env != NULL; env = env->next_cpu) {
        if (env->cpu_index == cpu_index) {
            return env;
        }
    }
    return NULL;
}

/**
 * do_info_cpus(): Show CPU information
 *
 * Return a QList. Each CPU is represented by a QDict, which contains:
 *
 * - "cpu": CPU index
 * - "current": true if this is the current CPU, false otherwise
 * - "halted": true if the cpu is halted, false otherwise
 * - Current program counter. The key's name depends on the architecture:
 *      "pc": i386/x86)64
 *      "nip": PPC
 *      "pc" and "npc": sparc
 *      "PC": mips
 *
 * Example:
 *
 * [ { "CPU": 0, "current": true, "halted": false, "pc": 3227107138 },
 *   { "CPU": 1, "current": false, "halted": true, "pc": 7108165 } ]
 */
static QObject *qmp_info_cpus(const QDict *args)
{
    CPUState *env;
    QList *cpu_list;

    cpu_list = qlist_new();

    for(env = first_cpu; env != NULL; env = env->next_cpu) {
        QDict *cpu;
        QObject *obj;

        cpu_synchronize_state(env);

        obj = qobject_from_jsonf("{ 'CPU': %d, 'halted': %i }",
                                 env->cpu_index,
                                 env->halted);

        cpu = qobject_to_qdict(obj);

#if defined(TARGET_I386)
        qdict_put(cpu, "pc", qint_from_int(env->eip + env->segs[R_CS].base));
#elif defined(TARGET_PPC)
        qdict_put(cpu, "nip", qint_from_int(env->nip));
#elif defined(TARGET_SPARC)
        qdict_put(cpu, "pc", qint_from_int(env->pc));
        qdict_put(cpu, "npc", qint_from_int(env->npc));
#elif defined(TARGET_MIPS)
        qdict_put(cpu, "PC", qint_from_int(env->active_tc.PC));
#endif

        qlist_append(cpu_list, cpu);
    }

    return cpu_list;
}

static QObject *eject_device(BlockDriverState *bs, int force)
{
    if (bdrv_is_inserted(bs)) {
        if (!force) {
            if (!bdrv_is_removable(bs)) {
                return qerror_new(QERR_DEVICE_NOT_REMOVABLE,
                                  bdrv_get_device_name(bs));
            }
            if (bdrv_is_locked(bs)) {
                return qerror_new(QERR_DEVICE_LOCKED, bdrv_get_device_name(bs));
            }
        }
        bdrv_close(bs);
    }
    return NULL;
}

static QObject *qmp_eject(const QDict *args)
{
    BlockDriverState *bs;
    int force = qdict_get_int(args, "force");
    const char *filename = qdict_get_str(args, "device");

    bs = bdrv_find(filename);
    if (!bs) {
        return qerror_new(QERR_DEVICE_NOT_FOUND, filename);
    }
    return eject_device(mon, bs, force);
}

static QObject *qmp_block_set_passwd(const QDict *args)
{
    BlockDriverState *bs;
    int err;

    bs = bdrv_find(qdict_get_str(args, "device"));
    if (!bs) {
        return qerror_new(QERR_DEVICE_NOT_FOUND,
                          qdict_get_str(args, "device"));

    }

    err = bdrv_set_key(bs, qdict_get_str(args, "password"));
    if (err == -EINVAL) {
        return qerror_new(QERR_DEVICE_NOT_ENCRYPTED, bdrv_get_device_name(bs));
    } else if (err < 0) {
        return qerror_new(QERR_INVALID_PASSWORD);
    }

    return NULL;
}

static QObject *qmp_change_block(const char *device,
                                 const char *filename, const char *fmt)
{
    BlockDriverState *bs;
    BlockDriver *drv = NULL;
    int bdrv_flags;

    bs = bdrv_find(device);
    if (!bs) {
        return qerror_new(QERR_DEVICE_NOT_FOUND, device);
    }
    if (fmt) {
        drv = bdrv_find_whitelisted_format(fmt);
        if (!drv) {
            return qerror_new(QERR_INVALID_BLOCK_FORMAT, fmt);
        }
    }
    if (eject_device(mon, bs, 0) < 0) {
        return qerror_new(QERR_UNDEFINED_ERROR);
    }
    bdrv_flags = bdrv_get_type_hint(bs) == BDRV_TYPE_CDROM ? 0 : BDRV_O_RDWR;
    if (bdrv_open(bs, filename, bdrv_flags, drv) < 0) {
        return qerror_new(QERR_OPEN_FILE_FAILED, filename);
    }
    if (bdrv_key_required(bs)) {
        return qerror_new(QERR_DEVICE_PASSWORD_REQUIRED, device);
    }
    return NULL;
}

static QObject *do_change_vnc(const char *target, const char *arg)
{
    if (strcmp(target, "passwd") == 0 ||
        strcmp(target, "password") == 0) {
        char password[9];
        strncpy(password, arg, sizeof(password));
        password[sizeof(password) - 1] = '\0';
        if (vnc_display_password(NULL, password) < 0) {
            return qerror_new(QERR_SET_PASSWD_FAILED);
        }
    } else if (vnc_display_open(NULL, target) < 0) {
        return qerror_new(QERR_VNC_SERVER_FAILED, target);
    }

    return NULL;
}

/**
 * do_change(): Change a removable medium, or VNC configuration
 */
static QObject *qmp_change(const QDict *args)
{
    const char *device = qdict_get_str(args, "device");
    const char *target = qdict_get_str(args, "target");
    const char *arg = qdict_get_try_str(args, "arg");
    QObject *ret;

    if (strcmp(device, "vnc") == 0) {
        ret = do_change_vnc(target, arg);
    } else {
        ret = do_change_block(device, target, arg);
    }

    return ret;
}

static QObject *qmp_screen_dump(const QDict *args)
{
    /* FIXME this should return binary data, need Jan's patch */
    vga_hw_screen_dump(qdict_get_str(args, "filename"));
    return 0;
}

static QObject *qmp_logfile(const QDict *args)
{
    cpu_set_log_filename(qdict_get_str(args, "filename"));
    return NULL;
}

static QObject *qmp_log(const QDict *args)
{
    int mask;
    const char *items = qdict_get_str(args, "items");

    if (!strcmp(items, "none")) {
        mask = 0;
    } else {
        mask = cpu_str_to_log_mask(items);
        if (!mask) {
            return qerror_new(QERR_INVALID_PARAMETER, "items");
        }
    }
    cpu_set_log(mask);
    return NULL;
}

static QObject *qmp_singlestep(const QDict *args)
{
    const char *option = qdict_get_try_str(args, "option");
    if (!option || !strcmp(option, "on")) {
        singlestep = 1;
    } else if (!strcmp(option, "off")) {
        singlestep = 0;
    } else {
        return qerror_new(QERR_INVALID_PARAMETER_VALUE,
                          "option", "on or off");
    }
    return NULL;
}

/**
 * do_stop(): Stop VM execution
 */
static QObject *qmp_stop(const QDict *args)
{
    vm_stop(EXCP_INTERRUPT);
    return NULL;
}

/**
 * do_cont(): Resume emulation.
 */
static QObject *qmp_cont(const QDict *args)
{
    vm_start();
    return NULL;
}

static QObject *qmp_gdbserver(const QDict *args)
{
    const char *device = qdict_get_try_str(args, "device");
    if (!device) {
        device = "tcp::" DEFAULT_GDBSTUB_PORT;
    }
    if (gdbserver_start(device) < 0) {
        return qerror_new(QERR_UNDEFINED_ERROR);
    }
}

static QObject *qmp_watchdog_action(const QDict *args)
{
    const char *action = qdict_get_str(args, "action");
    if (select_watchdog_action(action) == -1) {
        return qerror_new(QERR_INVALID_PARAMETER, "action");
    }
    return NULL;
}

static QObject *qmp_memory_save(const QDict *args)
{
    FILE *f;
    uint32_t size = qdict_get_int(args, "size");
    const char *filename = qdict_get_str(args, "filename");
    target_long addr = qdict_get_int(args, "val");
    int cpu_index = qdict_get_int(args, "cpu_index");
    uint32_t l;
    CPUState *env;
    uint8_t buf[1024];
    QError *err;

    env = find_cpu(cpu_index);
    if (env == NULL) {
        return qerror_new(QERR_INVALID_PARAMETER, "cpu_index");
    }

    f = fopen(filename, "wb");
    if (!f) {
        return qerror_new(QERR_OPEN_FILE_FAILED, filename);
    }
    while (size != 0) {
        l = sizeof(buf);
        if (l > size)
            l = size;
        cpu_memory_rw_debug(env, addr, buf, l, 0);
        if (fwrite(buf, 1, l, f) != l) {
            ret = qerror_new(QERR_IO_ERROR, "fwrite");
            goto exit;
        }
        addr += l;
        size -= l;
    }

    ret = NULL;

exit:
    fclose(f);
    return ret;
}

static QObject *qmp_physical_memory_save(const QDict *args)
{
    FILE *f;
    uint32_t l;
    uint8_t buf[1024];
    uint32_t size = qdict_get_int(args, "size");
    const char *filename = qdict_get_str(args, "filename");
    target_phys_addr_t addr = qdict_get_int(args, "val");
    QObject *ret;

    f = fopen(filename, "wb");
    if (!f) {
        return qerror_new(QERR_OPEN_FILE_FAILED, filename);
    }
    while (size != 0) {
        l = sizeof(buf);
        if (l > size)
            l = size;
        cpu_physical_memory_rw(addr, buf, l, 0);
        if (fwrite(buf, 1, l, f) != l) {
            ret = qerror_new(QERR_IO_ERROR, "fwrite");
            goto exit;
        }
        fflush(f);
        addr += l;
        size -= l;
    }

    ret = NULL;

exit:
    fclose(f);
    return ret;
}

typedef struct {
    int keycode;
    const char *name;
} KeyDef;

static const KeyDef key_defs[] = {
    { 0x2a, "shift" },
    { 0x36, "shift_r" },

    { 0x38, "alt" },
    { 0xb8, "alt_r" },
    { 0x64, "altgr" },
    { 0xe4, "altgr_r" },
    { 0x1d, "ctrl" },
    { 0x9d, "ctrl_r" },

    { 0xdd, "menu" },

    { 0x01, "esc" },

    { 0x02, "1" },
    { 0x03, "2" },
    { 0x04, "3" },
    { 0x05, "4" },
    { 0x06, "5" },
    { 0x07, "6" },
    { 0x08, "7" },
    { 0x09, "8" },
    { 0x0a, "9" },
    { 0x0b, "0" },
    { 0x0c, "minus" },
    { 0x0d, "equal" },
    { 0x0e, "backspace" },

    { 0x0f, "tab" },
    { 0x10, "q" },
    { 0x11, "w" },
    { 0x12, "e" },
    { 0x13, "r" },
    { 0x14, "t" },
    { 0x15, "y" },
    { 0x16, "u" },
    { 0x17, "i" },
    { 0x18, "o" },
    { 0x19, "p" },

    { 0x1c, "ret" },

    { 0x1e, "a" },
    { 0x1f, "s" },
    { 0x20, "d" },
    { 0x21, "f" },
    { 0x22, "g" },
    { 0x23, "h" },
    { 0x24, "j" },
    { 0x25, "k" },
    { 0x26, "l" },

    { 0x2c, "z" },
    { 0x2d, "x" },
    { 0x2e, "c" },
    { 0x2f, "v" },
    { 0x30, "b" },
    { 0x31, "n" },
    { 0x32, "m" },
    { 0x33, "comma" },
    { 0x34, "dot" },
    { 0x35, "slash" },

    { 0x37, "asterisk" },

    { 0x39, "spc" },
    { 0x3a, "caps_lock" },
    { 0x3b, "f1" },
    { 0x3c, "f2" },
    { 0x3d, "f3" },
    { 0x3e, "f4" },
    { 0x3f, "f5" },
    { 0x40, "f6" },
    { 0x41, "f7" },
    { 0x42, "f8" },
    { 0x43, "f9" },
    { 0x44, "f10" },
    { 0x45, "num_lock" },
    { 0x46, "scroll_lock" },

    { 0xb5, "kp_divide" },
    { 0x37, "kp_multiply" },
    { 0x4a, "kp_subtract" },
    { 0x4e, "kp_add" },
    { 0x9c, "kp_enter" },
    { 0x53, "kp_decimal" },
    { 0x54, "sysrq" },

    { 0x52, "kp_0" },
    { 0x4f, "kp_1" },
    { 0x50, "kp_2" },
    { 0x51, "kp_3" },
    { 0x4b, "kp_4" },
    { 0x4c, "kp_5" },
    { 0x4d, "kp_6" },
    { 0x47, "kp_7" },
    { 0x48, "kp_8" },
    { 0x49, "kp_9" },

    { 0x56, "<" },

    { 0x57, "f11" },
    { 0x58, "f12" },

    { 0xb7, "print" },

    { 0xc7, "home" },
    { 0xc9, "pgup" },
    { 0xd1, "pgdn" },
    { 0xcf, "end" },

    { 0xcb, "left" },
    { 0xc8, "up" },
    { 0xd0, "down" },
    { 0xcd, "right" },

    { 0xd2, "insert" },
    { 0xd3, "delete" },
#if defined(TARGET_SPARC) && !defined(TARGET_SPARC64)
    { 0xf0, "stop" },
    { 0xf1, "again" },
    { 0xf2, "props" },
    { 0xf3, "undo" },
    { 0xf4, "front" },
    { 0xf5, "copy" },
    { 0xf6, "open" },
    { 0xf7, "paste" },
    { 0xf8, "find" },
    { 0xf9, "cut" },
    { 0xfa, "lf" },
    { 0xfb, "help" },
    { 0xfc, "meta_l" },
    { 0xfd, "meta_r" },
    { 0xfe, "compose" },
#endif
    { 0, NULL },
};

static int get_keycode(const char *key)
{
    const KeyDef *p;
    char *endp;
    int ret;

    for(p = key_defs; p->name != NULL; p++) {
        if (!strcmp(key, p->name))
            return p->keycode;
    }
    if (strstart(key, "0x", NULL)) {
        ret = strtoul(key, &endp, 0);
        if (*endp == '\0' && ret >= 0x01 && ret <= 0xff)
            return ret;
    }
    return qerror_new(QERR_UNDEFINED_ERROR);
}

#define MAX_KEYCODES 16
static uint8_t keycodes[MAX_KEYCODES];
static int nb_pending_keycodes;
static QEMUTimer *key_timer;

static void release_keys(void *opaque)
{
    int keycode;

    while (nb_pending_keycodes > 0) {
        nb_pending_keycodes--;
        keycode = keycodes[nb_pending_keycodes];
        if (keycode & 0x80)
            kbd_put_keycode(0xe0);
        kbd_put_keycode(keycode | 0x80);
    }
}

static QObject *qmp_sendkey(const QDict *args)
{
    char keyname_buf[16];
    char *separator;
    int keyname_len, keycode, i;
    const char *string = qdict_get_str(args, "keys");
    int has_hold_time = qdict_haskey(args, "hold_time");
    int hold_time = qdict_get_try_int(args, "hold_time", -1);

    if (nb_pending_keycodes > 0) {
        qemu_del_timer(key_timer);
        release_keys(NULL);
    }
    if (!has_hold_time)
        hold_time = 100;
    i = 0;
    while (1) {
        separator = strchr(string, '-');
        keyname_len = separator ? separator - string : strlen(string);
        if (keyname_len > 0) {
            pstrcpy(keyname_buf, sizeof(keyname_buf), string);
            if (keyname_len > sizeof(keyname_buf) - 1) {
                return qerror_new(QERR_INVALID_PARAMETER, "keys");
            }
            if (i == MAX_KEYCODES) {
                return qerror_new(QERR_INVALID_PARAMETER, "keys");
            }
            keyname_buf[keyname_len] = 0;
            keycode = get_keycode(keyname_buf);
            if (keycode < 0) {
                return qerror_new(QERR_INVALID_PARAMETER_VALUE,
                                  keyname_buf, "valid key");
            }
            keycodes[i++] = keycode;
        }
        if (!separator)
            break;
        string = separator + 1;
    }
    nb_pending_keycodes = i;
    /* key down events */
    for (i = 0; i < nb_pending_keycodes; i++) {
        keycode = keycodes[i];
        if (keycode & 0x80)
            kbd_put_keycode(0xe0);
        kbd_put_keycode(keycode & 0x7f);
    }
    /* delayed key up events */
    qemu_mod_timer(key_timer, qemu_get_clock(vm_clock) +
                   muldiv64(get_ticks_per_sec(), hold_time, 1000));

    return NULL;
}

static int mouse_button_state;

static QObject *qmp_mouse_move(const QDict *args)
{
    int dx, dy, dz;
    const char *dx_str = qdict_get_str(args, "dx_str");
    const char *dy_str = qdict_get_str(args, "dy_str");
    const char *dz_str = qdict_get_try_str(args, "dz_str");
    dx = strtol(dx_str, NULL, 0);
    dy = strtol(dy_str, NULL, 0);
    dz = 0;
    if (dz_str)
        dz = strtol(dz_str, NULL, 0);
    kbd_mouse_event(dx, dy, dz, mouse_button_state);
    return NULL;
}

static QObject *qmp_mouse_button(const QDict *args)
{
    int button_state = qdict_get_int(args, "button_state");
    mouse_button_state = button_state;
    kbd_mouse_event(0, 0, 0, mouse_button_state);
    return NULL;
}

static QObject *qmp_ioport_read(const QDict *args)
{
    int size = qdict_get_int(args, "size");
    int addr = qdict_get_int(args, "addr");
    int has_index = qdict_haskey(args, "index");
    uint32_t val;
    int suffix;

    if (has_index) {
        int index = qdict_get_int(args, "index");
        cpu_outb(addr & IOPORTS_MASK, index & 0xff);
        addr++;
    }
    addr &= 0xffff;

    switch(size) {
    default:
    case 1:
        val = cpu_inb(addr);
        suffix = 'b';
        break;
    case 2:
        val = cpu_inw(addr);
        suffix = 'w';
        break;
    case 4:
        val = cpu_inl(addr);
        suffix = 'l';
        break;
    }

    return qobject_from_jsonf("{'address': %d,"
                              " 'value': %d}",
                              addr, val);
}

static QObject *qmp_ioport_write(const QDict *args)
{
    int size = qdict_get_int(args, "size");
    int addr = qdict_get_int(args, "addr");
    int val = qdict_get_int(args, "val");

    addr &= IOPORTS_MASK;

    switch (size) {
    default:
    case 1:
        cpu_outb(addr, val);
        break;
    case 2:
        cpu_outw(addr, val);
        break;
    case 4:
        cpu_outl(addr, val);
        break;
    }

    return NULL;
}

static QObject *qmp_boot_set(const QDict *args)
{
    int res;
    const char *bootdevice = qdict_get_str(args, "bootdevice");

    res = qemu_boot_set(bootdevice);
    if (res < 0) {
        return qerror_new(QERR_NOT_SUPPORTED);
    } else if (res > 0) {
        return qerror_new(QERR_INVALID_PARAMETER, "bootdevice",
                          "bootable device");
    }

    return NULL;
}

/**
 * do_system_reset(): Issue a machine reset
 */
static QObject *qmp_system_reset(const QDict *args)
{
    qemu_system_reset_request();
    return NULL;
}

/**
 * do_system_powerdown(): Issue a machine powerdown
 */
static QObject *qmp_system_powerdown(const QDict *args)
{
    qemu_system_powerdown_request();
    return NULL;
}

#if defined(TARGET_I386)
static QDict *pte_to_qdict(uint32_t addr, uint32_t pte, uint32_t mask)
{
    QObject *obj;

    obj = qobject_from_jsonf("{'virtual_address': %d,"
                             " 'pointer_address': %d,"
                             " 'global': %i,"
                             " 'pse': %i,"
                             " 'dirty': %i,"
                             " 'accessed': %i,"
                             " 'pcd': %i,"
                             " 'pwt': %i,"
                             " 'user': %i,"
                              " 'rw': %i}",
                             addr,
                             pte & mask,
                             pte & PG_GLOBAL_MASK,
                             pte & PG_PSE_MASK,
                             pte & PG_DIRTY_MASK,
                             pte & PG_ACCESSED_MASK,
                             pte & PG_PCD_MASK,
                             pte & PG_PWT_MASK,
                             pte & PG_USER_MASK,
                             pte & PG_RW_MASK);

    return qobject_to_qdict(obj);
}

static QObject *qmp_tlb_info(const QDict *args)
{
    CPUState *env;
    int l1, l2;
    uint32_t pgd, pde, pte;
    QDict *info;
    QList *ptes;
    int cpu_index = qdict_get_int(args, "cpu_index");

    env = find_cpu(cpu_index);
    if (env == NULL) {
        return qerror_new(QERR_INVALID_PARAMETER, "cpu_index");
    }

    if (!(env->cr[0] & CR0_PG_MASK)) {
        return qerror_new(QERR_NOT_SUPPORTED);
    }

    info = qdict_new();
    ptes = qlist_new();

    pgd = env->cr[3] & ~0xfff;
    for(l1 = 0; l1 < 1024; l1++) {
        cpu_physical_memory_read(pgd + l1 * 4, (uint8_t *)&pde, 4);
        pde = le32_to_cpu(pde);
        if (pde & PG_PRESENT_MASK) {
            if ((pde & PG_PSE_MASK) && (env->cr[4] & CR4_PSE_MASK)) {
                QDict *obj;
                obj = pte_to_qdict((l1 << 22), pde, ~((1 << 20) - 1));
                qlist_append(ptes, obj);
            } else {
                for(l2 = 0; l2 < 1024; l2++) {
                    cpu_physical_memory_read((pde & ~0xfff) + l2 * 4,
                                             (uint8_t *)&pte, 4);
                    pte = le32_to_cpu(pte);
                    if (pte & PG_PRESENT_MASK) {
                        QDict *obj;
                        obj = pte_to_qdict((l1 << 22) + (l2 << 12),
                                           pte & ~PG_PSE_MASK,
                                           ~0xfff);
                        qlist_append(ptes, obj);
                    }
                }
            }
        }
    }

    qdict_put(info, "ptes", ptes);

    return info;
}

static void mem_list(QList *list, uint32_t *pstart, int *plast_prot,
                     uint32_t end, int prot)
{
    int prot1;

    prot1 = *plast_prot;
    if (prot != prot1) {
        if (*pstart != -1) {
            QObject *obj;
            obj = qobject_from_jsonf("{'start': %d,"
                                     " 'end': %d,"
                                     " 'size': %d,"
                                     " 'protection': {"
                                     "   'user': %i,"
                                     "   'read': %i,"
                                     "   'write': %i }"
                                     "}",
                                     *pstart, end, end - *pstart,
                                     prot1 & PG_USER_MASK,
                                     1,
                                     prot1 & PG_RW_MASK);
            qlist_append(list, obj);
        }
        if (prot != 0)
            *pstart = end;
        else
            *pstart = -1;
        *plast_prot = prot;
    }
}

static QObject *qmp_mem_info(const QDict *args)
{
    CPUState *env;
    int l1, l2, prot, last_prot;
    uint32_t pgd, pde, pte, start, end;
    QList *regions = NULL;
    QDict *info;
    int cpu_index = qdict_get_int(args, "cpu_index");

    env = find_cpu(cpu_index);
    if (env == NULL) {
        return qerror_new(QERR_INVALID_PARAMETER, "cpu_index");
    }

    if (!(env->cr[0] & CR0_PG_MASK)) {
        return qerror_new(QERR_NOT_SUPPORTED);
    }

    info = qdict_new();
    regions = qlist_new();
    pgd = env->cr[3] & ~0xfff;
    last_prot = 0;
    start = -1;
    for(l1 = 0; l1 < 1024; l1++) {
        cpu_physical_memory_read(pgd + l1 * 4, (uint8_t *)&pde, 4);
        pde = le32_to_cpu(pde);
        end = l1 << 22;
        if (pde & PG_PRESENT_MASK) {
            if ((pde & PG_PSE_MASK) && (env->cr[4] & CR4_PSE_MASK)) {
                prot = pde & (PG_USER_MASK | PG_RW_MASK | PG_PRESENT_MASK);
                mem_list(regions, &start, &last_prot, end, prot);
            } else {
                for(l2 = 0; l2 < 1024; l2++) {
                    cpu_physical_memory_read((pde & ~0xfff) + l2 * 4,
                                             (uint8_t *)&pte, 4);
                    pte = le32_to_cpu(pte);
                    end = (l1 << 22) + (l2 << 12);
                    if (pte & PG_PRESENT_MASK) {
                        prot = pte & (PG_USER_MASK | PG_RW_MASK | PG_PRESENT_MASK);
                    } else {
                        prot = 0;
                    }
                    mem_list(regions, &start, &last_prot, end, prot);
                }
            }
        } else {
            prot = 0;
            mem_list(regions, &start, &last_prot, end, prot);
        }
    }

    qdict_put(info, "regions", regions);
    return info;
}
#endif

#if defined(TARGET_SH4)

static QObject *format_tlb_info(int idx, tlb_t *tlb)
{
    return qobject_from_jsonf("{'index': %d,"
                              " 'asid': %d,"
                              " 'vpn': %d,"
                              " 'ppn': %d,"
                              " 'sz': %d,"
                              " 'size': %d,"
                              " 'v': %d,"
                              " 'shared': %d,"
                              " 'cached': %d,"
                              " 'prot': %d,"
                              " 'dirty': %d,"
                              " 'writethrough': %d}",
                              idx,
                              tlb->asid, tlb->vpn, tlb->ppn,
                              tlb->sz, tlb->size, tlb->v, tlb->sh,
                              tlb->c, tlb->pr, tlb->d, tlb->wt);
}

static QObject *qmp_tlb_info(const QDict *args)
{
    int cpu_index = qdict_get_int(args, "cpu_index");
    CPUState *env = find_cpu(cpu_index);
    int i;
    QDict *info;
    QList *list;

    if (env == NULL) {
        return qerror_new(QERR_INVALID_PARAMETER, "cpu_index");
    }

    info = qdict_new();

    list = qlist_new();
    for (i = 0 ; i < ITLB_SIZE ; i++) {
        qlist_append(list, format_tlb_info(i, &env->itlb[i]));
    }
    qdict_put(info, "ITLB", list);

    list = qlist_new();
    for (i = 0 ; i < UTLB_SIZE ; i++) {
        qlist_append(list, format_tlb_info(i, &env->utlb[i]));
    }
    qdict_put(info, "UTLB", list);

    return info;
}

#endif

/**
 * do_info_kvm(): Show KVM information
 *
 * Return a QDict with the following information:
 *
 * - "enabled": true if KVM support is enabled, false otherwise
 * - "present": true if QEMU has KVM support, false otherwise
 *
 * Example:
 *
 * { "enabled": true, "present": true }
 */
static QObject *qmp_info_kvm(const QDict *args)
{
#ifdef CONFIG_KVM
    return qobject_from_jsonf("{ 'enabled': %i, 'present': true }",
                              kvm_enabled());
#else
    return qobject_from_jsonf("{ 'enabled': false, 'present': false }");
#endif
}

static QObject *qmp_info_numa(const QDict *args)
{
    int i;
    CPUState *env;
    QDict *info;
    QList *nodes;

    info = qdict_new();
    nodes = qlist_new();

    for (i = 0; i < nb_numa_nodes; i++) {
        QObject *node;
        uint64_t mask = 0;

        for (env = first_cpu; env != NULL; env = env->next_cpu) {
            if (env->numa_node == i) {
                mask |= (1 << i)
            }
        }

        node = qobject_from_jsonf("{'node_id': %d,"
                                  " 'cpu_mask': %d,"
                                  " 'memory_size': %d}"
                                  i, mask, node_mem[i]);

        qlist_append(nodes, node)
    }

    qdict_put(info, "nodes", nodes);

    return info;
}

#ifdef CONFIG_PROFILER

int64_t qemu_time;
int64_t dev_time;

static QObject *qmp_info_profile(const QDict *args)
{
    int64_t total;
    QObject *ret;

    total = qemu_time;
    if (total == 0)
        total = 1;

    ret = qobject_from_jsonf("{'async_time': %d,"
                             " 'qemu_time': %d,"
                             " 'ticks_per_sec': %d}",
                             dev_time, qemu_time, get_ticks_per_sec());

    qemu_time = 0;
    dev_time = 0;

    return ret;
}
#else
static QObject *qmp_info_profile(const QDict *args)
{
    return qerror_new(QERR_NOT_SUPPORTED);
}
#endif

/* Capture support */
static QLIST_HEAD (capture_list_head, CaptureState) capture_head;

static QObject *qmp_info_capture(const QDict *args)
{
    int i;
    CaptureState *s;
    QDict *info;
    QList *entries;

    info = qdict_new();
    entries = qlist_new();

    for (s = capture_head.lh_first, i = 0; s; s = s->entries.le_next, ++i) {
        QDict *dict;

        dict = s->ops.info(s->opaque);
        qdict_put(dict, "id", qint_from_int(i));
        qlist_append(entries, obj);
    }

    qdict_put(info, "captures", entries);

    return info;
}

#ifdef HAS_AUDIO
static QObject *qmp_stop_capture(const QDict *args)
{
    int i;
    int n = qdict_get_int(args, "n");
    CaptureState *s;

    for (s = capture_head.lh_first, i = 0; s; s = s->entries.le_next, ++i) {
        if (i == n) {
            s->ops.destroy (s->opaque);
            QLIST_REMOVE (s, entries);
            qemu_free (s);
            return NULL;
        }
    }

    return qerror_new(QERR_NO_ENTRY, "wav capture");
}

static QObject *qmp_wav_capture(const QDict *args)
{
    const char *path = qdict_get_str(args, "path");
    int has_freq = qdict_haskey(args, "freq");
    int freq = qdict_get_try_int(args, "freq", -1);
    int has_bits = qdict_haskey(args, "bits");
    int bits = qdict_get_try_int(args, "bits", -1);
    int has_channels = qdict_haskey(args, "nchannels");
    int nchannels = qdict_get_try_int(args, "nchannels", -1);
    CaptureState *s;
    QObject *ret = NULL;

    s = qemu_mallocz (sizeof (*s));

    freq = has_freq ? freq : 44100;
    bits = has_bits ? bits : 16;
    nchannels = has_channels ? nchannels : 2;

    if (wav_start_capture (s, path, freq, bits, nchannels)) {
        ret = qerror_new(QERR_UNDEFINED_ERROR, "wave capture");
        qemu_free (s);
    }
    QLIST_INSERT_HEAD (&capture_head, s, entries);

    return ret;
}
#endif

#if defined(TARGET_I386)
static QObject *qmp_inject_nmi(const QDict *args)
{
    CPUState *env;
    int cpu_index = qdict_get_int(args, "cpu_index");

    for (env = first_cpu; env != NULL; env = env->next_cpu) {
        if (env->cpu_index == cpu_index) {
            cpu_interrupt(env, CPU_INTERRUPT_NMI);
            break;
        }
    }

    return NULL;
}
#endif

/**
 * do_info_status(): VM status
 *
 * Return a QDict with the following information:
 *
 * - "running": true if the VM is running, or false if it is paused
 * - "singlestep": true if the VM is in single step mode, false otherwise
 *
 * Example:
 *
 * { "running": true, "singlestep": false }
 */
static QObject *qmp_info_status(const QDict *args)
{
    return qobject_from_jsonf("{ 'running': %i, 'singlestep': %i }",
                              vm_running, singlestep);
}

static QObject *qmp_acl_show(const QDict *args)
{
    const char *aclname = qdict_get_str(args, "aclname");
    qemu_acl *acl = qemu_acl_find(aclname);
    qemu_acl_entry *entry;
    int i = 0;
    QDict *policy = NULL;
    QList *acls;

    if (!acl) {
        return qerror_new(QERR_NOT_FOUND, "acl");
    }

    policy = qdict_new();
    qdict_put(policy, "default",
              qstring_new(acl->defaultDeny ? "deny" : "allow"));
    
    acls = qlist_new();
    QTAILQ_FOREACH(entry, &acl->entries, next) {
        QObject *obj;
        
        i++;
        obj = qobject_from_jsonf("{'index': %d,"
                                 " 'policy': %s,"
                                 " 'match': %s}",
                                 i, entry->deny ? "deny" : "allow",
                                 entry->match);
        qlist_append(acls, obj);
    }

    qdict_put(policy, "entries", acls);

    return policy;
}

static QObject *qmp_acl_reset(const QDict *args)
{
    const char *aclname = qdict_get_str(args, "aclname");
    qemu_acl *acl = qemu_acl_find(aclname);

    if (!acl) {
        return qerror_new(QERR_NOT_FOUND, "acl");
    }

    qemu_acl_reset(acl);

    return NULL;
}

static QObject *qmp_acl_policy(const QDict *args)
{
    const char *aclname = qdict_get_str(args, "aclname");
    const char *policy = qdict_get_str(args, "policy");
    qemu_acl *acl = qemu_acl_find(aclname);

    if (!acl) {
        return qerror_new(QERR_NOT_FOUND, "acl");
    }

    if (strcmp(policy, "allow") == 0) {
        acl->defaultDeny = 0;
    } else if (strcmp(policy, "deny") == 0) {
        acl->defaultDeny = 1;
    } else {
        return qerror_new(ERR_INVALID_PARAMETER_VALUE,
                          "policy", "'deny' or 'allow'");
    }

    return NULL;
}

static QObject *qmp_acl_add(const QDict *args)
{
    const char *aclname = qdict_get_str(args, "aclname");
    const char *match = qdict_get_str(args, "match");
    const char *policy = qdict_get_str(args, "policy");
    int has_index = qdict_haskey(args, "index");
    int index = qdict_get_try_int(args, "index", -1);
    qemu_acl *acl = qemu_acl_find(aclname);
    int deny, ret;

    if (!acl) {
        return qerror_new(QERR_NOT_FOUND, "acl");
    }

    if (strcmp(policy, "allow") == 0) {
        deny = 0;
    } else if (strcmp(policy, "deny") == 0) {
        deny = 1;
    } else {
        return qerror_new(ERR_INVALID_PARAMETER_VALUE,
                          "policy", "'deny' or 'allow'");
    }
    if (has_index)
        ret = qemu_acl_insert(acl, deny, match, index);
    else
        ret = qemu_acl_append(acl, deny, match);

    if (ret < 0) {
        return qerror_new(QERR_INTERNAL_ERROR, "acl");
    }

    return NULL;
}

static QObject *qmp_acl_remove(const QDict *args)
{
    const char *aclname = qdict_get_str(args, "aclname");
    const char *match = qdict_get_str(args, "match");
    qemu_acl *acl = qemu_find_acl(aclname);
    int ret;

    if (!acl) {
        return qerror_new(QERR_NO_ENTRY, "acl");
    }

    ret = qemu_acl_remove(acl, match);
    if (ret < 0) {
        return qerror_new(QERR_INTERNAL_ERROR, "acl");
    }

    return NULL;
}

#if defined(TARGET_I386)
static QObject *qmp_inject_mce(const QDict *args)
{
    CPUState *cenv;
    int cpu_index = qdict_get_int(args, "cpu_index");
    int bank = qdict_get_int(args, "bank");
    uint64_t status = qdict_get_int(args, "status");
    uint64_t mcg_status = qdict_get_int(args, "mcg_status");
    uint64_t addr = qdict_get_int(args, "addr");
    uint64_t misc = qdict_get_int(args, "misc");

    for (cenv = first_cpu; cenv != NULL; cenv = cenv->next_cpu) {
        if (cenv->cpu_index == cpu_index && cenv->mcg_cap) {
            cpu_inject_x86_mce(cenv, bank, status, mcg_status, addr, misc);
            break;
        }
    }

    return NULL;
}
#endif

QObject *qmp_run(const char *command, const char *fmt, ...)
{
}
