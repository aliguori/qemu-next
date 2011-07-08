#include "src_snk.h"

void source_initialize(Source *obj, const char *id)
{
    type_initialize(obj, TYPE_SOURCE, id);
}

void source_finalize(Source *obj)
{
    type_finalize(obj);
}

int64_t source_get_value(Source *obj)
{
    uint8_t value = 0;
    int i;

    for (i = 0; i < 8; i++) {
        value |= pin_get_level(&obj->out[i]) << i;
    }

    return value;
}

void source_set_value(Source *obj, int64_t value)
{
    int i;

    for (i = 0; i < 8; i++) {
        pin_set_level(&obj->out[i], !!(value & (1 << i)));
    }
}

static void source_initfn(TypeInstance *inst)
{
    Source *obj = SOURCE(inst);
    char name[32];
    int i;

    for (i = 0; i < 8; i++) {
        snprintf(name, sizeof(name), "%s::out[%d]", plug_get_id(PLUG(obj)), i);
        pin_initialize(&obj->out[i], name);
        
        snprintf(name, sizeof(name), "out[%d]", i);
        plug_add_property_plug(PLUG(obj), name, (Plug *)&obj->out[i], TYPE_PIN);
    }

    plug_add_property_int(PLUG(obj), "value",
                          (int64_t (*)(Plug *))source_get_value,
                          (void (*)(Plug *, int64_t))source_set_value,
                          PROP_F_READWRITE);
}

static const TypeInfo source_type_info = {
    .name = TYPE_SOURCE,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(Source),
    .instance_init = source_initfn,
};

void sink_initialize(Sink *obj, const char *id)
{
    type_initialize(obj, TYPE_SINK, id);
}

void sink_finalize(Sink *obj)
{
    type_finalize(obj);
}

int64_t sink_get_value(Sink *obj)
{
    uint8_t value = 0;
    int i;

    for (i = 0; i < 8; i++) {
        value |= pin_get_level(obj->in[i]) << i;
    }

    return value;
}

static void sink_initfn(TypeInstance *inst)
{
    Sink *obj = SINK(inst);
    char name[32];
    int i;

    for (i = 0; i < 8; i++) {
        snprintf(name, sizeof(name), "in[%d]", i);
        plug_add_property_socket(PLUG(obj), name, (Plug **)&obj->in[i], TYPE_PIN, true);
    }

    plug_add_property_int(PLUG(obj), "value",
                          (int64_t (*)(Plug *))sink_get_value,
                          NULL,
                          PROP_F_READ);
}

static const TypeInfo sink_type_info = {
    .name = TYPE_SINK,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(Sink),
    .instance_init = sink_initfn,
};

static void register_devices(void)
{
    type_register_static(&source_type_info);
    type_register_static(&sink_type_info);
}

device_init(register_devices);
