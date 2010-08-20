#ifndef QEMU_QDEV_CORE_H
#define QEMU_QDEV_CORE_H

#include "hw.h"
#include "blockdev.h"
#include "qemu-queue.h"
#include "qemu-char.h"
#include "qemu-option.h"

typedef struct Property Property;

typedef struct PropertyInfo PropertyInfo;

typedef struct CompatProperty CompatProperty;

typedef struct DeviceInfo DeviceInfo;

typedef struct BusState BusState;

typedef struct BusInfo BusInfo;

enum DevState {
    DEV_STATE_CREATED = 1,
    DEV_STATE_INITIALIZED,
};

enum {
    DEV_NVECTORS_UNSPECIFIED = -1,
};

/* This structure should not be accessed directly.  We declare it here
   so that it can be embedded in individual device state structures.  */
struct DeviceState {
    const char *id;
    enum DevState state;
    QemuOpts *opts;
    int hotplugged;
    DeviceInfo *info;
    BusState *parent_bus;
    int num_gpio_out;
    qemu_irq *gpio_out;
    int num_gpio_in;
    qemu_irq *gpio_in;
    QLIST_HEAD(, BusState) child_bus;
    int num_child_bus;
    QLIST_ENTRY(DeviceState) sibling;
    int instance_id_alias;
    int alias_required_for_version;
};

typedef char *(*bus_get_dev_path)(DeviceState *dev);

struct BusInfo {
    const char *name;
    size_t size;
    bus_get_dev_path get_dev_path;
    Property *props;
};

struct BusState {
    DeviceState *parent;
    BusInfo *info;
    const char *name;
    int allow_hotplug;
    int qdev_allocated;
    QLIST_HEAD(, DeviceState) children;
    QLIST_ENTRY(BusState) sibling;
};

struct Property {
    const char   *name;
    PropertyInfo *info;
    int          offset;
    int          bitnr;
    void         *defval;
};

enum PropertyType {
    PROP_TYPE_UNSPEC = 0,
    PROP_TYPE_UINT8,
    PROP_TYPE_UINT16,
    PROP_TYPE_UINT32,
    PROP_TYPE_INT32,
    PROP_TYPE_UINT64,
    PROP_TYPE_TADDR,
    PROP_TYPE_MACADDR,
    PROP_TYPE_DRIVE,
    PROP_TYPE_CHR,
    PROP_TYPE_STRING,
    PROP_TYPE_NETDEV,
    PROP_TYPE_VLAN,
    PROP_TYPE_PTR,
    PROP_TYPE_BIT,
};

struct PropertyInfo {
    const char *name;
    size_t size;
    enum PropertyType type;
    int (*parse)(DeviceState *dev, Property *prop, const char *str);
    int (*print)(DeviceState *dev, Property *prop, char *dest, size_t len);
    void (*free)(DeviceState *dev, Property *prop);
};

typedef struct GlobalProperty {
    const char *driver;
    const char *property;
    const char *value;
    QTAILQ_ENTRY(GlobalProperty) next;
} GlobalProperty;

/*** Board API.  This should go away once we have a machine config file.  ***/

DeviceState *qdev_create(BusState *bus, const char *name);
int qdev_init(DeviceState *dev) QEMU_WARN_UNUSED_RESULT;
void qdev_init_nofail(DeviceState *dev);
void qdev_set_legacy_instance_id(DeviceState *dev, int alias_id,
                                 int required_for_version);
int qdev_unplug(DeviceState *dev);
void qdev_free(DeviceState *dev);
int qdev_simple_unplug_cb(DeviceState *dev);
void qdev_machine_creation_done(void);

qemu_irq qdev_get_gpio_in(DeviceState *dev, int n);
void qdev_connect_gpio_out(DeviceState *dev, int n, qemu_irq pin);

BlockDriverState *qdev_init_bdrv(DeviceState *dev, BlockInterfaceType type);

BusState *qdev_get_child_bus(DeviceState *dev, const char *name);

/*** Device API.  ***/

typedef int (*qdev_initfn)(DeviceState *dev, DeviceInfo *info);
typedef int (*qdev_event)(DeviceState *dev);
typedef void (*qdev_resetfn)(DeviceState *dev);

struct DeviceInfo {
    const char *name;
    const char *alias;
    const char *desc;
    size_t size;
    Property *props;
    int no_user;

    /* callbacks */
    qdev_resetfn reset;

    /* device state */
    const VMStateDescription *vmsd;

    /* Private to qdev / bus.  */
    qdev_initfn init;
    qdev_event unplug;
    qdev_event exit;
    BusInfo *bus_info;
    struct DeviceInfo *next;
};

/* HACK: this needs to go away */
extern DeviceInfo *device_info_list;

void qdev_register(DeviceInfo *info);
DeviceInfo *qdev_find_info(BusInfo *bus_info, const char *name);

/* Register device properties.  */
/* GPIO inputs also double as IRQ sinks.  */
void qdev_init_gpio_in(DeviceState *dev, qemu_irq_handler handler, int n);
void qdev_init_gpio_out(DeviceState *dev, qemu_irq *pins, int n);

CharDriverState *qdev_init_chardev(DeviceState *dev);

BusState *qdev_get_parent_bus(DeviceState *dev);

/* HACK: this should go away */
int qdev_allow_hotplug(void);

/*** BUS API. ***/

/* Returns false to terminate walk; true to continue */
typedef int (qdev_walkerfn)(DeviceState *dev, void *opaque);
typedef int (qbus_walkerfn)(BusState *bus, void *opaque);

void qbus_create_inplace(BusState *bus, BusInfo *info,
                         DeviceState *parent, const char *name);
BusState *qbus_create(BusInfo *info, DeviceState *parent, const char *name);

int qbus_walk_child_devs(BusState *bus, qdev_walkerfn *walker, void *opaque);
int qbus_walk_child_busses(BusState *bus, qbus_walkerfn *walker, void *opaque);

DeviceState *qbus_find_child_dev(BusState *bus, const char *id);
BusState *qbus_find_child_bus(BusState *bus, const char *id);

void qbus_reset_all(BusState *bus);
void qbus_free(BusState *bus);

#define FROM_QBUS(type, dev) DO_UPCAST(type, qbus, dev)

/* This should go away once we get rid of the NULL bus hack */
BusState *sysbus_get_default(void);

/* This is a nasty hack to allow passing a NULL bus to qdev_create.  */
extern struct BusInfo system_bus_info;

#endif
