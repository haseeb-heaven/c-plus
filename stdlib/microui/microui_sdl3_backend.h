#ifndef MICROUI_API_H
#define MICROUI_API_H
#ifdef __cplusplus
extern "C" {
#endif
void *mi_init(void);
int mi_poll(void *ctx, int *quit);
void mi_frame(void *ctx, int proof);
void mi_shot(void *ctx, const char *path);
int mi_frames(const void *ctx);
int mi_events(const void *ctx);
const char *mi_calc_display(void);
void mi_drop(void *ctx);
#ifdef __cplusplus
}
#endif
#endif
