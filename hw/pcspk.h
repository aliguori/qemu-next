#ifndef PC_SPK_H
#define PC_SPK_H

#include "hw.h"
#include "i8254.h"
#include "audio/audio.h"
#include "isa.h"

#define PCSPK_BUF_LEN 1792
#define PCSPK_SAMPLE_RATE 32000
#define PCSPK_MAX_FREQ (PCSPK_SAMPLE_RATE >> 1)
#define PCSPK_MIN_COUNT ((PIT_FREQ + PCSPK_MAX_FREQ - 1) / PCSPK_MAX_FREQ)

#define TYPE_PC_SPEAKER "pc-speaker"
#define PC_SPEAKER(obj) OBJECT_CHECK(PCSpkState, (obj), TYPE_PC_SPEAKER);

typedef struct PCSpkState {
    ISADevice parent;

    PITState *pit;

    /*< private >*/
    bool audio_enabled;
    uint8_t sample_buf[PCSPK_BUF_LEN];
    QEMUSoundCard card;
    SWVoiceOut *voice;
    unsigned int pit_count;
    unsigned int samples;
    unsigned int play_pos;
    int data_on;
    int dummy_refresh_clock;
} PCSpkState;

void pcspk_set_audio_enabled(PCSpkState *s, bool enable, Error **errp);
bool pcspk_get_audio_enabled(PCSpkState *s);

static inline PCSpkState *pcspk_init(PITState *pit)
{
    PCSpkState *s;

    s = PC_SPEAKER(object_new(TYPE_PC_SPEAKER));
    s->pit = pit;
    qdev_init_nofail(DEVICE(s));

    return s;
}

#endif
