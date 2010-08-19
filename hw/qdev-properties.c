#include "qdev.h"
#include "serialinterface.h"

void *qdev_get_prop_ptr(DeviceState *dev, Property *prop)
{
    void *ptr = dev;
    ptr += prop->offset;
    return ptr;
}

static uint32_t qdev_get_prop_mask(Property *prop)
{
    assert(prop->info->type == PROP_TYPE_BIT);
    return 0x1 << prop->bitnr;
}

static void bit_prop_set(DeviceState *dev, Property *props, bool val)
{
    uint32_t *p = qdev_get_prop_ptr(dev, props);
    uint32_t mask = qdev_get_prop_mask(props);
    if (val)
        *p |= mask;
    else
        *p &= ~mask;
}

static void qdev_prop_cpy(DeviceState *dev, Property *props, void *src)
{
    if (props->info->type == PROP_TYPE_BIT) {
        bool *defval = src;
        bit_prop_set(dev, props, *defval);
    } else {
        char *dst = qdev_get_prop_ptr(dev, props);
        memcpy(dst, src, props->info->size);
    }
}

/* Bit */
static int parse_bit(DeviceState *dev, Property *prop, const char *str)
{
    if (!strncasecmp(str, "on", 2))
        bit_prop_set(dev, prop, true);
    else if (!strncasecmp(str, "off", 3))
        bit_prop_set(dev, prop, false);
    else
        return -EINVAL;
    return 0;
}

static int print_bit(DeviceState *dev, Property *prop, char *dest, size_t len)
{
    uint8_t *p = qdev_get_prop_ptr(dev, prop);
    return snprintf(dest, len, (*p & qdev_get_prop_mask(prop)) ? "on" : "off");
}

PropertyInfo qdev_prop_bit = {
    .name  = "on/off",
    .type  = PROP_TYPE_BIT,
    .size  = sizeof(uint32_t),
    .parse = parse_bit,
    .print = print_bit,
};

/* --- 8bit integer --- */

static int parse_uint8(DeviceState *dev, Property *prop, const char *str)
{
    uint8_t *ptr = qdev_get_prop_ptr(dev, prop);
    char *end;

    /* accept both hex and decimal */
    *ptr = strtoul(str, &end, 0);
    if ((*end != '\0') || (end == str)) {
        return -EINVAL;
    }

    return 0;
}

static int print_uint8(DeviceState *dev, Property *prop, char *dest, size_t len)
{
    uint8_t *ptr = qdev_get_prop_ptr(dev, prop);
    return snprintf(dest, len, "%" PRIu8, *ptr);
}

PropertyInfo qdev_prop_uint8 = {
    .name  = "uint8",
    .type  = PROP_TYPE_UINT8,
    .size  = sizeof(uint8_t),
    .parse = parse_uint8,
    .print = print_uint8,
};

/* --- 16bit integer --- */

static int parse_uint16(DeviceState *dev, Property *prop, const char *str)
{
    uint16_t *ptr = qdev_get_prop_ptr(dev, prop);
    char *end;

    /* accept both hex and decimal */
    *ptr = strtoul(str, &end, 0);
    if ((*end != '\0') || (end == str)) {
        return -EINVAL;
    }

    return 0;
}

static int print_uint16(DeviceState *dev, Property *prop, char *dest, size_t len)
{
    uint16_t *ptr = qdev_get_prop_ptr(dev, prop);
    return snprintf(dest, len, "%" PRIu16, *ptr);
}

PropertyInfo qdev_prop_uint16 = {
    .name  = "uint16",
    .type  = PROP_TYPE_UINT16,
    .size  = sizeof(uint16_t),
    .parse = parse_uint16,
    .print = print_uint16,
};

/* --- 32bit integer --- */

static int parse_uint32(DeviceState *dev, Property *prop, const char *str)
{
    uint32_t *ptr = qdev_get_prop_ptr(dev, prop);
    char *end;

    /* accept both hex and decimal */
    *ptr = strtoul(str, &end, 0);
    if ((*end != '\0') || (end == str)) {
        return -EINVAL;
    }

    return 0;
}

static int print_uint32(DeviceState *dev, Property *prop, char *dest, size_t len)
{
    uint32_t *ptr = qdev_get_prop_ptr(dev, prop);
    return snprintf(dest, len, "%" PRIu32, *ptr);
}

PropertyInfo qdev_prop_uint32 = {
    .name  = "uint32",
    .type  = PROP_TYPE_UINT32,
    .size  = sizeof(uint32_t),
    .parse = parse_uint32,
    .print = print_uint32,
};

static int parse_int32(DeviceState *dev, Property *prop, const char *str)
{
    int32_t *ptr = qdev_get_prop_ptr(dev, prop);
    char *end;

    *ptr = strtol(str, &end, 10);
    if ((*end != '\0') || (end == str)) {
        return -EINVAL;
    }

    return 0;
}

