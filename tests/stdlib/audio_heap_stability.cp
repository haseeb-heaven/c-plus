/* Issue #220 regression: miniaudio's ma_sound/ma_engine/ma_device/ma_decoder
 * are non-relocatable.  Wrappers returned by value must carry stable heap
 * pointers: the address seen inside the constructing/returning function must
 * still be the address used after the stack frame is gone.  A stack-embedded
 * struct changes address on return and leaves the node graph / audio threads
 * pointing at a dead frame. */
#include <stdio.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/audio.hp"

static unsigned char ahs_wav[8192];
static unsigned ahs_wav_len;
static ma_sound *ahs_sound_native;
static ma_engine *ahs_engine_native;
static ma_device *ahs_device_native;
static ma_decoder *ahs_decoder_native;
static int ahs_callbacks;

/* Direct indexing into the fixed-size global buffer keeps the checks
 * provable; subscripting a pointer parameter is not. */
#define PUT_U32(v) do {                                     \
        ahs_wav[at]      = (unsigned char)((v) & 0xff);       \
        ahs_wav[at + 1]  = (unsigned char)(((v) >> 8) & 0xff); \
        ahs_wav[at + 2]  = (unsigned char)(((v) >> 16) & 0xff); \
        ahs_wav[at + 3]  = (unsigned char)(((v) >> 24) & 0xff); \
        at += 4;                                              \
    } while (0)
#define PUT_U16(v) do {                                     \
        ahs_wav[at]     = (unsigned char)((v) & 0xff);        \
        ahs_wav[at + 1] = (unsigned char)(((v) >> 8) & 0xff);  \
        at += 2;                                              \
    } while (0)

static void build_wav(void) {
    unsigned i;
    unsigned at = 0;
    const char *r = "RIFF";
    const char *w = "WAVE";
    const char *f = "fmt ";
    const char *d = "data";
    unsigned samples = 4000;
    unsigned data_bytes = samples * 2u;
    for (i = 0; i < 4; i++) ahs_wav[at++] = (unsigned char)r[i];
    PUT_U32(36u + data_bytes);
    for (i = 0; i < 4; i++) ahs_wav[at++] = (unsigned char)w[i];
    for (i = 0; i < 4; i++) ahs_wav[at++] = (unsigned char)f[i];
    PUT_U32(16u);
    PUT_U16(1u);
    PUT_U16(1u);
    PUT_U32(8000u);
    PUT_U32(16000u);
    PUT_U16(2u);
    PUT_U16(16u);
    for (i = 0; i < 4; i++) ahs_wav[at++] = (unsigned char)d[i];
    PUT_U32(data_bytes);
    for (i = 0; i < samples; i++) {
        int sample = (i % 20 < 10) ? 8000 : -8000;
        PUT_U16((unsigned int)(sample & 0xffff));
    }
    ahs_wav_len = at;
}

static void on_audio(ma_device *device, void *output, const void *input,
                     ma_uint32 frames) {
    (void)device; (void)output; (void)input; (void)frames;
    ahs_callbacks++;
}

/* Each helper records the native pointer while the wrapper still lives on
 * its own stack frame, then returns the wrapper by value. */
static audio_decoder ahs_make_decoder(error *err) {
    audio_decoder made = decoder_from_memory(ahs_wav, ahs_wav_len, err);
    ahs_decoder_native = made.decoder;
    return made;
}

static audio_engine ahs_make_engine(ma_context *ctx, error *err) {
    audio_engine made = audio_engine(ctx, err);
    ahs_engine_native = made.native();
    return made;
}

static audio_sound ahs_make_sound(audio_engine *engine, const char *path,
                                  error *err) {
    audio_sound made = engine->load(path, err);
    ahs_sound_native = made.sound;
    return made;
}

static audio_device ahs_make_device(ma_context *ctx, error *err) {
    audio_device made = audio_device(ctx, 0, 48000, 2, on_audio, NULL, err);
    ahs_device_native = made.device;
    return made;
}

/* Deliberately dirty the stack that the old stack-embedded structs occupied. */
static void ahs_clobber_stack(void) {
    unsigned char junk[4096];
    unsigned i;
    for (i = 0; i < sizeof(junk); i++)
        junk[i] = 'z';
}

int main(void) {
    error err = error();
    stdlib_telemetry_stats before;
    stdlib_telemetry_stats after;
    FILE *tone;
    int stable;

    build_wav();
    tone = fopen("/tmp/cplus_audio_heap_tone.wav", "wb");
    if (tone == NULL)
        return 1;
    (void)fwrite(ahs_wav, 1, ahs_wav_len, tone);
    fclose(tone);

    stdlib_set_error_callback(NULL);
    stdlib_alloc_fail_reset();
    stdlib_telemetry_set_enabled(1);
    stdlib_telemetry_reset();
    stdlib_telemetry_snapshot(&before);

    {
        audio_decoder dec = ahs_make_decoder(&err);
        audio_context ctx = audio_context(1, &err);
        audio_engine eng = ahs_make_engine(ctx.native(), &err);
        audio_device dev = ahs_make_device(ctx.native(), &err);
        audio_sound snd = ahs_make_sound(&eng, "/tmp/cplus_audio_heap_tone.wav",
                                         &err);
        float frames[32];
        unsigned long long got;

        ahs_clobber_stack();
        stable = dec.valid() && dec.decoder == ahs_decoder_native &&
                 ctx.valid() && eng.valid() && eng.native() == ahs_engine_native &&
                 dev.valid() && dev.device == ahs_device_native &&
                 snd.valid() && snd.sound == ahs_sound_native;
        print("audio-heap-stable", stable);
        if (!stable)
            return 2;

        got = dec.read(frames, 16, &err);
        print("audio-heap-decode", got == 16u, err.failed_now() == 0);
        if (got != 16u || err.failed_now())
            return 3;

        if (dev.start(&err) != STDLIB_OK)
            return 4;
        audio_sleep_ms(40);
        (void)dev.stop(&err);
        print("audio-heap-device", ahs_callbacks > 0);

        {
            int play_rc = snd.play(&err);
            if (play_rc == STDLIB_OK) {
                audio_sleep_ms(20);
                print("audio-heap-play", snd.playing());
                (void)snd.stop(&err);
            } else {
                print("audio-heap-play", 0);
            }
        }

        /* Explicit drops must be idempotent: returning a wrapper by value
         * must never create a second owner of the same native handle. */
        snd.drop();
        snd.drop();
        dev.drop();
        dev.drop();
        eng.drop();
        eng.drop();
        ctx.drop();
        ctx.drop();
        dec.drop();
        dec.drop();
        print("audio-heap-dropped", !snd.valid() && !dev.valid() &&
              !eng.valid() && !ctx.valid() && !dec.valid());
        stdlib_telemetry_snapshot(&after);
        print("audio-heap-accounting",
              after.alloc_count - before.alloc_count ==
                  after.free_count - before.free_count);
        return (ahs_callbacks > 0 &&
                after.alloc_count - before.alloc_count ==
                    after.free_count - before.free_count) ? 0 : 5;
    }
}
