#include "spice-ring.h"
#include "spice-draw.h"
#include "spice-cmd.h"

typedef struct QXLUpdate {
    QXLDrawable drawable;
    QXLImage image;
    QXLCommand cmd;
} QXLUpdate;

QXLUpdate *qemu_spice_display_create_update(DisplayState *ds, Rect *dirty, int unique);

static inline int rect_is_empty(const Rect* r)
{
    return r->top == r->bottom || r->left == r->right;
}

static inline void rect_union(Rect *dest, const Rect *r)
{
    if (rect_is_empty(r)) {
        return;
    }

    if (rect_is_empty(dest)) {
        *dest = *r;
        return;
    }

    dest->top = MIN(dest->top, r->top);
    dest->left = MIN(dest->left, r->left);
    dest->bottom = MAX(dest->bottom, r->bottom);
    dest->right = MAX(dest->right, r->right);
}

