#define AUDIO_CAP "spice"

#include "hw/hw.h"
#include "audio.h"
#include "audio_int.h"
#include "qemu-timer.h"
#include "qemu-spice.h"

#define LINE_IN_SAMPLES 1024
#define LINE_OUT_SAMPLES 1024

typedef struct InterfaceVoiceOut {
    HWVoiceOut base;
    uint64_t prev_ticks;
    int active;
} InterfaceVoiceOut;

typedef struct InterfaceVoiceIn {
    HWVoiceIn base;
    uint64_t prev_ticks;
    uint32_t samples[LINE_IN_SAMPLES];
    int active;
} InterfaceVoiceIn;

static VDObjectRef interface_play_plug(PlaybackInterface *playback, PlaybackPlug* plug,
                                       int *enabled);
static void interface_play_unplug(PlaybackInterface *playback, VDObjectRef obj);
static VDObjectRef interface_record_plug(RecordInterface *recorder, RecordPlug* plug,
                                         int *enabled);
static void interface_record_unplug(RecordInterface *recorder, VDObjectRef obj);

static struct audio_option options[] = {
    { /* end of list */ },
};

typedef struct Interface_audio {
    PlaybackInterface play_interface;
    PlaybackPlug *play_plug;
    uint32_t play_avail;
    uint32_t *play_frame;
    uint32_t *play_now;

    RecordInterface record_interface;
    RecordPlug *record_plug;
    uint32_t silence[LINE_IN_SAMPLES];

    InterfaceVoiceOut *voic_out;
    InterfaceVoiceIn *voic_in;
} Interface_audio;

static Interface_audio driver = {
    .play_interface = {
        .base.base_version  = VM_INTERFACE_VERSION,
        .base.type          = VD_INTERFACE_PLAYBACK,
        .base.description   = "playback",
        .base.major_version = VD_INTERFACE_PLAYBACK_MAJOR,
        .base.minor_version = VD_INTERFACE_PLAYBACK_MINOR,
        .plug               = interface_play_plug,
        .unplug             = interface_play_unplug,
    },
    .record_interface = {
        .base.base_version  = VM_INTERFACE_VERSION,
        .base.type          = VD_INTERFACE_RECORD,
        .base.description   = "record",
        .base.major_version = VD_INTERFACE_RECORD_MAJOR,
        .base.minor_version = VD_INTERFACE_RECORD_MINOR,
        .plug               = interface_record_plug,
        .unplug             = interface_record_unplug,
    },
};

static VDObjectRef interface_play_plug(PlaybackInterface *playback, PlaybackPlug* plug,
                                int *enabled)
{
    if (driver.play_plug) {
        return INVALID_VD_OBJECT_REF;
    }
    assert(plug && playback == &driver.play_interface && enabled);
    driver.play_plug = plug;
    *enabled = driver.voic_out ? driver.voic_out->active : 0;
    return (VDObjectRef)plug;
}

static void interface_play_unplug(PlaybackInterface *playback, VDObjectRef obj)
{
    if (!driver.play_plug || obj != (VDObjectRef)driver.play_plug) {
        return;
    }
    assert(playback == &driver.play_interface);
    driver.play_plug = NULL;
    driver.play_avail = 0;
    driver.play_frame = NULL;
    driver.play_now = NULL;
}

static VDObjectRef interface_record_plug(RecordInterface *recorder, RecordPlug* plug,
                                  int *enabled)
{
    assert(!driver.record_plug && plug && recorder == &driver.record_interface
           && enabled);
    driver.record_plug = plug;
    *enabled = driver.voic_in ? driver.voic_in->active : 0;
    return (VDObjectRef)plug;
}

static void interface_record_unplug(RecordInterface *recorder, VDObjectRef obj)
{
    assert(driver.record_plug && recorder == &driver.record_interface);
    driver.record_plug = NULL;
}

static void *interface_audio_init(void)
{
    if (VD_INTERFACE_PLAYBACK_FMT != VD_INTERFACE_AUDIO_FMT_S16 ||
        VD_INTERFACE_PLAYBACK_CHAN != 2 ||
        VD_INTERFACE_RECORD_FMT != VD_INTERFACE_AUDIO_FMT_S16 ||
        VD_INTERFACE_RECORD_CHAN != 2) {
        exit(-1);
    }
    if (!using_spice)
        return NULL;

    memset(driver.silence, 0, sizeof(driver.silence));
    qemu_spice_add_interface(&driver.play_interface.base);
    qemu_spice_add_interface(&driver.record_interface.base);
    return &driver;
}

