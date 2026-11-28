/* Issue #220 regression: miniaudio wrappers must not embed non-relocatable
 * ma_* structs by value.  Returning such a wrapper by value left the engine /
 * graph / audio-thread raw pointers aimed at a dead stack frame.
 *
 * The test is headless (miniaudio null backend) and deterministic: FAIL lines
 * only on error, final line "ISSUE220 <checks> <failures>".  The structural
 * checks directly encode the root cause (wrapper smaller than the native
 * struct; native pointer outside the wrapper's own storage), and the
 * functional checks prove a by-value-returned sound/decoder still works. */
#include <stdio.h>
#include <stdint.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/audio.hp"

static int checks = 0;
static int failed = 0;

static void ck(int cond, const char *tag) {
    checks++;
    if (!cond) {
        failed++;
        printf("FAIL %s\n", tag);
    }
}

static int embedded(const void *obj, size_t obj_size, const void *ptr) {
    uintptr_t o = (uintptr_t)obj;
    uintptr_t p = (uintptr_t)ptr;
    return p >= o && p < o + obj_size;
}

/* Minimal 16-bit mono PCM WAV: 8000 Hz square wave (no pointer parameters). */
static unsigned char wav_buf[512];
static unsigned wav_len = 0;

static void put_u32(unsigned at, unsigned v) {
    wav_buf[at] = (unsigned char)(v & 0xffu);
    wav_buf[at + 1u] = (unsigned char)((v >> 8) & 0xffu);
    wav_buf[at + 2u] = (unsigned char)((v >> 16) & 0xffu);
    wav_buf[at + 3u] = (unsigned char)((v >> 24) & 0xffu);
}

static void put_u16(unsigned at, unsigned v) {
    wav_buf[at] = (unsigned char)(v & 0xffu);
    wav_buf[at + 1u] = (unsigned char)((v >> 8) & 0xffu);
}

static void build_wav(unsigned samples) {
    unsigned i;
    unsigned at = 0;
    wav_buf[at++] = 'R'; wav_buf[at++] = 'I';
    wav_buf[at++] = 'F'; wav_buf[at++] = 'F';
    put_u32(at, 36u + samples * 2u); at += 4;
    wav_buf[at++] = 'W'; wav_buf[at++] = 'A';
    wav_buf[at++] = 'V'; wav_buf[at++] = 'E';
    wav_buf[at++] = 'f'; wav_buf[at++] = 'm';
    wav_buf[at++] = 't'; wav_buf[at++] = ' ';
    put_u32(at, 16u); at += 4;
    put_u16(at, 1u); at += 2;  /* PCM */
    put_u16(at, 1u); at += 2;  /* mono */
    put_u32(at, 8000u); at += 4;
    put_u32(at, 16000u); at += 4;
    put_u16(at, 2u); at += 2;
    put_u16(at, 16u); at += 2;
    wav_buf[at++] = 'd'; wav_buf[at++] = 'a';
    wav_buf[at++] = 't'; wav_buf[at++] = 'a';
    put_u32(at, samples * 2u); at += 4;
    for (i = 0; i < samples; i++) {
        int sample = (i % 20u < 10u) ? 8000 : -8000;
        put_u16(at, (unsigned)(sample & 0xffff));
        at += 2;
    }
    wav_len = at;
}

static int callback_count = 0;

static void on_audio(ma_device *device, void *output, const void *input,
                     ma_uint32 frame_count) {
    (void)device; (void)output; (void)input; (void)frame_count;
    callback_count++;
}

/* Return by value: pre-fix this copied a ma_decoder whose inner pointers
 * targeted this function's popped stack frame. */
static audio_decoder make_decoder(error *err) {
    audio_decoder d = decoder_from_memory(wav_buf, wav_len, err);
    return d;
}

int main(void) {
    error e = error();
    float frames[64];
    const char *path = "/tmp/cplus_issue220_tone.wav";
    FILE *f;

    build_wav(200);

    /* Root cause, structural: owning pointers, not embedded native structs. */
    ck(sizeof(audio_sound) < sizeof(ma_sound), "sound_not_embedded");
    ck(sizeof(audio_engine) < sizeof(ma_engine), "engine_not_embedded");
    ck(sizeof(audio_device) < sizeof(ma_device), "device_not_embedded");
    ck(sizeof(audio_decoder) < sizeof(ma_decoder), "decoder_not_embedded");
    ck(sizeof(audio_context) < sizeof(ma_context), "context_not_embedded");

    /* A by-value-returned decoder must remain usable and cannot live inside
     * its wrapper object (stack storage was the use-after-return bug). */
    audio_decoder dec = make_decoder(&e);
    ck(dec.valid(), "decoder_valid");
    ck(dec.sample_rate() == 8000u, "decoder_rate");
    ck(dec.channels() == 1u, "decoder_channels");
    ck(dec.frame_count() == 200u, "decoder_frames");
    ck(dec.read(frames, 32, &e) == 32u, "decoder_read_after_return");
    ck(frames[0] > 0 && frames[10] < 0, "decoder_read_data");

    audio_context ctx = audio_context(1, &e);
    ck(ctx.valid(), "context_valid");
    ck(ctx.native() != (ma_context *)0 &&
       !embedded(&ctx, sizeof(audio_context), ctx.native()),
       "context_native_outside_wrapper");

    audio_device dev = audio_device(ctx.native(), 0, 48000, 2, on_audio, (void *)0, &e);
    ck(dev.valid(), "device_valid");
    ck(dev.start(&e) == STDLIB_OK, "device_start");
    audio_sleep_ms(30);
    ck(dev.running(), "device_running");
    ck(dev.stop(&e) == STDLIB_OK, "device_stop");
    ck(!dev.running(), "device_stopped");
    ck(callback_count > 0, "device_callbacks");

    f = fopen(path, "wb");
    ck(f != (FILE *)0, "wav_write_open");
    if (f != (FILE *)0) {
        (void)fwrite(wav_buf, 1, wav_len, f);
        fclose(f);
    }

    audio_engine eng = audio_engine(ctx.native(), &e);
    ck(eng.valid(), "engine_valid");
    ck(eng.native() != (ma_engine *)0 &&
       !embedded(&eng, sizeof(audio_engine), eng.native()),
       "engine_native_outside_wrapper");
    ck(eng.set_volume(0.25f, &e) == STDLIB_OK, "engine_volume");
    ck(eng.play(path, &e) == STDLIB_OK, "engine_play");

    /* A by-value-returned sound keeps a stable pointer into the live graph. */
    audio_sound snd = eng.load(path, &e);
    ck(snd.valid(), "sound_valid");
    ck(snd.play(&e) == STDLIB_OK, "sound_play");
    audio_sleep_ms(20);
    ck(snd.playing(), "sound_playing");
    ck(snd.stop(&e) == STDLIB_OK, "sound_stop");
    ck(!snd.playing(), "sound_stopped");

    printf("ISSUE220 %d %d\n", checks, failed);
    return failed != 0;
}
