/* Demo: FFmpeg - open a clip, report its streams, decode a few frames. */
#include <stdio.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/video.hp"

int main(void) {
    error err = error();
    media clip = media("build/stdlib-tests/clip.mp4", &err);
    if (!clip.valid()) {
        print("open failed", err.what());
        return 1;
    }
    print("media", clip.stream_count(), clip.video_codec(), clip.video_width(),
          clip.video_height(), clip.duration_ms());

    decoder dec = clip.open_decoder(&err);
    if (!dec.valid())
        return 2;

    frame picture = frame(&err);
    if (!picture.valid())
        return 3;

    int decoded = 0;
    int status = STDLIB_OK;
    while (status == STDLIB_OK && decoded < 5) {
        status = dec.next_frame(&picture, &err);
        if (status == STDLIB_OK) {
            decoded++;
            print("frame", decoded, picture.width(), picture.height(),
                  picture.pts());
        }
    }
    print("decoded", decoded, "end", status);

    media missing = media("/nonexistent/clip.mp4", &err);
    print("missing", missing.valid(), err.failed_now());
    return 0;
}
