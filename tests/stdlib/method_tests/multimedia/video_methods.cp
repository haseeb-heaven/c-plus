/* Method-by-method coverage for stdlib/video.hp (FFmpeg wrappers).
 * Uses the generated build/stdlib-tests/clip.mp4 fixture (64x48 h264).
 */
#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/video.hp"

static int g_fail = 0;

static void ck(const char *header, const char *method, int ok) {
    if (ok) {
        print(header, method, "PASS");
    } else {
        print(header, method, "FAIL");
        g_fail++;
    }
}

int main(void) {
    error err = error();
    char why[128];

    video_error_text(-1, why, sizeof(why));
    ck("video", "video_error_text", why[0] != '\0');
    ck("video", "video_stream_params", video_stream_params(NULL, 0) == NULL);
    ck("video", "video_codec_id", video_codec_id(NULL) == AV_CODEC_ID_NONE);

    /* Direct decoder constructor failure path is deterministic. */
    decoder none = decoder(NULL, -1, &err);
    ck("video", "decoder::ctor", none.valid() == 0 && err.failed_now() == 1);

    media clip = media("build/stdlib-tests/clip.mp4", &err);
    int opened = err.ok() && clip.valid();
    ck("video", "media::ctor", opened);
    if (!opened)
        return 1;

    ck("video", "media::valid", clip.valid() == 1);
    ck("video", "media::stream_count", clip.stream_count() > 0);
    ck("video", "media::video_index", clip.video_index() >= 0);
    ck("video", "media::video_width", clip.video_width() == 64);
    ck("video", "media::video_height", clip.video_height() == 48);
    ck("video", "media::video_codec", strcmp(clip.video_codec(), "h264") == 0);
    ck("video", "media::duration_ms", clip.duration_ms() > 0);

    decoder dec = clip.open_decoder(&err);
    ck("video", "media::open_decoder", err.ok() && dec.valid());
    ck("video", "decoder::valid", dec.valid() == 1);
    ck("video", "decoder::width", dec.width() == 64);
    ck("video", "decoder::height", dec.height() == 48);

    frame picture = frame(&err);
    ck("video", "frame::ctor", err.ok() && picture.valid());

    int status = dec.next_frame(&picture, &err);
    ck("video", "decoder::next_frame", status == STDLIB_OK && picture.valid());

    ck("video", "frame::valid", picture.valid() == 1);
    ck("video", "frame::width", picture.width() == 64);
    ck("video", "frame::height", picture.height() == 48);
    ck("video", "frame::format", picture.format() >= 0);
    ck("video", "frame::pts", picture.pts() >= 0);

    picture.drop();
    ck("video", "frame::drop", picture.valid() == 0 && picture.width() == 0);

    dec.drop();
    ck("video", "decoder::drop", dec.valid() == 0);

    clip.drop();
    ck("video", "media::drop", clip.valid() == 0);
    return g_fail;
}
