
#ifndef _HDHOME_RUN_X11_H_
#define _HDHOME_RUN_X11_H_

typedef int (*tmlcb)(int sck, void* udata);

int
hdhome_run_x11_init(void);
int
hdhome_run_x11_get_buffer(int width, int height, int format,
                          void** buffer, int* buffer_bytes);
int
hdhome_run_x11_show_buffer(int width, int height, int format,
                           void* buffer);
int
hdhome_run_x11_main_loop(int sck, tmlcb cb, void* udata);

#endif

