#line 1 "tests/stdlib/audio_test.cp"
/* Audio wrapper test: WAV decode, device lifecycle on the null backend. */
#include <stdio.h>
#include "cplus/cplus.h"
#include "/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/cplus-hp-73955-0.h"
#include "/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/cplus-hp-73955-1.h"

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
    error err = error_ctor();
    unsigned char wav[512];
    unsigned wav_len = build_wav(wav, 200);

    audio_decoder mem = decoder_from_memory(wav, wav_len, &err);
    print("decode", audio_decoder_valid(&mem), audio_decoder_sample_rate(&mem), audio_decoder_channels(&mem),
          audio_decoder_frame_count(&mem));

    float frames[64];
    unsigned long long got = audio_decoder_read(&mem, frames, 32, &err);
    print("frames", got, frames[0] > 0, frames[10] < 0);

    unsigned char junk[16] = {0, 1, 2, 3};
    audio_decoder broken = decoder_from_memory(junk, sizeof(junk), &err);
    print("junk", audio_decoder_valid(&broken), error_failed_now(&err));

    audio_decoder missing = audio_decoder_ctor("/nonexistent/tone.wav", &err);
    print("missing", audio_decoder_valid(&missing), error_failed_now(&err));

    audio_context context = audio_context_ctor(1, &err);
    print("context", audio_context_valid(&context), error_failed_now(&err));

    audio_device device = audio_device_ctor(audio_context_native(&context), 0, 48000, 2, on_audio,
                                       NULL, &err);
    if (!audio_device_valid(&device))
        { __typeof__(1) __cplus_ret = (1); audio_device_drop(&device); audio_context_drop(&context); audio_decoder_drop(&missing); audio_decoder_drop(&broken); audio_decoder_drop(&mem);  return __cplus_ret;; }
    if (audio_device_start(&device, &err) != STDLIB_OK)
        { __typeof__(2) __cplus_ret = (2); audio_device_drop(&device); audio_context_drop(&context); audio_decoder_drop(&missing); audio_decoder_drop(&broken); audio_decoder_drop(&mem);  return __cplus_ret;; }
    audio_sleep_ms(60);
    int running = audio_device_running(&device);
    if (audio_device_stop(&device, &err) != STDLIB_OK)
        { __typeof__(3) __cplus_ret = (3); audio_device_drop(&device); audio_context_drop(&context); audio_decoder_drop(&missing); audio_decoder_drop(&broken); audio_decoder_drop(&mem);  return __cplus_ret;; }
    print("device", running, callback_count > 0, audio_device_running(&device));

    audio_device bad = audio_device_ctor(NULL, 0, 48000, 2, NULL, NULL, &err);
    print("bad-device", audio_device_valid(&bad), error_failed_now(&err));

    /* Write the same WAV to a file so the engine can load it. */
    FILE *tone = fopen("/tmp/cplus_tone.wav", "wb");
    if (tone != NULL) {
        (void)fwrite(wav, 1, wav_len, tone);
        fclose(tone);
    }

    audio_engine engine = audio_engine_ctor(audio_context_native(&context), &err);
    print("engine", audio_engine_valid(&engine), audio_engine_native(&engine) != NULL,
          audio_engine_set_volume(&engine, 0.25f, &err) == STDLIB_OK);
    print("engine-play", audio_engine_play(&engine, "/tmp/cplus_tone.wav", &err) == STDLIB_OK);

    audio_sound sound = audio_engine_load(&engine, "/tmp/cplus_tone.wav", &err);
    if (audio_sound_valid(&sound)) {
        audio_sound_play(&sound, &err);
        audio_sleep_ms(20);
        int playing = audio_sound_playing(&sound);
        audio_sound_stop(&sound, &err);
        print("sound", audio_sound_valid(&sound), playing, audio_sound_playing(&sound) == 0);
    }

    audio_sound missing_sound = audio_engine_load(&engine, "/nonexistent/tone.wav", &err);
    print("sound-missing", audio_sound_valid(&missing_sound), error_failed_now(&err));
    { __typeof__(0) __cplus_ret = (0); audio_sound_drop(&missing_sound); audio_sound_drop(&sound); audio_engine_drop(&engine); audio_device_drop(&bad); audio_device_drop(&device); audio_context_drop(&context); audio_decoder_drop(&missing); audio_decoder_drop(&broken); audio_decoder_drop(&mem);  return __cplus_ret;; }
audio_sound_drop(&missing_sound); audio_sound_drop(&sound); audio_engine_drop(&engine); audio_device_drop(&bad); audio_device_drop(&device); audio_context_drop(&context); audio_decoder_drop(&missing); audio_decoder_drop(&broken); audio_decoder_drop(&mem); 
#line 136 "tests/stdlib/audio_test.cp"
}