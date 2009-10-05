#include "qemu-common.h"
#include "qemu-option.h"
#include "qemu-config.h"
#include "sysemu.h"

#define DEFINE_OPT(i_name, i_type, i_help) { \
    .name = i_name,                          \
    .type = i_type,                          \
    .help = i_help,                          \
}

#define DEFINE_OPT_NUMBER(name, help) \
    DEFINE_OPT(name, QEMU_OPT_NUMBER, help)
#define DEFINE_OPT_STRING(name, help) \
    DEFINE_OPT(name, QEMU_OPT_STRING, help)
#define DEFINE_OPT_BOOL(name, help) \
    DEFINE_OPT(name, QEMU_OPT_BOOL, help)

#define DEFINE_OPT_END_OF_LIST() { }

QemuOptsList qemu_drive_opts = {
    .name = "drive",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_drive_opts.head),
    .desc = {
        DEFINE_OPT_NUMBER("bus", "bus number"),
        DEFINE_OPT_NUMBER("unit", "unit number (i.e. lun for scsi)"),
        DEFINE_OPT_STRING("if", "interface (ide, scsi, sd, mtd, floppy, pflash, virtio)"),
        DEFINE_OPT_NUMBER("index", 0),
        DEFINE_OPT_NUMBER("cyls", "number of cylinders (ide disk geometry)"),
        DEFINE_OPT_NUMBER("heads", "number of heads (ide disk geometry)"),
        DEFINE_OPT_NUMBER("secs", "number of sectors (ide disk geometry)"),
        DEFINE_OPT_STRING("trans", "chs translation (auto, lba. none)"),
        DEFINE_OPT_STRING("media", "media type (disk, cdrom)"),
        DEFINE_OPT_BOOL("snapshot", 0),
        DEFINE_OPT_STRING("file", "disk image"),
        DEFINE_OPT_STRING("cache", "host cache usage (none, writeback, writethrough)"),
        DEFINE_OPT_STRING("aio", "host AIO implementation (threads, native)"),
        DEFINE_OPT_STRING("format", "disk format (raw, qcow2, ...)"),
        DEFINE_OPT_STRING("serial", 0),
        DEFINE_OPT_STRING("werror", 0),
        DEFINE_OPT_STRING("addr", "pci address (virtio only)"),
        DEFINE_OPT_END_OF_LIST(),
    },
};

QemuOptsList qemu_chardev_opts = {
    .name = "chardev",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_chardev_opts.head),
    .desc = {
        DEFINE_OPT_STRING("backend", 0),
        DEFINE_OPT_STRING("path", 0),
        DEFINE_OPT_STRING("host", 0),
        DEFINE_OPT_STRING("port", 0),
        DEFINE_OPT_STRING("localaddr", 0),
        DEFINE_OPT_STRING("localport", 0),
        DEFINE_OPT_NUMBER("to", 0),
        DEFINE_OPT_BOOL("ipv4", 0),
        DEFINE_OPT_BOOL("ipv6", 0),
        DEFINE_OPT_BOOL("wait", 0),
        DEFINE_OPT_BOOL("server", 0),
        DEFINE_OPT_BOOL("delay", 0),
        DEFINE_OPT_BOOL("telnet", 0),
        DEFINE_OPT_NUMBER("width", 0),
        DEFINE_OPT_NUMBER("height", 0),
        DEFINE_OPT_NUMBER("cols", 0),
        DEFINE_OPT_NUMBER("rows", 0),
        DEFINE_OPT_BOOL("mux", 0),
        DEFINE_OPT_END_OF_LIST(),
    },
};

QemuOptsList qemu_device_opts = {
    .name = "device",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_device_opts.head),
    .desc = {
        /*
         * no elements => accept any
         * sanity checking will happen later
         * when setting device properties
         */
        { /* end if list */ }
    },
};

QemuOptsList qemu_net_opts = {
    .name = "net",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_net_opts.head),
    .desc = {
        /*
         * no elements => accept any params
         * validation will happen later
         */
        { /* end of list */ }
    },
};

QemuOptsList qemu_rtc_opts = {
    .name = "rtc",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_rtc_opts.head),
    .desc = {
        DEFINE_OPT_STRING("base", 0),
        DEFINE_OPT_STRING("clock", 0),
#ifdef TARGET_I386
        DEFINE_OPT_STRING("driftfix", 0),
#endif
        DEFINE_OPT_END_OF_LIST(),
    },
};

QemuOptsList qemu_display_opts = {
    .name = "display",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_rtc_opts.head),
    .desc = {
        DEFINE_OPT_STRING("backend", 0),
        DEFINE_OPT_STRING("host", 0),
        DEFINE_OPT_STRING("port", 0),
        DEFINE_OPT_STRING("localaddr", 0),
        DEFINE_OPT_STRING("localport", 0),
        DEFINE_OPT_NUMBER("to", 0),
        DEFINE_OPT_BOOL("ipv4", 0),
        DEFINE_OPT_BOOL("ipv6", 0),
        DEFINE_OPT_BOOL("password", 0),
        DEFINE_OPT_BOOL("reverse", 0),
        DEFINE_OPT_BOOL("sasl", 0),
        DEFINE_OPT_BOOL("tls", 0),
        DEFINE_OPT_BOOL("acl", 0),
        DEFINE_OPT_BOOL("x509verify", 0),
        DEFINE_OPT_STRING("x509", 0),
        DEFINE_OPT_STRING("x509path", 0),
        DEFINE_OPT_END_OF_LIST(),
    },
};

static QemuOptsList *lists[] = {
    &qemu_drive_opts,
    &qemu_chardev_opts,
    &qemu_device_opts,
    &qemu_net_opts,
    &qemu_rtc_opts,
    NULL,
};

int qemu_set_option(const char *str)
{
    char group[64], id[64], arg[64];
    QemuOpts *opts;
    int i, rc, offset;

    rc = sscanf(str, "%63[^.].%63[^.].%63[^=]%n", group, id, arg, &offset);
    if (rc < 3 || str[offset] != '=') {
        qemu_error("can't parse: \"%s\"\n", str);
        return -1;
    }

    for (i = 0; lists[i] != NULL; i++) {
        if (strcmp(lists[i]->name, group) == 0)
            break;
    }
    if (lists[i] == NULL) {
        qemu_error("there is no option group \"%s\"\n", group);
        return -1;
    }

    opts = qemu_opts_find(lists[i], id);
    if (!opts) {
        qemu_error("there is no %s \"%s\" defined\n",
                lists[i]->name, id);
        return -1;
    }

    if (qemu_opt_set(opts, arg, str+offset+1) == -1) {
        return -1;
    }
    return 0;
}

