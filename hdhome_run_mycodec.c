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

#include <stdint.h>
#include <a52dec/a52.h>

#include <mpeg2dec/mpeg2.h>

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
    a52_state_t* state;
    int frame_size;
    int flags;
    int sample_rate;
    int channels;
    int bit_rate;
};

struct mycodec_video
{
    mpeg2dec_t* dec;
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
    struct mycodec_audio* self;

    if (obj == NULL)
    {
        return 1;
    }
    if (codec_id != AUDIO_CODEC_ID_AC3)
    {
        return 2;
    }
    self = (struct mycodec_audio*)calloc(sizeof(struct mycodec_audio), 1);
    if (self == NULL)
    {
        return 3;
    }
    self->state = a52_init(0);
    if (self->state == NULL)
    {
        free(self);
        return 4;
    }
    *obj = self;
    return 0;
}

/*****************************************************************************/
int
hdhome_run_codec_audio_delete(void* obj)
{
    struct mycodec_audio* self;

    if (obj == NULL)
    {
        return 0;
    }
    self = (struct mycodec_audio*)obj;
    a52_free(self->state);
    free(self);
    return 0;
}

static const int g_ac3_channels[8] = { 2, 1, 2, 3, 3, 4, 4, 5 };

/*****************************************************************************/
int
hdhome_run_codec_audio_decode(void* obj, void* cdata, int cdata_bytes,
                              int* cdata_bytes_processed, int* decoded)
{
    struct mycodec_audio* self;
    uint8_t* cdata8;
    int flags;
    int sample_rate;
    int bit_rate;
    int len;
    int index;
    float level;

    LLOGLN(0, ("hdhome_run_codec_audio_decode:"));
    self = (struct mycodec_audio*)obj;
    cdata8 = (uint8_t*)cdata;
    if (self->frame_size == 0)
    {
        /* lookign for header */
        sample_rate = 0;
        bit_rate = 0;
        len = a52_syncinfo(cdata8, &flags, &sample_rate, &bit_rate);
        if (len == 0)
        {
            return 1;
        }
        else
        {
            self->flags = flags;
            self->frame_size = len;
            self->sample_rate = sample_rate;
            self->channels = g_ac3_channels[self->flags & 7];
            if (self->flags & A52_LFE)
            {
                self->channels++;
            }
            self->bit_rate = bit_rate;
        }
    }
    else if (cdata_bytes >= self->frame_size)
    {
        flags = self->flags;
        if (self->channels == 1)
        {
            flags = A52_MONO;
        }
        else if (self->channels == 2)
        {
            flags = A52_STEREO;
        }
        else
        {
            flags |= A52_ADJUST_LEVEL;
        }
        level = 1;
        if (a52_frame(self->state, cdata8, &flags, &level, 384))
        {
            return 2;
        }
        for (index = 0; index < 6; index++)
        {
            if (a52_block(self->state))
            {
                return 3;
            }
            //float_to_int();
        }
        *decoded = 1;
        *cdata_bytes_processed = self->frame_size;
    }
    return 0;
}

/*****************************************************************************/
int
hdhome_run_codec_audio_get_frame_info(void* obj, int* channels, int* format,
                                      int* bytes)
{
    struct mycodec_audio* self;

    self = (struct mycodec_audio*)obj;
    *channels = self->channels;
    *format = 0;
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
