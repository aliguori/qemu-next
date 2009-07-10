#include "qdev.h"

static void *prop_ptr(DeviceState *dev, Property *prop)
{
    void *ptr = dev;
    ptr += prop->offset;
    return ptr;
}

/* --- 32bit integer --- */

static int parse_uint32(DeviceState *dev, Property *prop, const char *str)
{
    uint32_t *ptr = prop_ptr(dev, prop);

    if (sscanf(str, "%" PRIu32, ptr) != 1)
        return -1;
    return 0;
}

static int print_uint32(DeviceState *dev, Property *prop, char *dest, size_t len)
{
    uint32_t *ptr = prop_ptr(dev, prop);
    return snprintf(dest, len, "%" PRIu32, *ptr);
}

PropertyInfo qdev_prop_uint32 = {
    .name  = "uint32",
    .size  = sizeof(uint32_t),
    .parse = parse_uint32,
    .print = print_uint32,
};

/* --- 32bit hex value --- */

static int parse_hex32(DeviceState *dev, Property *prop, const char *str)
{
    uint32_t *ptr = prop_ptr(dev, prop);

    if (sscanf(str, "%" PRIx32, ptr) != 1)
        return -1;
    return 0;
}

static int print_hex32(DeviceState *dev, Property *prop, char *dest, size_t len)
{
    uint32_t *ptr = prop_ptr(dev, prop);
    return snprintf(dest, len, "0x%" PRIx32, *ptr);
}

PropertyInfo qdev_prop_hex32 = {
    .name  = "hex32",
    .size  = sizeof(uint32_t),
    .parse = parse_hex32,
    .print = print_hex32,
};

/* --- pointer --- */

static int print_ptr(DeviceState *dev, Property *prop, char *dest, size_t len)
{
    void **ptr = prop_ptr(dev, prop);
    return snprintf(dest, len, "<%p>", *ptr);
}

PropertyInfo qdev_prop_ptr = {
    .name  = "ptr",
    .size  = sizeof(void*),
    .print = print_ptr,
};

/* --- mac address --- */

/*
 * accepted syntax versions:
 *   01:02:03:04:05:06
 *   01-02-03-04-05-06
 */
static int parse_mac(DeviceState *dev, Property *prop, const char *str)
{
    uint8_t *mac = prop_ptr(dev, prop);
    int i, pos;
    char *p;

    for (i = 0, pos = 0; i < 6; i++, pos += 3) {
        if (!isxdigit(str[pos]))
            return -1;
        if (!isxdigit(str[pos+1]))
            return -1;
        if (i == 5 && str[pos+2] != '\0')
            return -1;
        if (str[pos+2] != ':' && str[pos+2] != '-')
            return -1;
        mac[i] = strtol(str+pos, &p, 16);
    }
    return 0;
}

static int print_mac(DeviceState *dev, Property *prop, char *dest, size_t len)
{
    uint8_t *mac = prop_ptr(dev, prop);
    return snprintf(dest, len, "%02x:%02x:%02x:%02x:%02x:%02x",
                    mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

PropertyInfo qdev_prop_mac = {
    .name  = "mac",
    .size  = 6,
    .parse = parse_mac,
    .print = print_mac,
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

int qdev_prop_parse(DeviceState *dev, const char *name, const char *value)
{
    Property *prop;

    prop = qdev_prop_find(dev, name);
    if (!prop) {
        fprintf(stderr, "property \"%s.%s\" not found\n",
                dev->info->name, name);
        return -1;
    }
    if (!prop->info->parse) {
        fprintf(stderr, "property \"%s.%s\" has no parser\n",
                dev->info->name, name);
        return -1;
    }
    return prop->info->parse(dev, prop, value);
}

int qdev_prop_set(DeviceState *dev, const char *name, void *src, size_t size)
{
    Property *prop;
    void *dst;

    prop = qdev_prop_find(dev, name);
    if (!prop) {
        fprintf(stderr, "property \"%s.%s\" not found\n",
                dev->info->name, name);
        return -1;
    }
    if (prop->info->size != size) {
        fprintf(stderr, "property \"%s.%s\" size mismatch (%zd / %zd)\n",
                dev->info->name, name, prop->info->size, size);
        return -1;
    }
    dst = prop_ptr(dev, prop);
    memcpy(dst, src, size);
    return 0;
}

int qdev_prop_set_uint32(DeviceState *dev, const char *name, uint32_t value)
{
    return qdev_prop_set(dev, name, &value, sizeof(value));
}

int qdev_prop_set_ptr(DeviceState *dev, const char *name, void *value)
{
    return qdev_prop_set(dev, name, &value, sizeof(value));
}

void qdev_prop_set_defaults(DeviceState *dev, Property *props)
{
    char *dst;

    if (!props)
        return;
    while (props->name) {
        if (props->defval) {
            dst = prop_ptr(dev, props);
            memcpy(dst, props->defval, props->info->size);
        }
        props++;
    }
}

