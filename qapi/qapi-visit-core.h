#ifndef QAPI_VISITER_CORE_H
#define QAPI_VISITER_CORE_H

#include "qapi-types-core.h"
#include "error.h"
#include <stdlib.h>

typedef struct GenericList
{
    void *value;
    struct GenericList *next;
} GenericList;

typedef struct Visiter Visiter;

struct Visiter
{
    /* Must be set */
    void (*start_struct)(Visiter *v, void **obj, const char *kind, const char *name, Error **errp);
    void (*end_struct)(Visiter *v, Error **errp);

    void (*start_list)(Visiter *v, const char *name, Error **errp);
    GenericList *(*next_list)(Visiter *v, GenericList **list, Error **errp);
    void (*end_list)(Visiter *v, Error **errp);

    void (*type_enum)(Visiter *v, int *obj, const char *kind, const char *name, Error **errp);

    void (*type_int)(Visiter *v, int64_t *obj, const char *name, Error **errp);
    void (*type_bool)(Visiter *v, bool *obj, const char *name, Error **errp);
    void (*type_str)(Visiter *v, char **obj, const char *name, Error **errp);
    void (*type_number)(Visiter *v, double *obj, const char *name, Error **errp);

    /* May be NULL */
    void (*start_optional)(Visiter *v, bool *present, const char *name, Error **errp);
    void (*end_optional)(Visiter *v, Error **errp);

    void (*start_handle)(Visiter *v, void **obj, const char *kind, const char *name, Error **errp);
    void (*end_handle)(Visiter *v, Error **errp);
};

static inline void visit_start_handle(Visiter *v, void **obj, const char *kind, const char *name, Error **errp)
{
    if (error_is_set(errp)) {
        return;
    }

    if (v->start_handle) {
        v->start_handle(v, obj, kind, name, errp);
    }
}

static inline void visit_end_handle(Visiter *v, Error **errp)
{
    if (error_is_set(errp)) {
        return;
    }

    if (v->end_handle) {
        v->end_handle(v, errp);
    }
}

static inline void visit_start_struct(Visiter *v, void **obj, const char *kind, const char *name, Error **errp)
{
    if (error_is_set(errp)) {
        return;
    }

    v->start_struct(v, obj, kind, name, errp);
}

static inline void visit_end_struct(Visiter *v, Error **errp)
{
    if (error_is_set(errp)) {
        return;
    }

    v->end_struct(v, errp);
}

static inline void visit_start_list(Visiter *v, const char *name, Error **errp)
{
    if (error_is_set(errp)) {
        return;
    }

    v->start_list(v, name, errp);
}

static inline GenericList *visit_next_list(Visiter *v, GenericList **list, Error **errp)
{
    if (error_is_set(errp)) {
        return 0;
    }

    return v->next_list(v, list, errp);
}

static inline void visit_end_list(Visiter *v, Error **errp)
{
    if (error_is_set(errp)) {
        return;
    }

    v->end_list(v, errp);
}

static inline void visit_start_optional(Visiter *v, bool *present, const char *name, Error **errp)
{
    if (error_is_set(errp)) {
        return;
    }

    if (v->start_optional) {
        v->start_optional(v, present, name, errp);
    }
}

static inline void visit_end_optional(Visiter *v, Error **errp)
{
    if (error_is_set(errp)) {
        return;
    }

    if (v->end_optional) {
        v->end_optional(v, errp);
    }
}

static inline void visit_type_enum(Visiter *v, int *obj, const char *kind, const char *name, Error **errp)
{
    if (error_is_set(errp)) {
        return;
    }

    v->type_enum(v, obj, kind, name, errp);
}

static inline void visit_type_int(Visiter *v, int64_t *obj, const char *name, Error **errp)
{
    if (error_is_set(errp)) {
        return;
    }

    v->type_int(v, obj, name, errp);
}

static inline void visit_type_bool(Visiter *v, bool *obj, const char *name, Error **errp)
{
    if (error_is_set(errp)) {
        return;
    }

    v->type_bool(v, obj, name, errp);
}

static inline void visit_type_str(Visiter *v, char **obj, const char *name, Error **errp)
{
    if (error_is_set(errp)) {
        return;
    }

    v->type_str(v, obj, name, errp);
}

static inline void visit_type_number(Visiter *v, double *obj, const char *name, Error **errp)
{
    if (error_is_set(errp)) {
        return;
    }

    v->type_number(v, obj, name, errp);
}

#endif
