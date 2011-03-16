/*
 * QEMU host block devices
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later.  See the COPYING file in the top-level directory.
 */

#include "block.h"
#include "blockdev.h"
#include "monitor.h"
#include "qerror.h"
#include "qemu-option.h"
#include "qemu-config.h"
#include "sysemu.h"
#include "hw/qdev.h"
#include "block_int.h"

static QTAILQ_HEAD(drivelist, DriveInfo) drives = QTAILQ_HEAD_INITIALIZER(drives);

/*
 * We automatically delete the drive when a device using it gets
 * unplugged.  Questionable feature, but we can't just drop it.
 * Device models call blockdev_mark_auto_del() to schedule the
 * automatic deletion, and generic qdev code calls blockdev_auto_del()
 * when deletion is actually safe.
 */
void blockdev_mark_auto_del(BlockDriverState *bs)
{
    DriveInfo *dinfo = drive_get_by_blockdev(bs);

    if (dinfo) {
        dinfo->auto_del = 1;
    }
}

void blockdev_auto_del(BlockDriverState *bs)
{
    DriveInfo *dinfo = drive_get_by_blockdev(bs);

    if (dinfo && dinfo->auto_del) {
        drive_uninit(dinfo);
    }
}

QemuOpts *drive_add(const char *file, const char *fmt, ...)
{
    va_list ap;
    char optstr[1024];
    QemuOpts *opts;

    va_start(ap, fmt);
    vsnprintf(optstr, sizeof(optstr), fmt, ap);
    va_end(ap);

    opts = qemu_opts_parse(qemu_find_opts_nofail("drive"), optstr, 0);
    if (!opts) {
        return NULL;
    }
    if (file)
        qemu_opt_set_qerr(opts, "file", file);
    return opts;
}

DriveInfo *drive_get(BlockInterfaceType type, int bus, int unit)
{
    DriveInfo *dinfo;

    /* seek interface, bus and unit */

    QTAILQ_FOREACH(dinfo, &drives, next) {
        if (dinfo->type == type &&
	    dinfo->bus == bus &&
	    dinfo->unit == unit)
            return dinfo;
    }

    return NULL;
}

int drive_get_max_bus(BlockInterfaceType type)
{
    int max_bus;
    DriveInfo *dinfo;

    max_bus = -1;
    QTAILQ_FOREACH(dinfo, &drives, next) {
        if(dinfo->type == type &&
           dinfo->bus > max_bus)
            max_bus = dinfo->bus;
    }
    return max_bus;
}

DriveInfo *drive_get_by_blockdev(BlockDriverState *bs)
{
    DriveInfo *dinfo;

    QTAILQ_FOREACH(dinfo, &drives, next) {
        if (dinfo->bdrv == bs) {
            return dinfo;
        }
    }
    return NULL;
}

static void bdrv_format_print(void *opaque, const char *name)
{
    fprintf(stderr, " %s", name);
}

void drive_uninit(DriveInfo *dinfo)
{
    qemu_opts_del(dinfo->opts);
    bdrv_delete(dinfo->bdrv);
    QTAILQ_REMOVE(&drives, dinfo, next);
    qemu_free(dinfo);
}

static int parse_block_error_action(const char *buf, int is_read)
{
    if (!strcmp(buf, "ignore")) {
        return BLOCK_ERR_IGNORE;
    } else if (!is_read && !strcmp(buf, "enospc")) {
        return BLOCK_ERR_STOP_ENOSPC;
    } else if (!strcmp(buf, "stop")) {
        return BLOCK_ERR_STOP_ANY;
    } else if (!strcmp(buf, "report")) {
        return BLOCK_ERR_REPORT;
    } else {
        fprintf(stderr, "qemu: '%s' invalid %s error action\n",
            buf, is_read ? "read" : "write");
        return -1;
    }
}