static int print_int32(DeviceState *dev, Property *prop, char *dest, size_t len)
{
    int32_t *ptr = qdev_get_prop_ptr(dev, prop);
    return snprintf(dest, len, "%" PRId32, *ptr);
}

PropertyInfo qdev_prop_int32 = {
    .name  = "int32",
    .type  = PROP_TYPE_INT32,
    .size  = sizeof(int32_t),
    .parse = parse_int32,
    .print = print_int32,
};

/* --- 32bit hex value --- */

static int parse_hex32(DeviceState *dev, Property *prop, const char *str)
{
    uint32_t *ptr = qdev_get_prop_ptr(dev, prop);
    char *end;

    *ptr = strtoul(str, &end, 16);
    if ((*end != '\0') || (end == str)) {
        return -EINVAL;
    }

    return 0;
}

static int print_hex32(DeviceState *dev, Property *prop, char *dest, size_t len)
{
    uint32_t *ptr = qdev_get_prop_ptr(dev, prop);
    return snprintf(dest, len, "0x%" PRIx32, *ptr);
}

PropertyInfo qdev_prop_hex32 = {
    .name  = "hex32",
    .type  = PROP_TYPE_UINT32,
    .size  = sizeof(uint32_t),
    .parse = parse_hex32,
    .print = print_hex32,
};

/* --- 64bit integer --- */

static int parse_uint64(DeviceState *dev, Property *prop, const char *str)
{
    uint64_t *ptr = qdev_get_prop_ptr(dev, prop);
    char *end;

    /* accept both hex and decimal */
    *ptr = strtoull(str, &end, 0);
    if ((*end != '\0') || (end == str)) {
        return -EINVAL;
    }

    return 0;
}

static int print_uint64(DeviceState *dev, Property *prop, char *dest, size_t len)
{
    uint64_t *ptr = qdev_get_prop_ptr(dev, prop);
    return snprintf(dest, len, "%" PRIu64, *ptr);
}

PropertyInfo qdev_prop_uint64 = {
    .name  = "uint64",
    .type  = PROP_TYPE_UINT64,
    .size  = sizeof(uint64_t),
    .parse = parse_uint64,
    .print = print_uint64,
};

/* --- 64bit hex value --- */

static int parse_hex64(DeviceState *dev, Property *prop, const char *str)
{
    uint64_t *ptr = qdev_get_prop_ptr(dev, prop);
    char *end;

    *ptr = strtoull(str, &end, 16);
    if ((*end != '\0') || (end == str)) {
        return -EINVAL;
    }

    return 0;
}

static int print_hex64(DeviceState *dev, Property *prop, char *dest, size_t len)
{
    uint64_t *ptr = qdev_get_prop_ptr(dev, prop);
    return snprintf(dest, len, "0x%" PRIx64, *ptr);
}

PropertyInfo qdev_prop_hex64 = {
    .name  = "hex64",
    .type  = PROP_TYPE_UINT64,
    .size  = sizeof(uint64_t),
    .parse = parse_hex64,
    .print = print_hex64,
};

/* --- string --- */

static int parse_string(DeviceState *dev, Property *prop, const char *str)
{
    char **ptr = qdev_get_prop_ptr(dev, prop);

    if (*ptr)
        qemu_free(*ptr);
    *ptr = qemu_strdup(str);
    return 0;
}

static void free_string(DeviceState *dev, Property *prop)
{
    qemu_free(*(char **)qdev_get_prop_ptr(dev, prop));
}

static int print_string(DeviceState *dev, Property *prop, char *dest, size_t len)
{
    char **ptr = qdev_get_prop_ptr(dev, prop);
    if (!*ptr)
        return snprintf(dest, len, "<null>");
    return snprintf(dest, len, "\"%s\"", *ptr);
}

PropertyInfo qdev_prop_string = {
    .name  = "string",
    .type  = PROP_TYPE_STRING,
    .size  = sizeof(char*),
    .parse = parse_string,
    .print = print_string,
    .free  = free_string,
};

static int parse_serialif(DeviceState *dev, Property *prop, const char *str)
{
    SerialInterface **ptr = qdev_get_prop_ptr(dev, prop);
    Interface *iface;

    iface = iface_find(str, "SerialInterface");
    if (iface == NULL) {
        return -ENOENT;
    }
    *ptr = container_of(iface, SerialInterface, iface);

    return 0;
}

static int print_serialif(DeviceState *dev, Property *prop, char *dest, size_t len)
{
    SerialInterface **ptr = qdev_get_prop_ptr(dev, prop);

    if (*ptr && (*ptr)->iface.name) {
        return snprintf(dest, len, "%s", (*ptr)->iface.name);
    } else {
        return snprintf(dest, len, "<null>");
    }
}

