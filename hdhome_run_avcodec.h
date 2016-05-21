/**
 * avcodec / ffmpeg calls
 *
 * Copyright 2015-2016 Jay Sorg <jay.sorg@gmail.com>
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

#ifndef _HDHOME_RUN_AVCODEC_H_
#define _HDHOME_RUN_AVCODEC_H_

int
hdhome_run_avcodec_init(void);
int
hdhome_run_avcodec_ac3_create(void** obj);
int
hdhome_run_avcodec_ac3_decode(void* obj, void* cdata, int cdata_bytes,
                              int* cdata_bytes_processed, int* decoded);
int
hdhome_run_avcodec_ac3_get_frame_info(void* obj, int* channels, int* format,
                                      int* bytes);
int
hdhome_run_avcodec_ac3_get_frame_data(void* obj, void* data, int data_bytes);

int
hdhome_run_avcodec_mpeg2_create(void** obj);
int
hdhome_run_avcodec_mpeg2_delete(void* obj);
int
hdhome_run_avcodec_mpeg2_decode(void* obj, void* cdata, int cdata_bytes,
                                int* cdata_bytes_processed, int* decoded);
int
hdhome_run_avcodec_mpeg2_get_frame_info(void* obj, int* width, int* height,
                                        int* format, int* bytes);
int
hdhome_run_avcodec_mpeg2_get_frame_data(void* obj, void* data, int data_bytes);

#endif