DriveInfo *drive_init(QemuOpts *opts, int default_to_scsi, int *fatal_error)
{
    const char *buf;
    const char *file = NULL;
    char devname[128];
    const char *serial;
    const char *mediastr = "";
    BlockInterfaceType type;
    enum { MEDIA_DISK, MEDIA_CDROM } media;
    int bus_id, unit_id;
    int cyls, heads, secs, translation;
    BlockDriver *drv = NULL;
    int max_devs;
    int index;
    int ro = 0;
    int bdrv_flags = 0;
    int on_read_error, on_write_error;
    const char *devaddr;
    DriveInfo *dinfo;
    int snapshot = 0;
    int ret;

    *fatal_error = 1;

    translation = BIOS_ATA_TRANSLATION_AUTO;

    if (default_to_scsi) {
        type = IF_SCSI;
        max_devs = MAX_SCSI_DEVS;
        pstrcpy(devname, sizeof(devname), "scsi");
    } else {
        type = IF_IDE;
        max_devs = MAX_IDE_DEVS;
        pstrcpy(devname, sizeof(devname), "ide");
    }
    media = MEDIA_DISK;

    /* extract parameters */
    bus_id  = qemu_opt_get_number(opts, "bus", 0);
    unit_id = qemu_opt_get_number(opts, "unit", -1);
    index   = qemu_opt_get_number(opts, "index", -1);

    cyls  = qemu_opt_get_number(opts, "cyls", 0);
    heads = qemu_opt_get_number(opts, "heads", 0);
    secs  = qemu_opt_get_number(opts, "secs", 0);

    snapshot = qemu_opt_get_bool(opts, "snapshot", 0);
    ro = qemu_opt_get_bool(opts, "readonly", 0);

    file = qemu_opt_get(opts, "file");
    serial = qemu_opt_get(opts, "serial");

    if ((buf = qemu_opt_get(opts, "if")) != NULL) {
        pstrcpy(devname, sizeof(devname), buf);
        if (!strcmp(buf, "ide")) {
	    type = IF_IDE;
            max_devs = MAX_IDE_DEVS;
        } else if (!strcmp(buf, "scsi")) {
	    type = IF_SCSI;
            max_devs = MAX_SCSI_DEVS;
        } else if (!strcmp(buf, "floppy")) {
	    type = IF_FLOPPY;
            max_devs = 0;
        } else if (!strcmp(buf, "pflash")) {
	    type = IF_PFLASH;
            max_devs = 0;
	} else if (!strcmp(buf, "mtd")) {
	    type = IF_MTD;
            max_devs = 0;
	} else if (!strcmp(buf, "sd")) {
	    type = IF_SD;
            max_devs = 0;
        } else if (!strcmp(buf, "virtio")) {
            type = IF_VIRTIO;
            max_devs = 0;
	} else if (!strcmp(buf, "xen")) {
	    type = IF_XEN;
            max_devs = 0;
	} else if (!strcmp(buf, "none")) {
	    type = IF_NONE;
            max_devs = 0;
	} else {
            fprintf(stderr, "qemu: unsupported bus type '%s'\n", buf);
            return NULL;
	}
    }

    if (cyls || heads || secs) {
        if (cyls < 1 || (type == IF_IDE && cyls > 16383)) {
            fprintf(stderr, "qemu: '%s' invalid physical cyls number\n", buf);
	    return NULL;
	}
        if (heads < 1 || (type == IF_IDE && heads > 16)) {
            fprintf(stderr, "qemu: '%s' invalid physical heads number\n", buf);
	    return NULL;
	}
        if (secs < 1 || (type == IF_IDE && secs > 63)) {
            fprintf(stderr, "qemu: '%s' invalid physical secs number\n", buf);
	    return NULL;
	}
    }

    if ((buf = qemu_opt_get(opts, "trans")) != NULL) {
        if (!cyls) {
            fprintf(stderr,
                    "qemu: '%s' trans must be used with cyls,heads and secs\n",
                    buf);
            return NULL;
        }
        if (!strcmp(buf, "none"))
            translation = BIOS_ATA_TRANSLATION_NONE;
        else if (!strcmp(buf, "lba"))
            translation = BIOS_ATA_TRANSLATION_LBA;
        else if (!strcmp(buf, "auto"))
            translation = BIOS_ATA_TRANSLATION_AUTO;
	else {
            fprintf(stderr, "qemu: '%s' invalid translation type\n", buf);
	    return NULL;
	}
    }

    if ((buf = qemu_opt_get(opts, "media")) != NULL) {
        if (!strcmp(buf, "disk")) {
	    media = MEDIA_DISK;
	} else if (!strcmp(buf, "cdrom")) {
            if (cyls || secs || heads) {
                fprintf(stderr,
                        "qemu: '%s' invalid physical CHS format\n", buf);
	        return NULL;
            }
	    media = MEDIA_CDROM;
	} else {
	    fprintf(stderr, "qemu: '%s' invalid media\n", buf);
	    return NULL;
	}
    }

    if ((buf = qemu_opt_get(opts, "cache")) != NULL) {
        if (!strcmp(buf, "off") || !strcmp(buf, "none")) {
            bdrv_flags |= BDRV_O_NOCACHE;
        } else if (!strcmp(buf, "writeback")) {
            bdrv_flags |= BDRV_O_CACHE_WB;
        } else if (!strcmp(buf, "unsafe")) {
            bdrv_flags |= BDRV_O_CACHE_WB;
            bdrv_flags |= BDRV_O_NO_FLUSH;
        } else if (!strcmp(buf, "writethrough")) {
            /* this is the default */
        } else {
           fprintf(stderr, "qemu: invalid cache option\n");
           return NULL;
        }
    }

#ifdef CONFIG_LINUX_AIO
    if ((buf = qemu_opt_get(opts, "aio")) != NULL) {
        if (!strcmp(buf, "native")) {
            bdrv_flags |= BDRV_O_NATIVE_AIO;
        } else if (!strcmp(buf, "threads")) {
            /* this is the default */
        } else {
           fprintf(stderr, "qemu: invalid aio option\n");
           return NULL;
        }
    }
#endif

    if ((buf = qemu_opt_get(opts, "format")) != NULL) {
       if (strcmp(buf, "?") == 0) {
            fprintf(stderr, "qemu: Supported formats:");
            bdrv_iterate_format(bdrv_format_print, NULL);
            fprintf(stderr, "\n");
	    return NULL;
        }
        drv = bdrv_find_whitelisted_format(buf);
        if (!drv) {
            fprintf(stderr, "qemu: '%s' invalid format\n", buf);
            return NULL;
        }
    }

    on_write_error = BLOCK_ERR_STOP_ENOSPC;
    if ((buf = qemu_opt_get(opts, "werror")) != NULL) {
        if (type != IF_IDE && type != IF_SCSI && type != IF_VIRTIO && type != IF_NONE) {
            fprintf(stderr, "werror is not supported by this format\n");
            return NULL;
        }

        on_write_error = parse_block_error_action(buf, 0);
        if (on_write_error < 0) {
            return NULL;
        }
    }

    on_read_error = BLOCK_ERR_REPORT;
    if ((buf = qemu_opt_get(opts, "rerror")) != NULL) {
        if (type != IF_IDE && type != IF_VIRTIO && type != IF_SCSI && type != IF_NONE) {
            fprintf(stderr, "rerror is not supported by this format\n");
            return NULL;
        }

        on_read_error = parse_block_error_action(buf, 1);
        if (on_read_error < 0) {
            return NULL;
        }
    }

    if ((devaddr = qemu_opt_get(opts, "addr")) != NULL) {
        if (type != IF_VIRTIO) {
            fprintf(stderr, "addr is not supported\n");
            return NULL;
        }
    }

    /* compute bus and unit according index */

    if (index != -1) {
        if (bus_id != 0 || unit_id != -1) {
            fprintf(stderr,
                    "qemu: index cannot be used with bus and unit\n");
            return NULL;
        }
        if (max_devs == 0)
        {
            unit_id = index;
            bus_id = 0;
        } else {
            unit_id = index % max_devs;
            bus_id = index / max_devs;
        }
    }

    /* if user doesn't specify a unit_id,
     * try to find the first free
     */

    if (unit_id == -1) {
       unit_id = 0;
       while (drive_get(type, bus_id, unit_id) != NULL) {
           unit_id++;
           if (max_devs && unit_id >= max_devs) {
               unit_id -= max_devs;
               bus_id++;
           }
       }
    }

    /* check unit id */

    if (max_devs && unit_id >= max_devs) {
        fprintf(stderr, "qemu: unit %d too big (max is %d)\n",
                unit_id, max_devs - 1);
        return NULL;
    }

    /*
     * ignore multiple definitions
     */

    if (drive_get(type, bus_id, unit_id) != NULL) {
        *fatal_error = 0;
        return NULL;
    }

    /* init */

    dinfo = qemu_mallocz(sizeof(*dinfo));
    if ((buf = qemu_opts_id(opts)) != NULL) {
        dinfo->id = qemu_strdup(buf);
    } else {
        /* no id supplied -> create one */
        dinfo->id = qemu_mallocz(32);
        if (type == IF_IDE || type == IF_SCSI)
            mediastr = (media == MEDIA_CDROM) ? "-cd" : "-hd";
        if (max_devs)
            snprintf(dinfo->id, 32, "%s%i%s%i",
                     devname, bus_id, mediastr, unit_id);
        else
            snprintf(dinfo->id, 32, "%s%s%i",
                     devname, mediastr, unit_id);
    }
    dinfo->bdrv = bdrv_new(dinfo->id);
    dinfo->devaddr = devaddr;
    dinfo->type = type;
    dinfo->bus = bus_id;
    dinfo->unit = unit_id;
    dinfo->opts = opts;
    if (serial)
        strncpy(dinfo->serial, serial, sizeof(dinfo->serial) - 1);
    QTAILQ_INSERT_TAIL(&drives, dinfo, next);

    bdrv_set_on_error(dinfo->bdrv, on_read_error, on_write_error);

    switch(type) {
    case IF_IDE:
    case IF_SCSI:
    case IF_XEN:
    case IF_NONE:
        switch(media) {
	case MEDIA_DISK:
            if (cyls != 0) {
                bdrv_set_geometry_hint(dinfo->bdrv, cyls, heads, secs);
                bdrv_set_translation_hint(dinfo->bdrv, translation);
            }
	    break;
	case MEDIA_CDROM:
            bdrv_set_type_hint(dinfo->bdrv, BDRV_TYPE_CDROM);
	    break;
	}
        break;
    case IF_SD:
        /* FIXME: This isn't really a floppy, but it's a reasonable
           approximation.  */
    case IF_FLOPPY:
        bdrv_set_type_hint(dinfo->bdrv, BDRV_TYPE_FLOPPY);
        break;
    case IF_PFLASH:
    case IF_MTD:
        break;
    case IF_VIRTIO:
        /* add virtio block device */
        opts = qemu_opts_create(qemu_find_opts_nofail("device"), NULL, 0, NULL);
        qemu_opt_set_qerr(opts, "driver", "virtio-blk-pci");
        qemu_opt_set_qerr(opts, "drive", dinfo->id);
        if (devaddr)
            qemu_opt_set_qerr(opts, "addr", devaddr);
        break;
    case IF_COUNT:
        abort();
    }
    if (!file || !*file) {
        *fatal_error = 0;
        return NULL;
    }
    if (snapshot) {
        /* always use cache=unsafe with snapshot */
        bdrv_flags &= ~BDRV_O_CACHE_MASK;
        bdrv_flags |= (BDRV_O_SNAPSHOT|BDRV_O_CACHE_WB|BDRV_O_NO_FLUSH);
    }

    if (media == MEDIA_CDROM) {
        /* CDROM is fine for any interface, don't check.  */
        ro = 1;
    } else if (ro == 1) {
        if (type != IF_SCSI && type != IF_VIRTIO && type != IF_FLOPPY && type != IF_NONE) {
            fprintf(stderr, "qemu: readonly flag not supported for drive with this interface\n");
            return NULL;
        }
    }

    bdrv_flags |= ro ? 0 : BDRV_O_RDWR;

    ret = bdrv_open(dinfo->bdrv, file, bdrv_flags, drv);
    if (ret < 0) {
        fprintf(stderr, "qemu: could not open disk image %s: %s\n",
                        file, strerror(-ret));
        return NULL;
    }

    if (bdrv_key_required(dinfo->bdrv))
        autostart = 0;
    *fatal_error = 0;
    return dinfo;
}

