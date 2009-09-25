#include "vnc-streams.h"
#include "sys-queue.h"

typedef struct VncCharState
{
    CharDriverState *chr;
    char *name;
    TAILQ_ENTRY(VncCharState) node;
    int id;
} VncCharState;

static TAILQ_HEAD(, VncCharState) vnc_char_list =
    TAILQ_HEAD_INITIALIZER(vnc_char_list);

/* We only support one connection */
static VncState *vnc_state;

static void vnc_streams_ack(VncState *vs)
{
    vnc_write_u8(vs, 0);
    vnc_write_u8(vs, 0);
    vnc_write_u16(vs, 1);
    vnc_framebuffer_update(vs, 1, 0,
                           ds_get_width(vs->ds), ds_get_height(vs->ds),
                           VNC_ENCODING_STREAMS);
    vnc_flush(vs);
}

static void vnc_streams_open(VncState *vs, const char *name, int id)
{
    int len = strlen(name);

    vnc_write_u8(vs, 255);    /* aliguori */
    vnc_write_u8(vs, 2);      /* streams */
    vnc_write_u16(vs, 0);     /* open */
    vnc_write_u32(vs, id);    /* id */
    vnc_write_u32(vs, len);   /* name len */
    vnc_write(vs, name, len); /* name */
    vnc_flush(vs);
}

static void vnc_streams_write(VncState *vs, int id, const uint8_t *buf, int len)
{
    vnc_write_u8(vs, 255);   /* aliguori */
    vnc_write_u8(vs, 2);     /* streams */
    vnc_write_u16(vs, 1);    /* data */
    vnc_write_u32(vs, id);   /* id */
    vnc_write_u32(vs, len);  /* len */
    vnc_write(vs, buf, len); /* buffer */
    vnc_flush(vs);
}

void vnc_streams_read(VncState *vs, int id, uint32_t len, void *data)
{
    VncCharState *vcs;

    if (vs != vnc_state)
        return;

    TAILQ_FOREACH(vcs, &vnc_char_list, node) {
        if (vcs->id == id) {
            break;
        }
    }

    if (vcs == NULL) {
        return;
    }

    qemu_chr_write(vcs->chr, data, len);
}

static void vnc_streams_close(VncState *vs, int id)
{
    vnc_write_u8(vs, 255);   /* aliguori */
    vnc_write_u8(vs, 2);     /* streams */
    vnc_write_u16(vs, 2);    /* close */
    vnc_write_u32(vs, id);   /* id */
    vnc_flush(vs);
}

void vnc_streams_enumerate(VncState *vs)
{
    VncCharState *vcs;

    /* there is already a streams capable client connected,
       this client gets no streams */
    if (vnc_state) {
        return;
    }

    /* set this client as the streams client */
    vnc_state = vs;
    vnc_streams_ack(vs);

    TAILQ_FOREACH(vcs, &vnc_char_list, node) {
        vnc_streams_open(vnc_state, vcs->name, vcs->id);
    }
}

void vnc_streams_detach(VncState *vs)
{
    vnc_state = NULL;
}

static int vnc_chr_write(CharDriverState *chr, const uint8_t *buf, int len)
{
    VncCharState *vcs = chr->opaque;

    if (vnc_state) {
        vnc_streams_write(vnc_state, vcs->id, buf, len);
    }

    return len;
}

static void vnc_chr_close(CharDriverState *chr)
{
    VncCharState *vcs = chr->opaque;

    TAILQ_REMOVE(&vnc_char_list, vcs, node);
    if (vnc_state) {
        vnc_streams_close(vnc_state, vcs->id);
    }

    qemu_free(vcs->name);
    qemu_free(vcs);
}

CharDriverState *qemu_chr_open_vnc(const char *name)
{
    static int vnc_next_id; /* FIXME */
    VncCharState *vcs;

    vcs = qemu_mallocz(sizeof(*vcs));
    vcs->chr = qemu_mallocz(sizeof(*vcs->chr));

    vcs->chr->opaque = vcs;
    vcs->chr->chr_write = vnc_chr_write;
    vcs->chr->chr_close = vnc_chr_close;

    vcs->name = qemu_strdup(name);
    vcs->id = vnc_next_id++;

    TAILQ_INSERT_HEAD(&vnc_char_list, vcs, node);

    if (vnc_state) {
        vnc_streams_open(vnc_state, name, vcs->id);
    }

    return vcs->chr;
}
