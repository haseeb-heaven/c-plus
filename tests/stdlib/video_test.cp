/* FFmpeg wrapper test: open a generated clip, inspect it, decode frames. */
#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/video.hp"

int main(void) {
    error err = error();
    media clip = media("build/stdlib-tests/clip.mp4", &err);
    if (!clip.valid()) {
        print("open-failed", err.what());
        return 1;
    }
    print("media", clip.stream_count() > 0, clip.video_index() >= 0,
          clip.video_width(), clip.video_height(), clip.video_codec(),
          clip.duration_ms() > 0);

    decoder dec = clip.open_decoder(&err);
    if (!dec.valid())
        return 2;
    print("reader", dec.width(), dec.height());

    frame picture = frame(&err);
    if (!picture.valid())
        return 3;

    int first = dec.next_frame(&picture, &err);
    print("frame", first, picture.width(), picture.height(),
          picture.format() >= 0, picture.valid());

    int decoded = 0;
    int status = STDLIB_OK;
    int i;
    for (i = 0; i < 8 && status == STDLIB_OK; i++) {
        status = dec.next_frame(&picture, &err);
        if (status == STDLIB_OK)
            decoded++;
    }
    print("more-frames", decoded > 0, status == STDLIB_OK || status == 1,
          picture.pts() >= 0);

    media missing = media("/nonexistent/cplus/clip.mp4", &err);
    print("missing", missing.valid(), err.failed_now(), missing.stream_count(),
          missing.video_codec());

    decoder no_media = missing.open_decoder(&err);
    print("no-reader", no_media.valid(), err.failed_now());

    char why[128];
    video_error_text(-1, why, sizeof(why));
    print("error-text", why[0] != '\0');

    if (video_codec_id(NULL) != AV_CODEC_ID_NONE)
        return 4;
    decoder direct_dec = decoder(NULL, -1, &err);
    if (direct_dec.valid() || !err.failed_now())
        return 5;
    return 0;
}
