/*
 * Spice Virtual Machine Channel (VMC).
 *
 * A specialized virtio-serial port used for spice to guest communication,
 * used by spice client and a daemon in the guest operating system (vdservice).
 * Connects the VDIPortInterface exposed by spice.h to a virtio-serial-bus
 * port.
 *
 * Usage:
 *  Current: mouse data (works better in wan environment), screen resize.
 *  Planned: shared clipboard.
 *
 * Author: Alon Levy <alevy@redhat.com>
 *
 * Copyright (c) 2010 Red Hat, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <spice.h>
#include <vd_interface.h>

#include "virtio-serial.h"
#include "qemu-spice.h"

#define SPICE_VM_CHANNEL_GUEST_DEVICE_NAME "org.redhat.spice.0"
#define SPICE_VM_CHANNEL_DEVICE_NAME       "spicevmc"

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
    if (d->plug) {
        return INVALID_VD_OBJECT_REF;
    }
    d->plug = plug;
    return (VDObjectRef)plug;
}

static void spice_virtual_channel_interface_unplug(
                VDIPortInterface *port, VDObjectRef plug)
{
    SpiceVirtualChannel *d = container_of(port, SpiceVirtualChannel, interface);
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
        printf("%s: TODO - notify_guest! what to do??\n", __func__);
    }
}

static int spice_virtual_channel_interface_write(
    VDIPortInterface *port, VDObjectRef plug, const uint8_t *buf, int len)
{
    SpiceVirtualChannel *svc = container_of(port, SpiceVirtualChannel, interface);
    ssize_t written = virtio_serial_write(&svc->port, buf, len);

    if (written != len) {
        printf("WARNING: %s short write. %lu of %d\n", __func__, written, len);
    }

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
    SpiceVirtualChannel* svc=(SpiceVirtualChannel*)opaque;

    if (running) {
        svc->running = true;
        if (svc->plug) {
            svc->plug->wakeup(svc->plug);
        }
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
    spice_virtual_channel_register_interface(svc);
}

static void spice_virtual_channel_guest_close(VirtIOSerialPort *port)
{
    SpiceVirtualChannel *svc = DO_UPCAST(SpiceVirtualChannel, port, port);
    spice_virtual_channel_unregister_interface(svc);
}

static void spice_virtual_channel_guest_ready(VirtIOSerialPort *port)
{
}

static void spice_virtual_channel_have_data(
                VirtIOSerialPort *port, const uint8_t *buf, size_t len)
{
    SpiceVirtualChannel *svc = DO_UPCAST(SpiceVirtualChannel, port, port);

    if (svc->guest_out_ring.bytes == sizeof(svc->guest_out_ring.d)) {
        printf("WARNING: %s: throwing away %lu bytes due to ring being full\n",
            __func__, len);
        return;
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
    // wakeup spice
    if (svc->plug) {
        svc->plug->wakeup(svc->plug);
    }
    return;
}

static int spice_virtual_channel_initfn(VirtIOSerialDevice *dev)
{
    VirtIOSerialPort *port = DO_UPCAST(VirtIOSerialPort, dev, &dev->qdev);
    SpiceVirtualChannel *svc = DO_UPCAST(SpiceVirtualChannel, port, port);

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