void do_commit(Monitor *mon, const QDict *qdict)
{
    const char *device = qdict_get_str(qdict, "device");
    BlockDriverState *bs;

    if (!strcmp(device, "all")) {
        bdrv_commit_all();
    } else {
        bs = bdrv_find(device);
        if (!bs) {
            qerror_report(QERR_DEVICE_NOT_FOUND, device);
            return;
        }
        bdrv_commit(bs);
    }
}

int do_snapshot_blkdev(Monitor *mon, const QDict *qdict, QObject **ret_data)
{
    const char *device = qdict_get_str(qdict, "device");
    const char *filename = qdict_get_try_str(qdict, "snapshot_file");
    const char *format = qdict_get_try_str(qdict, "format");
    BlockDriverState *bs;
    BlockDriver *drv, *proto_drv;
    int ret = 0;
    int flags;

    bs = bdrv_find(device);
    if (!bs) {
        qerror_report(QERR_DEVICE_NOT_FOUND, device);
        ret = -1;
        goto out;
    }

    if (!format) {
        format = "qcow2";
    }

    drv = bdrv_find_format(format);
    if (!drv) {
        qerror_report(QERR_INVALID_BLOCK_FORMAT, format);
        ret = -1;
        goto out;
    }

    proto_drv = bdrv_find_protocol(filename);
    if (!proto_drv) {
        qerror_report(QERR_INVALID_BLOCK_FORMAT, format);
        ret = -1;
        goto out;
    }

    ret = bdrv_img_create(filename, format, bs->filename,
                          bs->drv->format_name, NULL, -1, bs->open_flags);
    if (ret) {
        goto out;
    }

    qemu_aio_flush();
    bdrv_flush(bs);

    flags = bs->open_flags;
    bdrv_close(bs);
    ret = bdrv_open(bs, filename, flags, drv);
    /*
     * If reopening the image file we just created fails, we really
     * are in trouble :(
     */
    if (ret != 0) {
        abort();
    }
out:
    if (ret) {
        ret = -1;
    }

    return ret;
}

