/* Modern C+ app driving a real Nuklear GUI through a plain-C ABI wrapper. */
#include "cplus/cplus.h"
#include "nuklear_gui_api.h"
#include <stdlib.h>
#include <string.h>

class NuklearApp {
    void *backend;
    int clicks;
    float value;
    int check;
    char name[64];
    int frames;

    NuklearApp(void) {
        this->backend = (void *)0;
        this->clicks = 0;
        this->value = 42.0f;
        this->check = 1;
        strcpy(this->name, "Haseeb");
        this->frames = 0;
    }

    int init(void) { this->backend = ng_init(); return this->backend != (void *)0; }
    void draw(int proof) {
        if (proof) { this->value = 42.0f + (float)this->frames * 5.0f; this->clicks = this->frames; }
        ng_ui(this->backend, this->frames, &this->clicks, &this->value, &this->check, this->name, 64);
        ng_render(this->backend);
        this->frames++;
    }
    void shot(const char *p) { ng_shot(this->backend, p); }
    void drop(void) { ng_drop(this->backend); }
};

int main(void) {
    const int proof = getenv("GUI_PROOF") != (char *)0;
    NuklearApp app = NuklearApp();
    if (!app.init()) return 1;
    int running = 1;
    while (running) {
        int quit = 0;
        ng_poll(app.backend, &quit);
        if (quit) running = 0;
        if (proof && app.frames >= 4) break;
        app.draw(proof);
        if (proof && app.frames == 4) app.shot("out/nuklear_gui.bmp");
    }
    print("NUKLEAR_GUI frames=", app.frames, " events=", ng_events_seen(app.backend),
          " clicks=", app.clicks);
    int rc = 0;
    if (app.frames < 4) rc = 1;
    app.drop();
    return rc;
}
