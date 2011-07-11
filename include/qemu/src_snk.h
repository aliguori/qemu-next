#ifndef SRC_SNK_H
#define SRC_SNK_H

#include "qemu/device.h"
#include "qemu/pin.h"

typedef struct Source
{
    Device parent;

    Pin out[8];
} Source;

#define TYPE_SOURCE "source"
#define SOURCE(obj) TYPE_CHECK(Source, obj, TYPE_SOURCE)

void source_initialize(Source *obj, const char *id);
void source_finalize(Source *obj);
void source_visit(Source *obj, Visitor *v, const char *name, Error **errp);

int64_t source_get_value(Source *obj);
void source_set_value(Source *obj, int64_t value);

typedef struct Sink
{
    Device parent;

    Pin *in[8];
} Sink;

#define TYPE_SINK "sink"
#define SINK(obj) TYPE_CHECK(Sink, obj, TYPE_SINK)

void sink_initialize(Sink *obj, const char *id);
void sink_finalize(Sink *obj);

int64_t sink_get_value(Sink *obj);
void sink_set_value(Sink *obj, int64_t value);

#endif
