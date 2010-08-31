#include "qdev-prop-compat.h"
#include "blockdev.h"
#include "net.h"
#include "qemu-error.h"

/* --- drive --- */

static int parse_drive(DeviceState *dev, Property *prop, const char *str)
{
    BlockDriverState **ptr = qdev_get_prop_ptr(dev, prop);
    BlockDriverState *bs;

    bs = bdrv_find(str);
    if (bs == NULL)
        return -ENOENT;
    if (bdrv_attach(bs, dev) < 0)
        return -EEXIST;
    *ptr = bs;
    return 0;
}

static void free_drive(DeviceState *dev, Property *prop)
{
    BlockDriverState **ptr = qdev_get_prop_ptr(dev, prop);

    if (*ptr) {
        bdrv_detach(*ptr, dev);
        blockdev_auto_del(*ptr);
    }
}

static int print_drive(DeviceState *dev, Property *prop, char *dest, size_t len)
{
    BlockDriverState **ptr = qdev_get_prop_ptr(dev, prop);
    return snprintf(dest, len, "%s",
                    *ptr ? bdrv_get_device_name(*ptr) : "<null>");
}

PropertyInfo qdev_prop_drive = {
    .name  = "drive",
    .type  = PROP_TYPE_DRIVE,
    .size  = sizeof(BlockDriverState *),
    .parse = parse_drive,
    .print = print_drive,
    .free  = free_drive,
};

/* --- character device --- */

static int parse_chr(DeviceState *dev, Property *prop, const char *str)
{
    CharDriverState **ptr = qdev_get_prop_ptr(dev, prop);

    *ptr = qemu_chr_find(str);
    if (*ptr == NULL)
        return -ENOENT;
    return 0;
}

static int print_chr(DeviceState *dev, Property *prop, char *dest, size_t len)
{
    CharDriverState **ptr = qdev_get_prop_ptr(dev, prop);

    if (*ptr && (*ptr)->label) {
        return snprintf(dest, len, "%s", (*ptr)->label);
    } else {
        return snprintf(dest, len, "<null>");
    }
}

PropertyInfo qdev_prop_chr = {
    .name  = "chr",
    .type  = PROP_TYPE_CHR,
    .size  = sizeof(CharDriverState*),
    .parse = parse_chr,
    .print = print_chr,
};

/* --- netdev device --- */

static int parse_netdev(DeviceState *dev, Property *prop, const char *str)
{
    VLANClientState **ptr = qdev_get_prop_ptr(dev, prop);

    *ptr = qemu_find_netdev(str);
    if (*ptr == NULL)
        return -ENOENT;
    if ((*ptr)->peer) {
        return -EEXIST;
    }
    return 0;
}

static int print_netdev(DeviceState *dev, Property *prop, char *dest, size_t len)
{
    VLANClientState **ptr = qdev_get_prop_ptr(dev, prop);

    if (*ptr && (*ptr)->name) {
        return snprintf(dest, len, "%s", (*ptr)->name);
    } else {
        return snprintf(dest, len, "<null>");
    }
}

PropertyInfo qdev_prop_netdev = {
    .name  = "netdev",
    .type  = PROP_TYPE_NETDEV,
    .size  = sizeof(VLANClientState*),
    .parse = parse_netdev,
    .print = print_netdev,
};

/* --- vlan --- */

static int parse_vlan(DeviceState *dev, Property *prop, const char *str)
{
    VLANState **ptr = qdev_get_prop_ptr(dev, prop);
    int id;

    if (sscanf(str, "%d", &id) != 1)
        return -EINVAL;
    *ptr = qemu_find_vlan(id, 1);
    if (*ptr == NULL)
        return -ENOENT;
    return 0;
}

static int print_vlan(DeviceState *dev, Property *prop, char *dest, size_t len)
{
    VLANState **ptr = qdev_get_prop_ptr(dev, prop);

    if (*ptr) {
        return snprintf(dest, len, "%d", (*ptr)->id);
    } else {
        return snprintf(dest, len, "<null>");
    }
}

PropertyInfo qdev_prop_vlan = {
    .name  = "vlan",
    .type  = PROP_TYPE_VLAN,
    .size  = sizeof(VLANClientState*),
    .parse = parse_vlan,
    .print = print_vlan,
};

/* --- pointer --- */

