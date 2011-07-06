#include "plug.h"

/** plug **/

typedef struct PlugClass
{
    Type type; 
} PlugClass;

typedef struct Plug
{
    PlugClass *class;
} Plug;

Type plug_get_type(void)
{
    static Type type = 0;

    if (type == 0) {
        static const TypeInfo type_info = {
            .name = "plug",
            .abstract = true,
            .instance_size = sizeof(Plug),
            .class_size = sizeof(PlugClass),
        };
        type = type_register_static(0, &type_info);
    }

    return type;
}

/** virtio **/

typedef struct VirtioDeviceClass
{
    PlugClass parent_class;

    void (*get_config)(VirtioDevice *vdev, uint32_t offset, void *data, size_t len);
    void (*set_config)(VirtioDevice *vdev, uint32_t offset, const void *data, size_t len);
} VirtioDeviceClass;

typedef struct VirtioDevice
{
    Plug parent;
} VirtioDevice;

Type virtio_get_type(void)
{
    static Type type = 0;

    if (type == 0) {
        static const TypeInfo type_info = {
            .name = "virtio",
            .abstract = true,
            .instance_size = sizeof(VirtioDevice),
            .class_size = sizeof(VirtioClass),
        };
        type = type_register_static(plug_get_type(), &type_info);
    }

    return type;
}

/** virtio-net **/

typedef struct VirtioNetClass
{
    VirtioDeviceClass parent_class;
} VirtioNetClass;

typedef struct VirtioNetDevice
{
    VirtioDevice parent;
} VirtioNetDevice;

static void virtio_net_get_config(VirtioDevice *vdev, uint32_t offset, void *data, size_t len)
{
    VirtioNetDevice *dev = VIRTIO_NET_DEVICE(vdev);
}

static void virtio_net_set_config(VirtioDevice *vdev, uint32_t offset, const void *data, size_t len)
{
    VirtioNetDevice *dev = VIRTIO_NET_DEVICE(vdev);
}

static void virtio_net_class_init(PlugClass *parent_class)
{
    VirtioClass *virtio_klass = VIRTIO_CLASS(parent_class);

    virtio_klass->get = virtio_net_get_config;
    virtio_klass->set = virtio_net_set_config;
}

Type virtio_net_get_type(void)
{
    static Type type = 0;

    if (type == 0) {
        static const TypeInfo type_info = {
            .name = "virtio-net",
            .instance_size = sizeof(VirtioNetDevice),
            .class_size = sizeof(VirtioNetClass),
            .class_init = virtio_net_class_init,
        };
        type = type_register_static(virtio_get_type(), &type_info);
    }

    return type;
}

/** garbage **/

int plug_create_from_kv(int argc, const char *names[], const char *values[])
{
    int i;

    for (i = 0; i < argc; i++) {
        printf("'%s': '%s'\n", names[i], values[i]);
    }

    return 0;
}

int plug_create_from_va(const char *driver, const char *id, va_list ap)
{
    const char *arg_name[128];
    const char *arg_value[128];
    int arg_count = 0;
    const char *key;
    
    arg_name[arg_count] = "driver";
    arg_value[arg_count] = driver;
    arg_count++;

    arg_name[arg_count] = "id";
    arg_value[arg_count] = id;
    arg_count++;

    while ((key = va_arg(ap, const char *))) {
        const char *value = va_arg(ap, const char *);

        arg_name[arg_count] = key;
        arg_value[arg_count] = value;
        arg_count++;
    }

    return plug_create_from_kv(arg_count, arg_name, arg_value);
}

int plug_create(const char *driver, const char *id, ...)
{
    va_list ap;
    int ret;

    va_start(ap, id);
    ret = plug_create_from_va(driver, id, ap);
    va_end(ap);

    return ret;
}

int plug_create_from_string(const char *optarg)
{
    char buffer[1024];
    char *token, *ptr = buffer;
    bool first_option = true;
    const char *arg_name[128];
    const char *arg_value[128];
    int arg_count = 0;

    snprintf(buffer, sizeof(buffer), "%s", optarg);

    for (token = strsep(&ptr, ","); token; token = strsep(&ptr, ",")) {
        char *sep = strchr(token, '=');
        const char *key, *value;
        
        if (sep == NULL) {
            if (first_option) {
                key = "driver";
                value = token;
            } else {
                key = token;
                value = "on";
            }
        } else {
            *sep = '\0';
            key = token;
            value = sep + 1;
        }

        arg_name[arg_count] = key;
        arg_value[arg_count] = value;
        arg_count++;

        first_option = false;
    }

    return plug_create_from_kv(arg_count, arg_name, arg_value);
}
