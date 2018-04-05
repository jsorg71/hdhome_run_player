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
    sample_t* samples;
    int frame_size;
    int flags;
    int sample_rate;
    int channels;
    int bit_rate;
    int pad0;
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
    self->samples = a52_samples(self->state);
    if (self->samples == NULL)
    {
        a52_free(self->state);
        free(self);
        return 5;
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

/**** the following two functions comes from a52dec */
/*****************************************************************************/
static int
blah(int32_t i)
{
    if (i > 0x43c07fff)
    {
        return 32767;
    }
    else if (i < 0x43bf8000)
    {
        return -32768;
    }
    return i - 0x43c00000;
}

/*****************************************************************************/
static void
float_to_short(float* in_float, int16_t* out_i16, int nchannels)
{
    int i;
    int j;
    int c;
    int32_t* f;

    f = (int32_t*)in_float;     /* XXX assumes IEEE float format */
    j = 0;
    nchannels *= 256;
    for (i = 0; i < 256; i++)
    {
        for (c = 0; c < nchannels; c += 256)
        {
            out_i16[j++] = blah(f[i + c]);
        }
    }
}

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
    float level;

    LLOGLN(10, ("hdhome_run_codec_audio_decode:"));
    LLOGLN(10, ("hdhome_run_codec_audio_decode: cdata_bytes %d", cdata_bytes));
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
            LLOGLN(0, ("hdhome_run_codec_audio_decode: frame_size %d "
                   "channels %d sample_rate %d",
                   self->frame_size, self->channels, self->sample_rate));
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
    *bytes = 6 * 256 * self->channels * 2;
    return 0;
}

/*****************************************************************************/
int
hdhome_run_codec_audio_get_frame_data(void* obj, void* data, int data_bytes)
{
    struct mycodec_audio* self;
    int index;
    short* out_samples;

    self = (struct mycodec_audio*)obj;
    out_samples = (short*)data;
    for (index = 0; index < 6; index++)
    {
        if (a52_block(self->state))
        {
            return 1;
        }
        float_to_short(self->samples,
                       out_samples + index * 256 * self->channels,
                       self->channels);
    }
    return 0;
}

/*****************************************************************************/
int
hdhome_run_codec_video_create(void** obj, int codec_id)
{
    struct mycodec_video* self;

    if (obj == NULL)
    {
        return 1;
    }
    if (codec_id != VIDEO_CODEC_ID_MPEG2)
    {
        return 2;
    }
    self = (struct mycodec_video*)calloc(sizeof(struct mycodec_video), 1);
    if (self == NULL)
    {
        return 3;
    }
    self->dec = mpeg2_init();
    if (self->dec == NULL)
    {
        free(self);
        return 4;
    }
    *obj = self;
    return 0;
}

/*****************************************************************************/
int
hdhome_run_codec_video_delete(void* obj)
{
    struct mycodec_video* self;

    if (obj == NULL)
    {
        return 0;
    }
    self = (struct mycodec_video*)obj;
    mpeg2_close(self->dec);
    free(self);
    return 0;
}

/*****************************************************************************/
int
hdhome_run_codec_video_decode(void* obj, void* cdata, int cdata_bytes,
                              int* cdata_bytes_processed, int* decoded)
{
    LLOGLN(10, ("hdhome_run_codec_video_decode:"));
    LLOGLN(10, ("hdhome_run_codec_video_decode: cdata_bytes %d", cdata_bytes));
    *cdata_bytes_processed = cdata_bytes;
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