static void eject_device(BlockDriverState *bs, int force, Error **err)
{
    if (!force) {
        if (!bdrv_is_removable(bs)) {
            error_set(err, QERR_DEVICE_NOT_REMOVABLE, bdrv_get_device_name(bs));
            return;
        }
        if (bdrv_is_locked(bs)) {
            error_set(err, QERR_DEVICE_LOCKED, bdrv_get_device_name(bs));
            return;
        }
    }
    bdrv_close(bs);
}

void qmp_eject(Blockdev *device, bool has_force, bool force, Error **err)
{
    BlockDriverState *bs = blockdev_to_bs(device);

    if (!has_force) {
        force = false;
    }

    eject_device(bs, force, err);
}

void qmp_set_blockdev_password(Blockdev *device, const char *password, Error **err)
{
    BlockDriverState *bs = blockdev_to_bs(device);
    int ret;

    ret = bdrv_set_key(bs, password);
    if (ret == -EINVAL) {
        error_set(err, QERR_DEVICE_NOT_ENCRYPTED, bdrv_get_device_name(bs));
    } else if (ret < 0) {
        error_set(err, QERR_INVALID_PASSWORD);
    }
}

void qmp_block_passwd(Blockdev *device, const char *password, Error **err)
{
    qmp_set_blockdev_password(device, password, err);
}

