#ifndef PM_SMBUS_H
#define PM_SMBUS_H

typedef struct PMSMBus {
    i2c_bus *smbus;

    uint8_t smb_stat;
    uint8_t smb_ctl;
    uint8_t smb_cmd;
    uint8_t smb_addr;
    uint8_t smb_data0;
    uint8_t smb_data1;
    uint8_t smb_data[32];
    uint8_t smb_index;
} PMSMBus;

void pm_smbus_init(DeviceState *parent, PMSMBus *smb);
void smb_ioport_writeb(void *opaque, target_phys_addr_t addr, uint64_t val,
                       unsigned size);
uint64_t smb_ioport_readb(void *opaque, target_phys_addr_t addr,
                          unsigned size);

#endif /* !PM_SMBUS_H */