static void interface_audio_fini(void *opaque)
{
    qemu_spice_remove_interface(&driver.play_interface.base);
    assert(!driver.play_plug);
    qemu_spice_remove_interface(&driver.record_interface.base);
    assert(!driver.record_plug);
}

static int line_out_init(HWVoiceOut *hw, struct audsettings *as)
{
    InterfaceVoiceOut *voice_out = (InterfaceVoiceOut *)hw;
    struct audsettings settings;

    settings.freq = VD_INTERFACE_PLAYBACK_FREQ;
    settings.nchannels = VD_INTERFACE_PLAYBACK_CHAN;
    settings.fmt = AUD_FMT_S16;
    settings.endianness = AUDIO_HOST_ENDIANNESS;

    audio_pcm_init_info(&hw->info, &settings);
    hw->samples = LINE_OUT_SAMPLES;
    driver.voic_out = voice_out;
    voice_out->active = 0;
    return 0;
}

static void line_out_fini(HWVoiceOut *hw)
{
    driver.voic_out = NULL;
}

static uint64_t get_monotonic_time(void)
{
    struct timespec time_space;
    clock_gettime(CLOCK_MONOTONIC, &time_space);
    return (uint64_t)time_space.tv_sec * 1000 * 1000 * 1000 + time_space.tv_nsec;
}

static int line_out_run(HWVoiceOut *hw, int live)
{
    InterfaceVoiceOut *voice_out = (InterfaceVoiceOut *)hw;
    int rpos, decr;
    int samples;
    uint64_t now;
    uint64_t ticks;
    uint64_t bytes;

    if (!live) {
        return 0;
    }

    now = get_monotonic_time();
    ticks = now - voice_out->prev_ticks;
    bytes = (ticks * hw->info.bytes_per_second) / (1000 * 1000 * 1000);

    voice_out->prev_ticks = now;

    decr = (bytes > INT_MAX) ? INT_MAX >> hw->info.shift :
                                        (bytes + (1 << (hw->info.shift - 1))) >> hw->info.shift;
    decr = audio_MIN(live, decr);

    samples = decr;
    rpos = hw->rpos;
    while (samples) {
        int left_till_end_samples = hw->samples - rpos;
        int len = audio_MIN(samples, left_till_end_samples);

        if (driver.play_plug && !driver.play_frame) {
            driver.play_plug->get_frame(driver.play_plug,
                                        &driver.play_frame,
                                        &driver.play_avail);
            driver.play_now = driver.play_frame;
        }
        if (driver.play_avail) {
            len = audio_MIN(len, driver.play_avail);
            hw->clip(driver.play_now, hw->mix_buf + rpos, len);
            if (!(driver.play_avail -= len)) {
                driver.play_plug->put_frame(driver.play_plug,
                                            driver.play_frame);
                driver.play_frame = driver.play_now = NULL;
            } else {
                driver.play_now += len;
            }
        }
        rpos = (rpos + len) % hw->samples;
        samples -= len;
    }
    hw->rpos = rpos;
    return decr;
}

static int line_out_write(SWVoiceOut *sw, void *buf, int len)
{
    return audio_pcm_sw_write(sw, buf, len);
}

static int line_out_ctl(HWVoiceOut *hw, int cmd, ...)
{
    InterfaceVoiceOut *voice_out = (InterfaceVoiceOut *)hw;

    switch (cmd) {
    case VOICE_ENABLE:
        if (voice_out->active) {
            break;
        }
        voice_out->active = 1;
        voice_out->prev_ticks = get_monotonic_time();
        if (driver.play_plug) {
            driver.play_plug->start(driver.play_plug);
        }
        break;
    case VOICE_DISABLE:
        if (!voice_out->active) {
            break;
        }
        voice_out->active = 0;
        if (driver.play_plug) {
            if (driver.play_frame) {
                uint32_t *frame = driver.play_frame;
                memset(driver.play_now, 0, driver.play_avail << 2);
                driver.play_avail = 0;
                driver.play_now = driver.play_frame = NULL;
                driver.play_plug->put_frame(driver.play_plug, frame);
            }
            driver.play_plug->stop(driver.play_plug);
        }
        break;
    }
    return 0;
}

