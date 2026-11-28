/* Modern C+ app driving real microui through a plain-C ABI wrapper. */
#include "cplus/cplus.h"
#include "stdlib/gfx_microui.hp"
#include <stdlib.h>

class MicroUIApp {
    void *backend;
    int frames;

    MicroUIApp(void) { this->backend = (void *)0; this->frames = 0; }
    int init(void) { this->backend = mi_init(); return this->backend != (void *)0; }
    void frame(int proof) { mi_frame(this->backend, proof); this->frames++; }
    void shot(const char *p) { mi_shot(this->backend, p); }
    void drop(void) { mi_drop(this->backend); }
};

int main(void) {
    const int proof = getenv("UI_PROOF") != (char *)0;
    MicroUIApp app = MicroUIApp();
    if (!app.init()) return 1;
    int running = 1;
    while (running) {
        int quit = 0;
        mi_poll(app.backend, &quit);
        if (quit) running = 0;
        if (proof && app.frames >= 4) break;
        app.frame(proof);
        if (proof && app.frames == 4) app.shot("out/microui.bmp");
    }
    int rc = 0;
    if (app.frames < 4) rc = 1;
    print("MICROUI frames=", app.frames, " events=", mi_events(app.backend),
          " calc=", mi_calc_display());
    app.drop();
    return rc;
}
