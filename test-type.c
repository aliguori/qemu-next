#include "type.h"

/** plug.h **/

typedef struct Plug
{
    TypeInstance parent;

    int x;
} Plug;

typedef struct PlugClass {
    TypeClass parent_class;
} PlugClass;

#define TYPE_PLUG "plug"
#define PLUG(obj) TYPE_CHECK(Plug, obj, TYPE_PLUG)

void plug_initialize(Plug *plug);

/** c **/

static void plug_initfn(TypeInstance *obj)
{
    Plug *plug = PLUG(obj);

    plug->x = 42;
}

static const TypeInfo plug_type_info = {
    .name = TYPE_PLUG,
    .instance_size = sizeof(Plug),
    .instance_init = plug_initfn,
};

void plug_initialize(Plug *plug)
{
    type_initialize(plug, TYPE_PLUG);
}

/** device.h **/

typedef struct Device
{
    Plug parent;

    int y;
} Device;

typedef struct DeviceClass
{
    PlugClass parent_class;

    void (*realize)(Device *device);
    void (*reset)(Device *device);
} DeviceClass;

#define TYPE_DEVICE "device"
#define DEVICE(obj) TYPE_CHECK(Device, obj, TYPE_DEVICE)
#define DEVICE_CLASS(class) TYPE_CLASS_CHECK(DeviceClass, class, TYPE_DEVICE)

void device_initialize(Device *device);

void device_realize(Device *device);

void device_reset(Device *device);

/** device.c **/

static void device_initfn(TypeInstance *obj)
{
    Device *device = DEVICE(obj);

    device->y = 84;
}

static const TypeInfo device_type_info = {
    .name = TYPE_DEVICE,
    .parent = TYPE_PLUG,
    .instance_size = sizeof(Device),
    .class_size = sizeof(DeviceClass),
    .instance_init = device_initfn,
};

void device_realize(Device *device)
{
    DeviceClass *class = DEVICE_CLASS(TYPE_INSTANCE(device)->class);
    return class->realize(device);
}

void device_reset(Device *device)
{
    DeviceClass *class = DEVICE_CLASS(TYPE_INSTANCE(device)->class);
    return class->reset(device);
}

void device_initialize(Device *device)
{
    type_initialize(device, TYPE_DEVICE);
}

/** stub **/

typedef struct SerialDevice
{
    Device parent;
} SerialDevice;

#define TYPE_SERIAL "serial"
#define SERIAL(obj) TYPE_CHECK(SerialDevice, obj, TYPE_SERIAL)

void serial_initialize(SerialDevice *obj);

/** c **/

static void serial_initfn(TypeInstance *obj)
{
//    SerialDevice *serial = SERIAL(obj);
}

void serial_initialize(SerialDevice *obj)
{
    type_initialize(obj, TYPE_SERIAL);
}

static void serial_realize(Device *device)
{
    printf("serial::realize\n");
}

static void serial_reset(Device *device)
{
    printf("serial::reset\n");
}

static void serial_class_init(TypeClass *class)
{
    DeviceClass *device_class = DEVICE_CLASS(class);

    device_class->realize = serial_realize;
    device_class->reset = serial_reset;
}

static const TypeInfo serial_type_info = {
    .name = TYPE_SERIAL,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(SerialDevice),
    .instance_init = serial_initfn,
    .class_init = serial_class_init,
};

int main(int argc, char **argv)
{
    Device device;
    SerialDevice serial;

    type_register_static(&serial_type_info);
    type_register_static(&device_type_info);
    type_register_static(&plug_type_info);

    device_initialize(&device);
    serial_initialize(&serial);

    assert(device.parent.x == 42);
    assert(device.y == 84);

    device_reset(DEVICE(&serial));

    return 0;
}
