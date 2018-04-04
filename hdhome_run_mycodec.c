/**
 * mycodec calls
 *
 * Copyright 2018 Jay Sorg <jay.sorg@gmail.com>
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hdhome_run_codec.h"

#define LLOG_LEVEL 1
#define LLOGLN(_level, _args) \
  do \
  { \
    if (_level < LLOG_LEVEL) \
    { \
        printf _args ; \
        printf("\n"); \
    } \
  } \
  while (0)

struct mycodec_audio
{
    int pad0;
};

struct mycodec_video
{
    int pad0;
};

/*****************************************************************************/
int
hdhome_run_codec_init(void)
{
    return 0;
}

/*****************************************************************************/
int
hdhome_run_codec_audio_create(void** obj, int codec_id)
{
    if (obj == NULL)
    {
        return 1;
    }
    return 0;
}

/*****************************************************************************/
int
hdhome_run_codec_audio_delete(void* obj)
{
    return 0;
}

/*****************************************************************************/
int
hdhome_run_codec_audio_decode(void* obj, void* cdata, int cdata_bytes,
                              int* cdata_bytes_processed, int* decoded)
{
    LLOGLN(0, ("hdhome_run_codec_audio_decode:"));
    return 0;
}

/*****************************************************************************/
int
hdhome_run_codec_audio_get_frame_info(void* obj, int* channels, int* format,
                                      int* bytes)
{
    return 0;
}

/*****************************************************************************/
int
hdhome_run_codec_audio_get_frame_data(void* obj, void* data, int data_bytes)
{
    return 0;
}

/*****************************************************************************/
int
hdhome_run_codec_video_create(void** obj, int codec_id)
{
    return 0;
}

/*****************************************************************************/
int
hdhome_run_codec_video_delete(void* obj)
{
    return 0;
}

/*****************************************************************************/
int
hdhome_run_codec_video_decode(void* obj, void* cdata, int cdata_bytes,
                              int* cdata_bytes_processed, int* decoded)
{
    LLOGLN(0, ("hdhome_run_codec_video_decode:"));
    return 0;
}

/*****************************************************************************/
int
hdhome_run_codec_video_get_frame_info(void* obj, int* width, int* height,
                                      int* format, int* bytes)
{
    return 0;
}

/*****************************************************************************/
int
hdhome_run_codec_video_get_frame_data(void* obj, void* data, int data_bytes)
{
    return 0;
}
