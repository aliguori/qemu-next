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

#define SPICE_VMC_GUEST_DEVICE_NAME "com.redhat.spice.0"
#define SPICE_VMC_DEVICE_NAME       "spicevmc"

typedef struct {
    uint8_t  d[1024*16]; /* 16 KiB */
    uint64_t write_pos;
    uint64_t bytes;      /* in [0, sizeof(d)] */
    uint64_t read_pos;
} spice_vmc_ring_t;

size_t spice_ring_read(spice_vmc_ring_t* ring, uint8_t* buf, size_t len);
size_t spice_ring_write(spice_vmc_ring_t* ring, const uint8_t* buf, size_t len);

typedef struct SpiceVMChannel {
    VirtIOSerialPort vserport;
    bool running;
    bool active_interface;
    uint8_t active_interface_vmstate;
    VDIPortInterface interface;
    VDIPortPlug *plug;

    /* buffer the memory written by the guest until spice-server reads */
    spice_vmc_ring_t guest_out_ring;
} SpiceVMChannel;

/*
 * ring buffer
 */

size_t spice_ring_read(spice_vmc_ring_t* ring, uint8_t* buf, size_t len)
{
    size_t actual_read = MIN(len, ring->bytes);
    size_t first_part;
    if (actual_read > 0) {
        if (actual_read + ring->read_pos > sizeof(ring->d)) {
            /* two parts */
            first_part = sizeof(ring->d) - ring->read_pos;
            memcpy(buf, ring->d + ring->read_pos, first_part);
            memcpy(buf + first_part, ring->d, actual_read - first_part);
            ring->read_pos = actual_read - first_part;
        } else {
            /* one part */
            memcpy(buf, ring->d + ring->read_pos, actual_read);
            ring->read_pos += actual_read;
        }
        ring->bytes -= actual_read;
    }
    return actual_read;
}

size_t spice_ring_write(spice_vmc_ring_t* ring, const uint8_t* buf, size_t len)
{
    size_t bytes_written = 0;
    size_t first_part;
    if (ring->bytes == sizeof(ring->d)) {
        return 0;
    }
    bytes_written = MIN(sizeof(ring->d) - ring->bytes, len);
    if (ring->write_pos + bytes_written > sizeof(ring->d)) {
        /* two parts */
        first_part = sizeof(ring->d) - ring->write_pos;
        memcpy(ring->d + ring->write_pos, buf, first_part);
        memcpy(ring->d, buf + first_part, bytes_written - first_part);
        ring->write_pos = bytes_written - first_part;
    } else {
        /* one part */
        memcpy(ring->d + ring->write_pos, buf, bytes_written);
        ring->write_pos += bytes_written;
    }
    ring->bytes += bytes_written;
    return bytes_written;
}

/*
 * VDIPortInterface callbacks
 */

static VDObjectRef spice_vmc_interface_plug(
                VDIPortInterface *port, VDIPortPlug* plug)
{
    SpiceVMChannel *svc = container_of(port, SpiceVMChannel, interface);
    if (svc->plug) {
        return INVALID_VD_OBJECT_REF;
    }
    svc->plug = plug;
    return (VDObjectRef)plug;
}

static void spice_vmc_interface_unplug(
                VDIPortInterface *port, VDObjectRef plug)
{
    SpiceVMChannel *svc = container_of(port, SpiceVMChannel, interface);
    if (!plug || plug != (VDObjectRef)svc->plug) {
        return;
    }
    svc->plug = NULL;
}

static int spice_vmc_interface_write(
    VDIPortInterface *port, VDObjectRef plug, const uint8_t *buf, int len)
{
    SpiceVMChannel *svc = container_of(port, SpiceVMChannel, interface);
    ssize_t written = virtio_serial_write(&svc->vserport, buf, len);

    if (written != len) {
        printf("WARNING: %s short write. %lu of %d\n", __func__, written, len);
    }

   /* TODO:
    * we always claim the write worked. Reason: otherwise interface gives up
    * We could try to close/open interface.. but actually better to fix agent?
    */
    return len;
}

static int spice_vmc_interface_read(
    VDIPortInterface *port, VDObjectRef plug, uint8_t *buf, int len)
{
    int actual_read;
    SpiceVMChannel *svc = container_of(port, SpiceVMChannel, interface);

    actual_read = spice_ring_read(&(svc->guest_out_ring), buf, len);
    return actual_read;
}

static void spice_vmc_register_interface(SpiceVMChannel *svc)
{
    VDIPortInterface *interface = &svc->interface;
    static int interface_id = 0;

    if (svc->active_interface ) {
        return;
    }

    interface->base.base_version = VM_INTERFACE_VERSION;
    interface->base.type = VD_INTERFACE_VDI_PORT;
    interface->base.id = ++interface_id;
    interface->base.description = "spice virtual channel vdi port";
    interface->base.major_version = VD_INTERFACE_VDI_PORT_MAJOR;
    interface->base.minor_version = VD_INTERFACE_VDI_PORT_MINOR;

    interface->plug = spice_vmc_interface_plug;
    interface->unplug = spice_vmc_interface_unplug;
    interface->write = spice_vmc_interface_write;
    interface->read = spice_vmc_interface_read;

    svc->active_interface = true;
    qemu_spice_add_interface(&interface->base);
}

