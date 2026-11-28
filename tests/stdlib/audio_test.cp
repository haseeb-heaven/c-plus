/* Audio wrapper test: WAV decode, device lifecycle on the null backend. */
#include <stdio.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/audio.hp"

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

/* Minimal 16-bit mono PCM WAV: 8000 Hz, square wave. */
static unsigned build_wav(unsigned char *buf, unsigned samples) {
    unsigned data_bytes = samples * 2;
    unsigned i;
    unsigned at = 0;
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

    audio_decoder mem = decoder_from_memory(wav, wav_len, &err);
    print("decode", mem.valid(), mem.sample_rate(), mem.channels(),
          mem.frame_count());

    float frames[64];
    unsigned long long got = mem.read(frames, 32, &err);
    print("frames", got, frames[0] > 0, frames[10] < 0);

    unsigned char junk[16] = {0, 1, 2, 3};
    audio_decoder broken = decoder_from_memory(junk, sizeof(junk), &err);
    print("junk", broken.valid(), err.failed_now());

    audio_decoder missing = audio_decoder("/nonexistent/tone.wav", &err);
    print("missing", missing.valid(), err.failed_now());

    audio_context context = audio_context(1, &err);
    print("context", context.valid(), err.failed_now());

    audio_device device = audio_device(context.native(), 0, 48000, 2, on_audio,
                                       NULL, &err);
    if (!device.valid())
        return 1;
    if (device.start(&err) != STDLIB_OK)
        return 2;
    audio_sleep_ms(60);
    int running = device.running();
    if (device.stop(&err) != STDLIB_OK)
        return 3;
    print("device", running, callback_count > 0, device.running());

    audio_device bad = audio_device(NULL, 0, 48000, 2, NULL, NULL, &err);
    print("bad-device", bad.valid(), err.failed_now());

    /* Write the same WAV to a file so the engine can load it. */
    FILE *tone = fopen("/tmp/cplus_tone.wav", "wb");
    if (tone != NULL) {
        (void)fwrite(wav, 1, wav_len, tone);
        fclose(tone);
    }

    audio_engine engine = audio_engine(context.native(), &err);
    print("engine", engine.valid(), engine.native() != NULL,
          engine.set_volume(0.25f, &err) == STDLIB_OK);
    print("engine-play", engine.play("/tmp/cplus_tone.wav", &err) == STDLIB_OK);

    audio_sound sound = engine.load("/tmp/cplus_tone.wav", &err);
    if (sound.valid()) {
        sound.play(&err);
        audio_sleep_ms(20);
        int playing = sound.playing();
        sound.stop(&err);
        print("sound", sound.valid(), playing, sound.playing() == 0);
    }

    audio_sound missing_sound = engine.load("/nonexistent/tone.wav", &err);
    print("sound-missing", missing_sound.valid(), err.failed_now());

    audio_sound bad_snd = audio_sound(NULL, NULL, &err);
    if (bad_snd.valid() || !err.failed_now())
        return 4;
    return 0;
}
