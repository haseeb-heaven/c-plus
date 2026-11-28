/* Issue #177 regression: audio gain and device arguments must be validated.
 *
 * Headless and deterministic: uses the miniaudio null backend, prints only a
 * summary line, and exits non-zero if any check fails.  Every refusal must set
 * the documented error category without touching miniaudio's mixer/device. */
#include <math.h>
#include <stdio.h>
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

static void on_audio(ma_device *device, void *output, const void *input,
                     ma_uint32 frame_count) {
    (void)device;
    (void)output;
    (void)input;
    (void)frame_count;
}

int main(void) {
    error e = error();
    audio_context ctx = audio_context(1, &e);
    audio_engine engine;
    audio_device bad_rate;
    audio_device bad_channels;
    audio_device bad_callback;
    audio_device bad_context;
    audio_device good;

    stdlib_set_error_callback((stdlib_error_callback)0);
    ck(ctx.valid() == 1, "null_context");
    if (!ctx.valid()) {
        printf("ISSUE177 %d %d\n", checks, failed);
        return 1;
    }

    engine = audio_engine(ctx.native(), &e);
    ck(engine.valid() == 1, "engine_init");
    if (!engine.valid()) {
        ctx.drop();
        printf("ISSUE177 %d %d\n", checks, failed);
        return 1;
    }

    /* Non-finite and negative gains are refused before reaching miniaudio. */
    ck(engine.set_volume(NAN, &e) == STDLIB_FAIL, "nan_refused");
    ck(e.failed_now() == 1 && e.category() == STDLIB_CATEGORY_RANGE &&
           e.code() == CPLUS_EXIT_BOUNDS,
       "nan_error");
    ck(engine.set_volume(INFINITY, &e) == STDLIB_FAIL, "inf_refused");
    ck(e.category() == STDLIB_CATEGORY_RANGE, "inf_error");
    ck(engine.set_volume(-INFINITY, &e) == STDLIB_FAIL, "neg_inf_refused");
    ck(e.category() == STDLIB_CATEGORY_RANGE, "neg_inf_error");
    ck(engine.set_volume(-1.0f, &e) == STDLIB_FAIL, "negative_refused");
    ck(e.failed_now() == 1 && e.category() == STDLIB_CATEGORY_RANGE,
       "negative_error");

    /* Zero and an ordinary positive gain remain accepted and clear `err`. */
    ck(engine.set_volume(0.0f, &e) == STDLIB_OK && e.ok(), "zero_ok");
    ck(engine.set_volume(0.25f, &e) == STDLIB_OK && e.ok(), "positive_ok");

    /* Invalid device arguments are refused with STDLIB_CATEGORY_INVALID. */
    bad_rate = audio_device(ctx.native(), 0, 0u, 2u, on_audio, (void *)0, &e);
    ck(bad_rate.valid() == 0, "zero_sample_rate_refused");
    ck(e.failed_now() == 1 && e.category() == STDLIB_CATEGORY_INVALID,
       "zero_sample_rate_error");
    bad_channels = audio_device(ctx.native(), 0, 48000u, 0u, on_audio,
                                (void *)0, &e);
    ck(bad_channels.valid() == 0 && e.failed_now() == 1 &&
           e.category() == STDLIB_CATEGORY_INVALID,
       "zero_channels_refused");
    bad_callback = audio_device(ctx.native(), 0, 48000u, 2u, NULL,
                                (void *)0, &e);
    ck(bad_callback.valid() == 0 && e.failed_now() == 1 &&
           e.category() == STDLIB_CATEGORY_INVALID,
       "null_callback_refused");
    bad_context = audio_device(NULL, 0, 48000u, 2u, on_audio,
                               (void *)0, &e);
    ck(bad_context.valid() == 0 && e.failed_now() == 1,
       "null_context_refused");

    /* Valid arguments still construct a usable device. */
    good = audio_device(ctx.native(), 0, 48000u, 2u, on_audio, (void *)0, &e);
    ck(good.valid() == 1 && e.ok(), "valid_device");
    good.drop();

    engine.drop();
    ck(engine.set_volume(0.5f, &e) == STDLIB_FAIL, "dropped_engine_refused");

    ctx.drop();
    printf("ISSUE177 %d %d\n", checks, failed);
    return failed == 0 ? 0 : 1;
}
