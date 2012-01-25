/*
 * QEMU PC speaker emulation
 *
 * Copyright (c) 2006 Joachim Henke
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

#include "hw.h"
#include "pc.h"
#include "qemu-timer.h"
#include "pcspk.h"

static inline void generate_samples(PCSpkState *s)
{
    unsigned int i;

    if (s->pit_count) {
        const uint32_t m = PCSPK_SAMPLE_RATE * s->pit_count;
        const uint32_t n = ((uint64_t)PIT_FREQ << 32) / m;

        /* multiple of wavelength for gapless looping */
        s->samples = (PCSPK_BUF_LEN * PIT_FREQ / m * m / (PIT_FREQ >> 1) + 1) >> 1;
        for (i = 0; i < s->samples; ++i)
            s->sample_buf[i] = (64 & (n * i >> 25)) - 32;
    } else {
        s->samples = PCSPK_BUF_LEN;
        for (i = 0; i < PCSPK_BUF_LEN; ++i)
            s->sample_buf[i] = 128; /* silence */
    }
}

static void pcspk_callback(void *opaque, int free)
{
    PCSpkState *s = opaque;
    unsigned int n;

    if (pit_get_mode(s->pit, 2) != 3)
        return;

    n = pit_get_initial_count(s->pit, 2);
    /* avoid frequencies that are not reproducible with sample rate */
    if (n < PCSPK_MIN_COUNT)
        n = 0;

    if (s->pit_count != n) {
        s->pit_count = n;
        s->play_pos = 0;
        generate_samples(s);
    }

    while (free > 0) {
        n = audio_MIN(s->samples - s->play_pos, (unsigned int)free);
        n = AUD_write(s->voice, &s->sample_buf[s->play_pos], n);
        if (!n)
            break;
        s->play_pos = (s->play_pos + n) % s->samples;
        free -= n;
    }
}

void pcspk_set_audio_enabled(PCSpkState *s, bool enable, Error **errp)
{
    struct audsettings as = {PCSPK_SAMPLE_RATE, 1, AUD_FMT_U8, 0};

    if (!enable) {
        return;
    }

    s->audio_enabled = enable;

    AUD_register_card("pcspk", &s->card);

    s->voice = AUD_open_out(&s->card, s->voice, "pcspk",
                            s, pcspk_callback, &as);
    if (!s->voice) {
        error_set(errp, QERR_OPEN_FILE_FAILED, "voice");
    }
}

bool pcspk_get_audio_enabled(PCSpkState *s)
{
    return s->audio_enabled;
}

static uint32_t pcspk_ioport_read(void *opaque, uint32_t addr)
{
    PCSpkState *s = opaque;
    int out;

    s->dummy_refresh_clock ^= (1 << 4);
    out = pit_get_out(s->pit, 2, qemu_get_clock_ns(vm_clock)) << 5;

    return pit_get_gate(s->pit, 2) | (s->data_on << 1) | s->dummy_refresh_clock | out;
}

static void pcspk_ioport_write(void *opaque, uint32_t addr, uint32_t val)
{
    PCSpkState *s = opaque;
    const int gate = val & 1;

    s->data_on = (val >> 1) & 1;
    pit_set_gate(s->pit, 2, gate);
    if (s->voice) {
        if (gate) /* restart */
            s->play_pos = 0;
        AUD_set_active_out(s->voice, gate & s->data_on);
    }
}

static int pcspk_realize(ISADevice *pit)
{
    PCSpkState *s = PC_SPEAKER(pit);

    if (s->pit == NULL) {
        return -1;
    }

    register_ioport_read(0x61, 1, 1, pcspk_ioport_read, s);
    register_ioport_write(0x61, 1, 1, pcspk_ioport_write, s);

    return 0;
}

static void pcspk_initfn(Object *obj)
{
    PCSpkState *s = PC_SPEAKER(obj);

    object_property_add_bool(obj, "audio-enabled",
                             (BoolPropertyGetter *)pcspk_get_audio_enabled,
                             (BoolPropertySetter *)pcspk_set_audio_enabled,
                             NULL);

    object_property_add_link(obj, "pit", TYPE_PIT, (Object **)&s->pit, NULL);
}

static void pcspk_class_init(ObjectClass *klass, void *data)
{
    ISADeviceClass *ic = ISA_DEVICE_CLASS(klass);

    ic->init = pcspk_realize;
}

static TypeInfo pcspk_type = {
    .name = TYPE_PC_SPEAKER,
    .parent = TYPE_ISA_DEVICE,
    .instance_init = pcspk_initfn,
    .instance_size = sizeof(PCSpkState),
    .class_init = pcspk_class_init,
};

static void register_devices(void)
{
    type_register_static(&pcspk_type);
}

device_init(register_devices);
