/* Method-by-method coverage for stdlib/audio.hp (miniaudio).
 * Uses miniaudio's null backend and a generated in-memory WAV, so no audio
 * hardware is required on macOS.
 */
#include <stdio.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/audio.hp"

static int g_fail = 0;

static void ck(const char *header, const char *method, int ok) {
    if (ok) {
        print(header, method, "PASS");
    } else {
        print(header, method, "FAIL");
        g_fail++;
    }
}

static void put_u32(unsigned char *p, unsigned int v) {
    p[0] = (unsigned char)(v & 0xff);
    p[1] = (unsigned char)((v >> 8) & 0xff);
    p[2] = (unsigned char)((v >> 16) & 0xff);
    p[3] = (unsigned char)((v >> 24) & 0xff);
}

static void put_u16(unsigned char *p, unsigned int v) {
    p[0] = (unsigned char)(v & 0xff);
    p[1] = (unsigned char)((v >> 8) & 0xff);
}

/* 8 kHz 16-bit mono PCM square wave, 200 samples. */
static unsigned build_wav(unsigned char *buf, unsigned samples) {
    unsigned data_bytes = samples * 2;
    unsigned at = 0;
    unsigned i;
    const char *riff = "RIFF";
    const char *wave = "WAVE";
    const char *fmt = "fmt ";
    const char *data = "data";
    for (i = 0; i < 4; i++)
        buf[at++] = (unsigned char)riff[i];
    put_u32(buf + at, 36 + data_bytes);
    at += 4;
    for (i = 0; i < 4; i++)
        buf[at++] = (unsigned char)wave[i];
    for (i = 0; i < 4; i++)
        buf[at++] = (unsigned char)fmt[i];
    put_u32(buf + at, 16);
    at += 4;
    put_u16(buf + at, 1);
    at += 2;
    put_u16(buf + at, 1);
    at += 2;
    put_u32(buf + at, 8000);
    at += 4;
    put_u32(buf + at, 16000);
    at += 4;
    put_u16(buf + at, 2);
    at += 2;
    put_u16(buf + at, 16);
    at += 2;
    for (i = 0; i < 4; i++)
        buf[at++] = (unsigned char)data[i];
    put_u32(buf + at, data_bytes);
    at += 4;
    for (i = 0; i < samples; i++) {
        int sample = (i % 20 < 10) ? 8000 : -8000;
        put_u16(buf + at, (unsigned int)(sample & 0xffff));
        at += 2;
    }
    return at;
}

static int callback_count = 0;

static void on_audio(ma_device *device, void *output, const void *input,
                     ma_uint32 frame_count) {
    (void)device;
    (void)input;
    (void)output;
    (void)frame_count;
    callback_count++;
}

int main(void) {
    error err = error();
    unsigned char wav[512];
    unsigned wav_len = build_wav(wav, 200);
    const char *path = "/tmp/cplus_mt_tone.wav";
    FILE *out = fopen(path, "wb");
    if (out != NULL) {
        (void)fwrite(wav, 1, wav_len, out);
        fclose(out);
    }

    /* audio_sleep_ms: monotonic time must not go backwards. */
    struct timespec t0;
    struct timespec t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    audio_sleep_ms(2);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    ck("audio", "audio_sleep_ms",
       t1.tv_sec > t0.tv_sec ||
           (t1.tv_sec == t0.tv_sec && t1.tv_nsec >= t0.tv_nsec));

    audio_decoder mem = decoder_from_memory(wav, wav_len, &err);
    ck("audio", "decoder_from_memory", err.ok() && mem.valid());

    audio_decoder dec = audio_decoder(path, &err);
    ck("audio", "audio_decoder::ctor", err.ok() && dec.valid());
    ck("audio", "audio_decoder::valid", dec.valid() == 1);
    ck("audio", "audio_decoder::channels", dec.channels() == 1);
    ck("audio", "audio_decoder::sample_rate", dec.sample_rate() == 8000);
    ck("audio", "audio_decoder::frame_count", dec.frame_count() == 200);

    float frames[64];
    unsigned long long got = dec.read(frames, 32, &err);
    ck("audio", "audio_decoder::read", got == 32 && frames[0] > 0.0f);

    dec.drop();
    ck("audio", "audio_decoder::drop", dec.valid() == 0);
    mem.drop();

    audio_context ctx = audio_context(1, &err);
    ck("audio", "audio_context::ctor", err.ok());
    ck("audio", "audio_context::valid", ctx.valid() == 1);
    ck("audio", "audio_context::native", ctx.native() != NULL);

    audio_engine eng = audio_engine(ctx.native(), &err);
    ck("audio", "audio_engine::ctor", err.ok());
    ck("audio", "audio_engine::valid", eng.valid() == 1);
    ck("audio", "audio_engine::native", eng.native() != NULL);
    ck("audio", "audio_engine::set_volume", eng.set_volume(0.5f, &err) == STDLIB_OK);
    ck("audio", "audio_engine::play", eng.play(path, &err) == STDLIB_OK);

    audio_sound snd = eng.load(path, &err);
    ck("audio", "audio_engine::load", err.ok() && snd.valid());

    audio_sound direct = audio_sound(eng.native(), path, &err);
    ck("audio", "audio_sound::ctor", err.ok() && direct.valid());

    ck("audio", "audio_sound::valid", snd.valid() == 1);
    ck("audio", "audio_sound::play", snd.play(&err) == STDLIB_OK);
    audio_sleep_ms(20);
    ck("audio", "audio_sound::playing", snd.playing() == 1);
    ck("audio", "audio_sound::stop", snd.stop(&err) == STDLIB_OK);

    snd.drop();
    ck("audio", "audio_sound::drop", snd.valid() == 0);
    direct.drop();
    eng.drop();
    ck("audio", "audio_engine::drop", eng.valid() == 0);

    audio_device dev = audio_device(ctx.native(), 0, 48000, 2, on_audio, NULL, &err);
    ck("audio", "audio_device::ctor", err.ok() && dev.valid());
    ck("audio", "audio_device::valid", dev.valid() == 1);
    ck("audio", "audio_device::start", dev.start(&err) == STDLIB_OK);
    audio_sleep_ms(40);
    ck("audio", "audio_device::running", dev.running() == 1 && callback_count > 0);
    ck("audio", "audio_device::stop", dev.stop(&err) == STDLIB_OK);

    dev.drop();
    ck("audio", "audio_device::drop", dev.valid() == 0);

    ctx.drop();
    ck("audio", "audio_context::drop", ctx.valid() == 0);
    return g_fail;
}
