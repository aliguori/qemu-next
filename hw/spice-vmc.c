/*

 Spice Virtual Machine Channel (VMC).

  A virtio-serial port used for spice to guest communication, over which spice client
 and a daemon in the guest operating system communicate.

  Replaces the old vdi_port PCI device.

*/

#include <spice.h>
#include <vd_interface.h>

#include "virtio-serial.h"
#include "qemu-spice.h"

#define SPICE_VM_CHANNEL_GUEST_DEVICE_NAME "org.redhat.spice.0"
#define SPICE_VM_CHANNEL_DEVICE_NAME       "spicevmc"

/*#define DEBUG_SVMC*/

#ifdef DEBUG_SVMC
#ifndef INFO_SVMC
#define INFO_SVMC
#endif
#define DEBUG_SVMC_PRINTF(fmt, ...) printf("DEBUG spicevmc: " fmt, __VA_ARGS__)
#else
#define DEBUG_SVMC_PRINTF(...)
#endif

#ifdef INFO_SVMC
#define INFO_SVMC_PRINTF(fmt, ...) printf("spicevmc: " fmt, __VA_ARGS__)
#else
#define INFO_SVMC_PRINTF(...)
#endif

typedef struct SpiceVirtualChannel {
    VirtIOSerialPort port;
    bool running;
    bool active_interface;
    VDIPortInterface interface;
    VDIPortPlug *plug;

    /* buffer the memory written by the guest until spice-server reads */
    struct {
        uint8    d[1024*16]; /* 16 KiB */
        unsigned write_pos;
        int      bytes;      /* in [0, sizeof(d)] */
        int      read_pos;
    } guest_out_ring;
} SpiceVirtualChannel;

/*
 * VDIPortInterface callbacks
 */

static VDObjectRef spice_virtual_channel_interface_plug(
                VDIPortInterface *port, VDIPortPlug* plug)
{
    SpiceVirtualChannel *d = container_of(port, SpiceVirtualChannel, interface);
    DEBUG_SVMC_PRINTF("%s, d = %p, d->plug = %p\n", __func__, d, d->plug);
    if (d->plug) {
        return INVALID_VD_OBJECT_REF;
    }
    d->plug = plug;
    DEBUG_SVMC_PRINTF("%s, d = %p, d->plug = %p\n", __func__, d, d->plug);
    return (VDObjectRef)plug;
}

static void spice_virtual_channel_interface_unplug(
                VDIPortInterface *port, VDObjectRef plug)
{
    SpiceVirtualChannel *d = container_of(port, SpiceVirtualChannel, interface);
    DEBUG_SVMC_PRINTF("%s, d = %p, d->plug = %p\n", __func__, d, d->plug);
    if (!plug || plug != (VDObjectRef)d->plug) {
        return;
    }
    d->plug = NULL;

    /* XXX - throw away anything the client has not read */

    if (d->guest_out_ring.bytes != 0) {
        printf("warning: %s: %d unwritten bytes discarded.\n",
                            __func__, d->guest_out_ring.bytes);
    }
    d->guest_out_ring.read_pos = d->guest_out_ring.write_pos;

    if (!d->running) {
        INFO_SVMC_PRINTF("%s: TODO - notify_guest! what to do??\n", __func__);
    }
}

static int spice_virtual_channel_interface_write(
    VDIPortInterface *port, VDObjectRef plug, const uint8_t *buf, int len)
{
    SpiceVirtualChannel *svc = container_of(port, SpiceVirtualChannel, interface);
    DEBUG_SVMC_PRINTF("%s with %d bytes\n", __func__, len);
    ssize_t written = virtio_serial_write(&svc->port, buf, len);
    if (written != len)
        printf("WARNING: %s short write. %lu of %d\n", __func__, written, len);

   /* TODO:
    * we always claim the write worked. Reason: otherwise interface gives up
    * We could try to close/open interface.. but actually better to fix agent?
    */
    return len;
}