static void qmp_bdrv_open_encrypted(BlockDriverState *bs, const char *filename,
                                    int bdrv_flags, BlockDriver *drv,
                                    const char *password, Error **errp)
{
    if (bdrv_open(bs, filename, bdrv_flags, drv) < 0) {
        error_set(errp, QERR_OPEN_FILE_FAILED, filename);
        return;
    }

    if (bdrv_key_required(bs)) {
        if (password) {
            if (bdrv_set_key(bs, password) < 0) {
                error_set(errp, QERR_INVALID_PASSWORD);
            }
        } else {
            error_set(errp, QERR_DEVICE_ENCRYPTED, bdrv_get_device_name(bs),
                      bdrv_get_encrypted_filename(bs));
        }
    } else if (password) {
        error_set(errp, QERR_DEVICE_NOT_ENCRYPTED, bdrv_get_device_name(bs));
    }
}

void deprecated_qmp_change_blockdev(const char *device, const char *filename,
                                    bool has_format, const char *format,
                                    Error **errp)
{
    BlockDriverState *bs;
    BlockDriver *drv = NULL;
    int bdrv_flags;
    Error *err = NULL;

    bs = bdrv_find(device);
    if (!bs) {
        error_set(errp, QERR_DEVICE_NOT_FOUND, device);
        return;
    }
    if (has_format) {
        drv = bdrv_find_whitelisted_format(format);
        if (!drv) {
            error_set(errp, QERR_INVALID_BLOCK_FORMAT, format);
            return;
        }
    }

    bdrv_flags = bdrv_is_read_only(bs) ? 0 : BDRV_O_RDWR;
    bdrv_flags |= bdrv_is_snapshot(bs) ? BDRV_O_SNAPSHOT : 0;

    eject_device(bs, 0, &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }

    qmp_bdrv_open_encrypted(bs, filename, bdrv_flags, drv, NULL, errp);
}