static int line_in_init(HWVoiceIn *hw, struct audsettings *as)
{
    InterfaceVoiceIn *voice_in = (InterfaceVoiceIn *)hw;
    struct audsettings settings;

    settings.freq = VD_INTERFACE_RECORD_FREQ;
    settings.nchannels = VD_INTERFACE_RECORD_CHAN;
    if (VD_INTERFACE_PLAYBACK_FMT != VD_INTERFACE_AUDIO_FMT_S16) {
        return -1;
    }
    settings.fmt = AUD_FMT_S16;
    settings.endianness = AUDIO_HOST_ENDIANNESS;

    audio_pcm_init_info(&hw->info, &settings);
    hw->samples = LINE_IN_SAMPLES;
    driver.voic_in = voice_in;
    voice_in->active = 0;
    return 0;
}

static void line_in_fini(HWVoiceIn *hw)
{
    driver.voic_in = NULL;
}

static int line_in_run(HWVoiceIn *hw)
{
    InterfaceVoiceIn *voice_in = (InterfaceVoiceIn *)hw;
    int num_samples;
    int ready;
    int len[2];
    uint64_t now;
    uint64_t ticks;
    uint64_t delta_samp;
    uint32_t *samples;

    if (!(num_samples = hw->samples - audio_pcm_hw_get_live_in(hw))) {
        return 0;
    }

    now = get_monotonic_time();
    ticks = now - voice_in->prev_ticks;
    voice_in->prev_ticks = now;
    delta_samp = (ticks * hw->info.bytes_per_second) / (1000 * 1000 * 1000);
    delta_samp = (delta_samp + (1 << (hw->info.shift - 1))) >> hw->info.shift;

    num_samples = audio_MIN(num_samples, delta_samp);

    if (driver.record_plug) {
        ready = driver.record_plug->read(driver.record_plug, num_samples, voice_in->samples);
        samples = voice_in->samples;
    } else {
        ready = num_samples;
        samples = driver.silence;
    }

    num_samples = audio_MIN(ready, num_samples);

    if (hw->wpos + num_samples > hw->samples) {
        len[0] = hw->samples - hw->wpos;
        len[1] = num_samples - len[0];
    } else {
        len[0] = num_samples;
        len[1] = 0;
    }

    hw->conv(hw->conv_buf + hw->wpos, samples, len[0], &nominal_volume);

    if (len[1]) {
        hw->conv(hw->conv_buf, samples + len[0], len[1],
                 &nominal_volume);
    }

    hw->wpos = (hw->wpos + num_samples) % hw->samples;

    return num_samples;
}

static int line_in_read(SWVoiceIn *sw, void *buf, int size)
{
    return audio_pcm_sw_read(sw, buf, size);
}

static int line_in_ctl(HWVoiceIn *hw, int cmd, ...)
{
    InterfaceVoiceIn *voice_in = (InterfaceVoiceIn *)hw;

    switch (cmd) {
    case VOICE_ENABLE:
        if (voice_in->active) {
            break;
        }
        voice_in->active = 1;
        voice_in->prev_ticks = get_monotonic_time();
        if (driver.record_plug) {
            driver.record_plug->start(driver.record_plug);
        }
        break;
    case VOICE_DISABLE:
        if (!voice_in->active) {
            break;
        }
        voice_in->active = 0;
        if (driver.record_plug) {
            driver.record_plug->stop(driver.record_plug);
        }
        break;
    }
    return 0;
}


static struct audio_pcm_ops audio_callbacks = {
    .init_out = line_out_init,
    .fini_out = line_out_fini,
    .run_out  = line_out_run,
    .write    = line_out_write,
    .ctl_out  = line_out_ctl,

    .init_in  = line_in_init,
    .fini_in  = line_in_fini,
    .run_in   = line_in_run,
    .read     = line_in_read,
    .ctl_in   = line_in_ctl,
};

struct audio_driver spice_audio_driver = {
    .name = "spice",
    .descr = "spice audio driver",
    .options = options,
    .init = interface_audio_init,
    .fini = interface_audio_fini,
    .pcm_ops = &audio_callbacks,
    .can_be_default = 1,
    .max_voices_out = 1,
    .max_voices_in = 1,
    .voice_size_out = sizeof (InterfaceVoiceOut),
    .voice_size_in = sizeof (InterfaceVoiceIn),
};
