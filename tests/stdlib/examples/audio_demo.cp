/* Demo: miniaudio - decode a generated WAV and drive a silent device. */
#include <stdio.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/audio.hp"

static int callbacks = 0;

static void on_audio(ma_device *device, void *output, const void *input,
                     ma_uint32 frame_count) {
    (void)device;
    (void)output;
    (void)input;
    (void)frame_count;
    callbacks++;
}

int main(void) {
    error err = error();
    unsigned char wav[256];
    unsigned len = 0;
    unsigned i;
    const char *riff = "RIFFWAVEfmt ";
    for (i = 0; i < 12; i++)
        wav[len++] = (unsigned char)riff[i];
    wav[len++] = 16;
    wav[len++] = 0;
    wav[len++] = 0;
    wav[len++] = 0;
    wav[len++] = 1;
    wav[len++] = 0;
    wav[len++] = 1;
    wav[len++] = 0;
    wav[len++] = 40;
    wav[len++] = 31;
    wav[len++] = 0;
    wav[len++] = 0;
    wav[len++] = 80;
    wav[len++] = 62;
    wav[len++] = 0;
    wav[len++] = 0;
    wav[len++] = 2;
    wav[len++] = 0;
    wav[len++] = 16;
    wav[len++] = 0;
    {
        const char *data = "data";
        for (i = 0; i < 4; i++)
            wav[len++] = (unsigned char)data[i];
    }
    wav[len++] = 100;
    wav[len++] = 0;
    wav[len++] = 0;
    wav[len++] = 0;
    for (i = 0; i < 100; i++)
        wav[len++] = (unsigned char)((i % 20 < 10) ? 30 : 220);

    audio_decoder decoder = decoder_from_memory(wav, len, &err);
    print("decoder", decoder.valid(), decoder.sample_rate(), decoder.channels(),
          decoder.frame_count());

    float frames[32];
    unsigned long long read = decoder.read(frames, 16, &err);
    print("decoded frames", read, frames[0] != 0.0f);

    audio_context context = audio_context(1, &err);
    audio_device device = audio_device(context.native(), 0, 48000, 2, on_audio,
                                       NULL, &err);
    if (device.valid()) {
        device.start(&err);
        audio_sleep_ms(40);
        device.stop(&err);
    }
    print("device", device.valid(), callbacks > 0);
    return 0;
}