PropertyInfo qdev_prop_serialif = {
    .name  = "serialif",
    .type  = PROP_TYPE_SERIALIF,
    .size  = sizeof(SerialInterface *),
    .parse = parse_serialif,
    .print = print_serialif,
};

/* --- public helpers --- */

static Property *qdev_prop_walk(Property *props, const char *name)
{
    if (!props)
        return NULL;
    while (props->name) {
        if (strcmp(props->name, name) == 0)
            return props;
        props++;
    }
    return NULL;
}

static Property *qdev_prop_find(DeviceState *dev, const char *name)
{
    Property *prop;

    /* device properties */
    prop = qdev_prop_walk(dev->info->props, name);
    if (prop)
        return prop;

    /* bus properties */
    prop = qdev_prop_walk(dev->parent_bus->info->props, name);
    if (prop)
        return prop;

    return NULL;
}

int qdev_prop_exists(DeviceState *dev, const char *name)
{
    return qdev_prop_find(dev, name) ? true : false;
}

int qdev_prop_parse(DeviceState *dev, const char *name, const char *value)
{
    Property *prop;
    int ret;

    prop = qdev_prop_find(dev, name);
    /*
     * TODO Properties without a parse method are just for dirty
     * hacks.  qdev_prop_ptr is the only such PropertyInfo.  It's
     * marked for removal.  The test !prop->info->parse should be
     * removed along with it.
     */
    if (!prop || !prop->info->parse) {
        hw_error("no parsing method\n");
        return -1;
    }
    ret = prop->info->parse(dev, prop, value);
    if (ret < 0) {
        switch (ret) {
        case -EEXIST:
            hw_error("value in use\n");
            break;
        default:
        case -EINVAL:
            hw_error("bad value\n");
            break;
        case -ENOENT:
            hw_error("no such property\n");
            break;
        }
        return -1;
    }
    return 0;
}

void qdev_prop_set(DeviceState *dev, const char *name, void *src, enum PropertyType type)
{
    Property *prop;

    prop = qdev_prop_find(dev, name);
    if (!prop) {
        hw_error("property \"%s.%s\" not found\n",
                 dev->info->name, name);
    }
    if (prop->info->type != type) {
        hw_error("property \"%s.%s\" type mismatch\n",
                 dev->info->name, name);
    }
    qdev_prop_cpy(dev, prop, src);
}

void qdev_prop_set_bit(DeviceState *dev, const char *name, bool value)
{
    qdev_prop_set(dev, name, &value, PROP_TYPE_BIT);
}

void qdev_prop_set_uint8(DeviceState *dev, const char *name, uint8_t value)
{
    qdev_prop_set(dev, name, &value, PROP_TYPE_UINT8);
}

void qdev_prop_set_uint16(DeviceState *dev, const char *name, uint16_t value)
{
    qdev_prop_set(dev, name, &value, PROP_TYPE_UINT16);
}

void qdev_prop_set_uint32(DeviceState *dev, const char *name, uint32_t value)
{
    qdev_prop_set(dev, name, &value, PROP_TYPE_UINT32);
}

void qdev_prop_set_int32(DeviceState *dev, const char *name, int32_t value)
{
    qdev_prop_set(dev, name, &value, PROP_TYPE_INT32);
}

void qdev_prop_set_uint64(DeviceState *dev, const char *name, uint64_t value)
{
    qdev_prop_set(dev, name, &value, PROP_TYPE_UINT64);
}

void qdev_prop_set_string(DeviceState *dev, const char *name, char *value)
{
    qdev_prop_set(dev, name, &value, PROP_TYPE_STRING);
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

void qdev_prop_set_ptr(DeviceState *dev, const char *name, void *value)
{
    qdev_prop_set(dev, name, &value, PROP_TYPE_PTR);
}

void qdev_prop_set_defaults(DeviceState *dev, Property *props)
{
    if (!props)
        return;
    while (props->name) {
        if (props->defval) {
            qdev_prop_cpy(dev, props, props->defval);
        }
        props++;
    }
}

static QTAILQ_HEAD(, GlobalProperty) global_props = QTAILQ_HEAD_INITIALIZER(global_props);

static void qdev_prop_register_global(GlobalProperty *prop)
{
    QTAILQ_INSERT_TAIL(&global_props, prop, next);
}

void qdev_prop_register_global_list(GlobalProperty *props)
{
    int i;

    for (i = 0; props[i].driver != NULL; i++) {
        qdev_prop_register_global(props+i);
    }
}

void qdev_prop_set_globals(DeviceState *dev)
{
    GlobalProperty *prop;

    QTAILQ_FOREACH(prop, &global_props, next) {
        if (strcmp(dev->info->name, prop->driver) != 0 &&
            strcmp(dev->info->bus_info->name, prop->driver) != 0) {
            continue;
        }
        if (qdev_prop_parse(dev, prop->property, prop->value) != 0) {
            exit(1);
        }
    }
}
