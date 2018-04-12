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
    int cdata_bytes;;
    uint8_t* cdata;
};

struct mycodec_video
{
    mpeg2dec_t* dec;
    int width;
    int height;
    int got_frame;
    int pad0;
    void* ybuf;
    void* ubuf;
    void* vbuf;
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
    free(self->cdata);
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
    int flags;
    int sample_rate;
    int bit_rate;
    int len;
    float level;

    LLOGLN(10, ("hdhome_run_codec_audio_decode:"));
    LLOGLN(10, ("hdhome_run_codec_audio_decode: cdata_bytes %d", cdata_bytes));
    *decoded = 0;
    *cdata_bytes_processed = 0;
    self = (struct mycodec_audio*)obj;
    if (self->frame_size == 0)
    {
        if (cdata_bytes >= 7)
        {
            /* lookign for header */
            flags = 0;
            sample_rate = 0;
            bit_rate = 0;
            len = a52_syncinfo((uint8_t*)cdata, &flags, &sample_rate, &bit_rate);
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
                self->cdata = (uint8_t*)malloc(len);
                if (self->cdata == NULL)
                {
                    return 2;
                }
                self->cdata_bytes = 0;
                return 0;
            }
        }
        return 3;
    }
    len = self->frame_size - self->cdata_bytes;
    if (len > cdata_bytes)
    {
        len = cdata_bytes;
    }
    if (len < 1)
    {
        return 4;
    }
    memcpy(self->cdata + self->cdata_bytes, cdata, len);
    self->cdata_bytes += len;
    *cdata_bytes_processed = len;
    if (self->cdata_bytes >= self->frame_size)
    {
        self->cdata_bytes = 0;
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
        if (a52_frame(self->state, self->cdata, &flags, &level, 384))
        {
            return 5;
        }
        *decoded = 1;
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
    free(self->ybuf);
    free(self->ubuf);
    free(self->vbuf);
    mpeg2_close(self->dec);
    free(self);
    return 0;
}

/*****************************************************************************/
static int
decode_mpeg2_frame(struct mycodec_video* self, const mpeg2_info_t* info)
{
    int ybytes;
    int uvbytes;
    int lwidth;
    int lheight;

    if ((info->sequence != NULL) && (info->display_fbuf != NULL))
    {
        LLOGLN(10, ("decode_mpeg2: width %d height %d frame_period %d",
               info->sequence->width, info->sequence->height,
               info->sequence->frame_period));
        lwidth = info->sequence->width;
        lheight = info->sequence->height;
        if ((lwidth < 16) || (lwidth > 4096) ||
            (lheight < 16) || (lheight > 4096))
        {
            return 1;
        }
        ybytes = lwidth * lheight;
        uvbytes = ybytes / 4;
        if ((self->width != lwidth) || (self->height != lheight))
        {
            self->width = lwidth;
            self->height = lheight;
            free(self->ybuf);
            free(self->ubuf);
            free(self->vbuf);
            self->ybuf = malloc(ybytes);
            self->ubuf = malloc(uvbytes);
            self->vbuf = malloc(uvbytes);
            if ((self->ybuf == NULL) || (self->ubuf == NULL) ||
                (self->vbuf == NULL))
            {
                self->width = 0;
                self->height = 0;
                return 2;
            }
        }
        memcpy(self->ybuf, info->display_fbuf->buf[0], ybytes);
        memcpy(self->ubuf, info->display_fbuf->buf[1], uvbytes);
        memcpy(self->vbuf, info->display_fbuf->buf[2], uvbytes);
        self->got_frame = 1;
    }
    return 0;
}

/*****************************************************************************/
static int
decode_mpeg2(struct mycodec_video* self, uint8_t* start, uint8_t* end)
{
    const mpeg2_info_t* info;
    mpeg2_state_t state;
    int error;

    LLOGLN(10, ("decode_mpeg2:"));
    error = 0;
    mpeg2_buffer(self->dec, start, end);
    info = mpeg2_info(self->dec);
    while (error == 0)
    {
        state = mpeg2_parse(self->dec);
        LLOGLN(10, ("decode_mpeg2: state %d", state));
        switch (state)
        {
            case STATE_BUFFER:
                LLOGLN(10, ("decode_mpeg2: STATE_BUFFER"));
                return 0;
            case STATE_SEQUENCE:
                LLOGLN(10, ("decode_mpeg2: STATE_SEQUENCE"));
                break;
            case STATE_PICTURE:
                LLOGLN(10, ("decode_mpeg2: STATE_PICTURE"));
                break;
            case STATE_SLICE:
            case STATE_END:
            case STATE_INVALID_END:
                error = decode_mpeg2_frame(self, info);
                break;
            default:
                break;
        }
    }
    return error;
}

/*****************************************************************************/
int
hdhome_run_codec_video_decode(void* obj, void* cdata, int cdata_bytes,
                              int* cdata_bytes_processed, int* decoded)
{
    struct mycodec_video* self;
    uint8_t* start;
    uint8_t* end;
    int error;

    LLOGLN(10, ("hdhome_run_codec_video_decode:"));
    LLOGLN(10, ("hdhome_run_codec_video_decode: cdata_bytes %d", cdata_bytes));
    self = (struct mycodec_video*)obj;
    start = (uint8_t*)cdata;
    end = start + cdata_bytes;
    error = decode_mpeg2(self, start, end);
    if (error == 0)
    {
        *decoded = self->got_frame;
        *cdata_bytes_processed = cdata_bytes;
    }
    self->got_frame = 0;
    return error;
}

/*****************************************************************************/
int
hdhome_run_codec_video_get_frame_info(void* obj, int* width, int* height,
                                      int* format, int* bytes)
{
    struct mycodec_video* self;
    int lwidth;
    int lheight;

    self = (struct mycodec_video*)obj;
    lwidth = self->width;
    lheight = self->height;
    if ((lwidth < 1) || (lheight < 1))
    {
        return 1;
    }
    LLOGLN(10,("hdhome_run_codec_video_get_frame_info: width %d height %d",
           lwidth, lheight));
    *width = lwidth;
    *height = lheight;
    *format = 0; /* I420 */
    *bytes = lwidth * lheight * 3 / 2;
    return 0;
}

/*****************************************************************************/
int
hdhome_run_codec_video_get_frame_data(void* obj, void* data, int data_bytes)
{
    struct mycodec_video* self;
    uint8_t* dst8;
    int lwidth;
    int lheight;
    int bytes;

    self = (struct mycodec_video*)obj;
    lwidth = self->width;
    lheight = self->height;
    dst8 = (uint8_t*)data;
    bytes = lwidth * lheight;
    if (bytes > data_bytes)
    {
        bytes = data_bytes;
    }
    memcpy(dst8, self->ybuf, bytes);
    dst8 += bytes;
    data_bytes -= bytes;
    bytes = lwidth * lheight / 4;
    if (bytes > data_bytes)
    {
        bytes = data_bytes;
    }
    memcpy(dst8, self->ubuf, bytes);
    dst8 += bytes;
    data_bytes -= bytes;
    bytes = lwidth * lheight / 4;
    if (bytes > data_bytes)
    {
        bytes = data_bytes;
    }
    memcpy(dst8, self->vbuf, bytes);
    return 0;
}
