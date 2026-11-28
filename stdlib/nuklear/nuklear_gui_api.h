/* Plain-C ABI wrapper declarations for the Nuklear GUI backend.
 * The C+ app includes only this header; SDL3 and Nuklear headers stay in the
 * implementation TU, so the C+ analyser never has to parse third-party UI
 * headers. This is the supported way to use an existing C ABI library.
 */
#ifndef NUKLEAR_GUI_API_H
#define NUKLEAR_GUI_API_H

#ifdef __cplusplus
extern "C" {
#endif

void *ng_init(void);
int   ng_poll(void *ctx, int *quit);
void  ng_ui(void *ctx, int frame, int *clicks, float *value, int *check, char *name, int name_cap);
void  ng_render(void *ctx);
void  ng_shot(void *ctx, const char *path);
int   ng_should_close(const void *ctx);
void  ng_drop(void *ctx);
int   ng_events_seen(const void *ctx);

#ifdef __cplusplus
}
#endif
#endif
