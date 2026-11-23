/* Method-by-method coverage for stdlib/input.hp (SDL3 event helpers).
 * All events are synthetic and the video driver is the dummy driver.
 */
#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/window.hp"
#include "stdlib/input.hp"

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
    sdl core = sdl((unsigned)(SDL_INIT_VIDEO | SDL_INIT_EVENTS), &err);
    if (!core.valid())
        return 1;

    SDL_Event pushed;
    memset(&pushed, 0, sizeof(pushed));
    pushed.type = SDL_EVENT_QUIT;
    SDL_PushEvent(&pushed);

    SDL_Event got_event;
    memset(&got_event, 0, sizeof(got_event));
    int got = poll_event(&got_event);
    ck("input", "poll_event", got == 1 && got_event.type == SDL_EVENT_QUIT);

    SDL_Event quit_ev;
    memset(&quit_ev, 0, sizeof(quit_ev));
    quit_ev.type = SDL_EVENT_QUIT;
    ck("input", "event_is_quit", event_is_quit(&quit_ev) == 1);

    SDL_Event down_ev;
    memset(&down_ev, 0, sizeof(down_ev));
    down_ev.type = SDL_EVENT_KEY_DOWN;
    ck("input", "event_is_key_down", event_is_key_down(&down_ev) == 1);

    SDL_Event up_ev;
    memset(&up_ev, 0, sizeof(up_ev));
    up_ev.type = SDL_EVENT_KEY_UP;
    ck("input", "event_is_key_up", event_is_key_up(&up_ev) == 1);

    SDL_Event key_ev;
    memset(&key_ev, 0, sizeof(key_ev));
    key_ev.type = SDL_EVENT_KEY_DOWN;
    key_ev.key.scancode = SDL_SCANCODE_A;
    ck("input", "event_scancode", event_scancode(&key_ev) == SDL_SCANCODE_A);

    SDL_Event text_ev;
    memset(&text_ev, 0, sizeof(text_ev));
    text_ev.type = SDL_EVENT_TEXT_INPUT;
    text_ev.text.text = "Z";
    ck("input", "event_text", event_text(&text_ev) == 'Z');

    const bool *keys = keyboard_state();
    ck("input", "keyboard_state", keys != NULL);

    bool local_keys[SDL_SCANCODE_COUNT];
    int i;
    for (i = 0; i < (int)SDL_SCANCODE_COUNT; i++)
        local_keys[i] = false;
    local_keys[SDL_SCANCODE_A] = true;
    ck("input", "key_pressed",
       key_pressed(local_keys, SDL_SCANCODE_A) == 1 &&
           key_pressed(NULL, SDL_SCANCODE_A) == 0);

    float mx = -1.0f;
    float my = -1.0f;
    unsigned mask = mouse_state(&mx, &my);
    ck("input", "mouse_state",
       mx >= 0.0f && my >= 0.0f && (mask & ~0x1fu) == 0u);

    ck("input", "mouse_button_left",
       mouse_button_left((unsigned)SDL_BUTTON_LMASK) == 1 &&
           mouse_button_left(0) == 0);

    core.drop();
    return g_fail;
}