static int spice_virtual_channel_interface_read(
    VDIPortInterface *port, VDObjectRef plug, uint8_t *buf, int len)
{
    SpiceVirtualChannel *svc = container_of(port, SpiceVirtualChannel, interface);
    int actual_read = MIN(len, svc->guest_out_ring.bytes);

    DEBUG_SVMC_PRINTF(
        "%s with %d bytes, bytes = %d, read_pos = %d, will actually read %d bytes\n",
        __func__, len, svc->guest_out_ring.read_pos,
        svc->guest_out_ring.write_pos, actual_read);

    if (actual_read > 0) {
        if (actual_read + svc->guest_out_ring.read_pos > sizeof(svc->guest_out_ring.d)) {
            // two parts
            int first_part = sizeof(svc->guest_out_ring.d) - svc->guest_out_ring.read_pos;
            memcpy(buf, svc->guest_out_ring.d + svc->guest_out_ring.read_pos,
                   first_part);
            memcpy(buf + first_part, svc->guest_out_ring.d, actual_read - first_part);
            svc->guest_out_ring.read_pos = actual_read - first_part;
        } else {
            // one part
            memcpy(buf, svc->guest_out_ring.d + svc->guest_out_ring.read_pos,
                   actual_read);
            svc->guest_out_ring.read_pos += actual_read;
        }
        svc->guest_out_ring.bytes -= actual_read;
    }
    return actual_read;
}

static void spice_virtual_channel_register_interface(SpiceVirtualChannel *d)
{
    VDIPortInterface *interface = &d->interface;
    static int interface_id = 0;

    if (d->active_interface ) {
        return;
    }

    interface->base.base_version = VM_INTERFACE_VERSION;
    interface->base.type = VD_INTERFACE_VDI_PORT;
    interface->base.id = ++interface_id;
    interface->base.description = "spice virtual channel vdi port";
    interface->base.major_version = VD_INTERFACE_VDI_PORT_MAJOR;
    interface->base.minor_version = VD_INTERFACE_VDI_PORT_MINOR;

    interface->plug = spice_virtual_channel_interface_plug;
    interface->unplug = spice_virtual_channel_interface_unplug;
    interface->write = spice_virtual_channel_interface_write;
    interface->read = spice_virtual_channel_interface_read;

    d->active_interface = true;
    qemu_spice_add_interface(&interface->base);
}

static void spice_virtual_channel_unregister_interface(SpiceVirtualChannel *d)
{
    if (!d->active_interface ) {
        return;
    }
    d->active_interface = false;
    qemu_spice_remove_interface(&d->interface.base);
}


static void spice_virtual_channel_vm_change_state_handler(
                        void *opaque, int running, int reason)
{
    INFO_SVMC_PRINTF("%s running = %d reason = %d\n", __func__, running, reason);
    SpiceVirtualChannel* svc=(SpiceVirtualChannel*)opaque;

    if (running) {
        svc->running = true;
        if (svc->plug) svc->plug->wakeup(svc->plug);
    } else {
        svc->running = false;
    }
}

/*
 * virtio-serial callbacks
 */

static void spice_virtual_channel_guest_open(VirtIOSerialPort *port)
{
    SpiceVirtualChannel *svc = DO_UPCAST(SpiceVirtualChannel, port, port);
    INFO_SVMC_PRINTF("%s called (svc=%p)\n", __func__, svc);
    spice_virtual_channel_register_interface(svc);
}

static void spice_virtual_channel_guest_close(VirtIOSerialPort *port)
{
    SpiceVirtualChannel *svc = DO_UPCAST(SpiceVirtualChannel, port, port);
    INFO_SVMC_PRINTF("%s called (svc=%p)\n", __func__, svc);
    spice_virtual_channel_unregister_interface(svc);
}

static void spice_virtual_channel_guest_ready(VirtIOSerialPort *port)
{
#ifdef INFO_SVMC
    SpiceVirtualChannel *svc = DO_UPCAST(SpiceVirtualChannel, port, port);
    INFO_SVMC_PRINTF("%s called (svc=%p)\n", __func__, svc);
#endif
}

