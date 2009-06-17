#ifndef SCSI_DISK_H
#define SCSI_DISK_H

#include "qdev.h"

/* scsi-disk.c */
enum scsi_reason {
    SCSI_REASON_DONE, /* Command complete.  */
    SCSI_REASON_DATA  /* Transfer complete, more data required.  */
};

typedef struct SCSIBus SCSIBus;
typedef struct SCSIDeviceState SCSIDeviceState;
typedef struct SCSIDevice SCSIDevice;
typedef struct SCSIDeviceInfo SCSIDeviceInfo;
typedef void (*scsi_completionfn)(SCSIBus *bus, int reason, uint32_t tag,
                                  uint32_t arg);

struct SCSIDevice
{
    DeviceState qdev;
    SCSIDeviceInfo *info;
    SCSIDeviceState *state;
};

SCSIDevice *scsi_disk_init(SCSIBus *bus, BlockDriverState *bdrv);
SCSIDevice *scsi_generic_init(SCSIBus *bus, BlockDriverState *bdrv);

/* cdrom.c */
int cdrom_read_toc(int nb_sectors, uint8_t *buf, int msf, int start_track);
int cdrom_read_toc_raw(int nb_sectors, uint8_t *buf, int msf, int session_num);

/* scsi-bus.c */
typedef void (*scsi_qdev_initfn)(SCSIDevice *dev);
struct SCSIDeviceInfo {
    DeviceInfo qdev;
    scsi_qdev_initfn init;
    void (*destroy)(SCSIDevice *s);
    int32_t (*send_command)(SCSIDevice *s, uint32_t tag, uint8_t *buf,
                            int lun);
    void (*read_data)(SCSIDevice *s, uint32_t tag);
    int (*write_data)(SCSIDevice *s, uint32_t tag);
    void (*cancel_io)(SCSIDevice *s, uint32_t tag);
    uint8_t *(*get_buf)(SCSIDevice *s, uint32_t tag);
};

typedef void (*SCSIAttachFn)(DeviceState *host, BlockDriverState *bdrv,
              int unit);
struct SCSIBus {
    BusState qbus;
    int busnr;

    int tcq;
    SCSIAttachFn attach;
    scsi_completionfn complete;
};

SCSIBus *scsi_bus_new(DeviceState *host, int tcq,
                      SCSIAttachFn attach, scsi_completionfn complete);
void scsi_bus_attach_cmdline(SCSIBus *bus);
void scsi_qdev_register(SCSIDeviceInfo *info);
SCSIDevice *scsi_create_simple(SCSIBus *bus, const char *name);

static inline SCSIBus *scsi_bus_from_device(SCSIDevice *d)
{
    return DO_UPCAST(SCSIBus, qbus, d->qdev.parent_bus);
}

#endif