static void spice_vmc_unregister_interface(SpiceVMChannel *svc)
{
    if (!svc->active_interface ) {
        return;
    }
    svc->active_interface = false;
    qemu_spice_remove_interface(&svc->interface.base);
}


static void spice_vmc_vm_change_state_handler(
                        void *opaque, int running, int reason)
{
    SpiceVMChannel* svc = opaque;

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

static void spice_vmc_guest_open(VirtIOSerialPort *vserport)
{
    SpiceVMChannel *svc = DO_UPCAST(SpiceVMChannel, vserport, vserport);
    spice_vmc_register_interface(svc);
}

static void spice_vmc_guest_close(VirtIOSerialPort *vserport)
{
    SpiceVMChannel *svc = DO_UPCAST(SpiceVMChannel, vserport, vserport);
    spice_vmc_unregister_interface(svc);
}

static void spice_vmc_guest_ready(VirtIOSerialPort *vserport)
{
}

static void spice_vmc_have_data(
                VirtIOSerialPort *vserport, const uint8_t *buf, size_t len)
{
    SpiceVMChannel *svc = DO_UPCAST(SpiceVMChannel, vserport, vserport);
    int bytes_written;

    bytes_written = spice_ring_write(&(svc->guest_out_ring), buf, len);
    if (bytes_written != len) {
        printf("WARNING: %s: threw away %lu bytes due to ring being full\n",
            __func__, (len - bytes_written));
        return;
    }
    if (svc->plug) {
        svc->plug->wakeup(svc->plug);
    }
    return;
}

static int spice_vmc_post_load(void *opaque, int version_id)
{
    SpiceVMChannel* svc = opaque;
    if (svc->active_interface_vmstate) {
        spice_vmc_register_interface(svc);
    }
    return 0;
}

static void spice_vmc_pre_save(void *opaque)
{
    SpiceVMChannel* svc = opaque;
    svc->active_interface_vmstate = svc->active_interface;
}

static VMStateDescription spice_vmc_vmstate = {
    .name = SPICE_VMC_DEVICE_NAME,
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = spice_vmc_post_load,
    .pre_save = spice_vmc_pre_save,
    .fields = (VMStateField []) {
        VMSTATE_UINT8(active_interface_vmstate, SpiceVMChannel),
        VMSTATE_END_OF_LIST()
    }
};

static int spice_vmc_initfn(VirtIOSerialDevice *dev)
{
    VirtIOSerialPort *vserport = DO_UPCAST(VirtIOSerialPort, dev, &dev->qdev);
    SpiceVMChannel *svc = DO_UPCAST(SpiceVMChannel, vserport, vserport);

    vserport->name = (char*)SPICE_VMC_GUEST_DEVICE_NAME;

    vserport->info = dev->info;

    svc->plug = NULL;

    virtio_serial_open(vserport);

    qemu_add_vm_change_state_handler(
        spice_vmc_vm_change_state_handler, svc);

    return 0;
}

static int spice_vmc_exitfn(VirtIOSerialDevice *dev)
{
    VirtIOSerialPort *vserport = DO_UPCAST(VirtIOSerialPort, dev, &dev->qdev);
    SpiceVMChannel *svc = DO_UPCAST(SpiceVMChannel, vserport, vserport);

    spice_vmc_unregister_interface(svc);
    virtio_serial_close(vserport);
    return 0;
}

static VirtIOSerialPortInfo spice_vmc_info = {
    .qdev.name     = SPICE_VMC_DEVICE_NAME,
    .qdev.size     = sizeof(SpiceVMChannel),
    .qdev.vmsd     = &spice_vmc_vmstate,
    .init          = spice_vmc_initfn,
    .exit          = spice_vmc_exitfn,
    .guest_open    = spice_vmc_guest_open,
    .guest_close   = spice_vmc_guest_close,
    .guest_ready   = spice_vmc_guest_ready,
    .have_data     = spice_vmc_have_data,
    .qdev.props = (Property[]) {
        DEFINE_PROP_UINT32("nr", SpiceVMChannel, vserport.id,
                           VIRTIO_CONSOLE_BAD_ID),
        DEFINE_PROP_STRING("name", SpiceVMChannel, vserport.name),
        DEFINE_PROP_END_OF_LIST(),
    },
};

static void spice_vmc_register(void)
{
    virtio_serial_port_qdev_register(&spice_vmc_info);
}
device_init(spice_vmc_register)