static size_t spice_virtual_channel_have_data(
                VirtIOSerialPort *port, const uint8_t *buf, size_t len)
{
    SpiceVirtualChannel *svc = DO_UPCAST(SpiceVirtualChannel, port, port);
    DEBUG_SVMC_PRINTF("%s: (svc = %llX), %ld bytes\n", __func__,
        (unsigned long long)svc, len);

    DEBUG_SVMC_PRINTF(
        "filling guest write ring. Was %u full, pos at %d, trying to write %lu\n",
        svc->guest_out_ring.bytes, svc->guest_out_ring.write_pos, len);

    if (svc->guest_out_ring.bytes == sizeof(svc->guest_out_ring.d)) {
        printf("WARNING: %s: throwing away %lu bytes due to ring being full\n",
            __func__, len);
        return len;
    }
    int bytes_read = MIN(sizeof(svc->guest_out_ring.d) - svc->guest_out_ring.bytes, len);
    if (svc->guest_out_ring.write_pos + bytes_read > sizeof(svc->guest_out_ring.d)) {
        /* two parts */
        size_t first_part = sizeof(svc->guest_out_ring.d) - svc->guest_out_ring.write_pos;
        memcpy(svc->guest_out_ring.d + svc->guest_out_ring.write_pos, buf, first_part);
        memcpy(svc->guest_out_ring.d, buf + first_part, bytes_read - first_part);
        svc->guest_out_ring.write_pos = bytes_read - first_part;
    } else {
        /* one part */
        memcpy(svc->guest_out_ring.d + svc->guest_out_ring.write_pos, buf, bytes_read);
        svc->guest_out_ring.write_pos += bytes_read;
    }
    svc->guest_out_ring.bytes += bytes_read;
    DEBUG_SVMC_PRINTF("filling guest write ring. Now %d, pos at %d, having written %d\n",
        svc->guest_out_ring.bytes, svc->guest_out_ring.write_pos, bytes_read);
    // wakeup spice
    if (svc->plug) svc->plug->wakeup(svc->plug);
    return bytes_read;
}

static int spice_virtual_channel_initfn(VirtIOSerialDevice *dev)
{
    VirtIOSerialPort *port = DO_UPCAST(VirtIOSerialPort, dev, &dev->qdev);
    SpiceVirtualChannel *svc = DO_UPCAST(SpiceVirtualChannel, port, port);
    INFO_SVMC_PRINTF("%s called: %p\n", __func__, svc);

    port->name = (char*)SPICE_VM_CHANNEL_GUEST_DEVICE_NAME;

    port->info = dev->info;

    svc->plug = NULL;

    virtio_serial_open(port);

    qemu_add_vm_change_state_handler(
        spice_virtual_channel_vm_change_state_handler, svc);

    return 0;
}

static int spice_virtual_channel_exitfn(VirtIOSerialDevice *dev)
{
    VirtIOSerialPort *port = DO_UPCAST(VirtIOSerialPort, dev, &dev->qdev);
    SpiceVirtualChannel *svc = DO_UPCAST(SpiceVirtualChannel, port, port);
    INFO_SVMC_PRINTF("%s called: %p\n", __func__, svc);
    spice_virtual_channel_unregister_interface(svc);
    virtio_serial_close(port);
    return 0;
}

static VirtIOSerialPortInfo spice_virtual_channel_info = {
    .qdev.name     = SPICE_VM_CHANNEL_DEVICE_NAME,
    .qdev.size     = sizeof(SpiceVirtualChannel),
    .init          = spice_virtual_channel_initfn,
    .exit          = spice_virtual_channel_exitfn,
    .guest_open    = spice_virtual_channel_guest_open,
    .guest_close   = spice_virtual_channel_guest_close,
    .guest_ready   = spice_virtual_channel_guest_ready,
    .have_data     = spice_virtual_channel_have_data,
    .qdev.props = (Property[]) {
        DEFINE_PROP_STRING("name", SpiceVirtualChannel, port.name),
        DEFINE_PROP_END_OF_LIST(),
    },
};

static void spice_virtual_channel_register(void)
{
    virtio_serial_port_qdev_register(&spice_virtual_channel_info);
}
device_init(spice_virtual_channel_register)
