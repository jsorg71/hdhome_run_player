/**
 * x11 calls
 *
 * Copyright 2015-2018 Jay Sorg <jay.sorg@gmail.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef _HDHOME_RUN_X11_H_
#define _HDHOME_RUN_X11_H_

typedef int (*tmlcb)(int sck, void* udata);

int
hdhome_run_x11_init(void);
int
hdhome_run_x11_create(void** obj);
int
hdhome_run_x11_delete(void* obj);
int
hdhome_run_x11_get_buffer(void* obj, int width, int height, int format,
                          void** buffer, int* buffer_bytes);
int
hdhome_run_x11_show_buffer(void* obj, int width, int height, int format,
                           void* buffer);
int
hdhome_run_x11_main_loop(void* obj, int* sck, tmlcb* cb, int count, void* udata,
                         int term_fd);

#endif