void qmp_change_blockdev(Blockdev *device, const char *filename,
                         bool has_format, const char *format,
                         bool has_password, const char *password,
                         Error **errp)
{
    BlockDriverState *bs, *bs1;
    BlockDriver *drv = NULL;
    int bdrv_flags;
    Error *err = NULL;
    bool probed_raw = false;

    bs = blockdev_to_bs(device);

    if (has_format) {
        drv = bdrv_find_whitelisted_format(format);
        if (!drv) {
            error_set(errp, QERR_INVALID_BLOCK_FORMAT, format);
            return;
        }
    }

    bdrv_flags = bdrv_is_read_only(bs) ? 0 : BDRV_O_RDWR;
    bdrv_flags |= bdrv_is_snapshot(bs) ? BDRV_O_SNAPSHOT : 0;

    if (!has_password) {
        password = NULL;
    }

    /* Try to open once with a temporary block device to make sure that
     * the disk isn't encrypted and we lack a key.  This also helps keep
     * this operation as a transaction.  That is, if we fail, we try very
     * hard to make sure that the state is the same as before the operation
     * was started.
     */
    bs1 = bdrv_new("");
    qmp_bdrv_open_encrypted(bs1, filename, bdrv_flags, drv, password, &err);
    if (!has_format && bs1->drv->unsafe_probe) {
        probed_raw = true;
    }
    bdrv_delete(bs1);

    if (err) {
        error_propagate(errp, err);
        return;
    }

    if (probed_raw) {
        /* We will not auto probe raw files. */
        error_set(errp, QERR_MISSING_PARAMETER, "format");
        return;
    }

    eject_device(bs, 0, &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }

    qmp_bdrv_open_encrypted(bs, filename, bdrv_flags, drv, password, errp);
}

int do_drive_del(Monitor *mon, const QDict *qdict, QObject **ret_data)
{
    const char *id = qdict_get_str(qdict, "id");
    BlockDriverState *bs;
    BlockDriverState **ptr;
    Property *prop;

    bs = bdrv_find(id);
    if (!bs) {
        qerror_report(QERR_DEVICE_NOT_FOUND, id);
        return -1;
    }

    /* quiesce block driver; prevent further io */
    qemu_aio_flush();
    bdrv_flush(bs);
    bdrv_close(bs);

    /* clean up guest state from pointing to host resource by
     * finding and removing DeviceState "drive" property */
    for (prop = bs->peer->info->props; prop && prop->name; prop++) {
        if (prop->info->type == PROP_TYPE_DRIVE) {
            ptr = qdev_get_prop_ptr(bs->peer, prop);
            if ((*ptr) == bs) {
                bdrv_detach(bs, bs->peer);
                *ptr = NULL;
                break;
            }
        }
    }

    /* clean up host side */
    drive_uninit(drive_get_by_blockdev(bs));

    return 0;
}