/* Not a proper property, just for dirty hacks.  TODO Remove it!  */
PropertyInfo qdev_prop_ptr = {
    .name  = "ptr",
    .type  = PROP_TYPE_PTR,
    .size  = sizeof(void*),
};

/* --- mac address --- */

/*
 * accepted syntax versions:
 *   01:02:03:04:05:06
 *   01-02-03-04-05-06
 */
static int parse_mac(DeviceState *dev, Property *prop, const char *str)
{
    MACAddr *mac = qdev_get_prop_ptr(dev, prop);
    int i, pos;
    char *p;

    for (i = 0, pos = 0; i < 6; i++, pos += 3) {
        if (!qemu_isxdigit(str[pos]))
            return -EINVAL;
        if (!qemu_isxdigit(str[pos+1]))
            return -EINVAL;
        if (i == 5) {
            if (str[pos+2] != '\0')
                return -EINVAL;
        } else {
            if (str[pos+2] != ':' && str[pos+2] != '-')
                return -EINVAL;
        }
        mac->a[i] = strtol(str+pos, &p, 16);
    }
    return 0;
}

static int print_mac(DeviceState *dev, Property *prop, char *dest, size_t len)
{
    MACAddr *mac = qdev_get_prop_ptr(dev, prop);

    return snprintf(dest, len, "%02x:%02x:%02x:%02x:%02x:%02x",
                    mac->a[0], mac->a[1], mac->a[2],
                    mac->a[3], mac->a[4], mac->a[5]);
}

PropertyInfo qdev_prop_macaddr = {
    .name  = "macaddr",
    .type  = PROP_TYPE_MACADDR,
    .size  = sizeof(MACAddr),
    .parse = parse_mac,
    .print = print_mac,
};

/* --- pci address --- */

/*
 * bus-local address, i.e. "$slot" or "$slot.$fn"
 */
static int parse_pci_devfn(DeviceState *dev, Property *prop, const char *str)
{
    uint32_t *ptr = qdev_get_prop_ptr(dev, prop);
    unsigned int slot, fn, n;

    if (sscanf(str, "%x.%x%n", &slot, &fn, &n) != 2) {
        fn = 0;
        if (sscanf(str, "%x%n", &slot, &n) != 1) {
            return -EINVAL;
        }
    }
    if (str[n] != '\0')
        return -EINVAL;
    if (fn > 7)
        return -EINVAL;
    *ptr = slot << 3 | fn;
    return 0;
}

static int print_pci_devfn(DeviceState *dev, Property *prop, char *dest, size_t len)
{
    uint32_t *ptr = qdev_get_prop_ptr(dev, prop);

    if (*ptr == -1) {
        return snprintf(dest, len, "<unset>");
    } else {
        return snprintf(dest, len, "%02x.%x", *ptr >> 3, *ptr & 7);
    }
}

PropertyInfo qdev_prop_pci_devfn = {
    .name  = "pci-devfn",
    .type  = PROP_TYPE_UINT32,
    .size  = sizeof(uint32_t),
    .parse = parse_pci_devfn,
    .print = print_pci_devfn,
};

int qdev_prop_set_drive(DeviceState *dev, const char *name, BlockDriverState *value)
{
    int res;

    res = bdrv_attach(value, dev);
    if (res < 0) {
        error_report("Can't attach drive %s to %s.%s: %s",
                     bdrv_get_device_name(value),
                     dev->name,
                     name, strerror(-res));
        return -1;
    }
    qdev_prop_set(dev, name, &value, PROP_TYPE_DRIVE);
    return 0;
}

void qdev_prop_set_drive_nofail(DeviceState *dev, const char *name, BlockDriverState *value)
{
    if (qdev_prop_set_drive(dev, name, value) < 0) {
        exit(1);
    }
}
void qdev_prop_set_chr(DeviceState *dev, const char *name, CharDriverState *value)
{
    qdev_prop_set(dev, name, &value, PROP_TYPE_CHR);
}

void qdev_prop_set_netdev(DeviceState *dev, const char *name, VLANClientState *value)
{
    qdev_prop_set(dev, name, &value, PROP_TYPE_NETDEV);
}

void qdev_prop_set_vlan(DeviceState *dev, const char *name, VLANState *value)
{
    qdev_prop_set(dev, name, &value, PROP_TYPE_VLAN);
}

void qdev_prop_set_macaddr(DeviceState *dev, const char *name, uint8_t *value)
{
    qdev_prop_set(dev, name, value, PROP_TYPE_MACADDR);
}
