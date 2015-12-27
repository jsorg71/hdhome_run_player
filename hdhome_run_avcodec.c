/**
 * avcodec / ffmpeg calls
 *
 * Copyright 2015 Jay Sorg <jay.sorg@gmail.com>
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

#include <libavcodec/avcodec.h>
#include <libavutil/mem.h>

#ifndef LIBAVCODEC_VERSION_MAJOR
#warning LIBAVCODEC_VERSION_MAJOR not defined
#endif

// LIBAVCODEC_VERSION_MAJOR 52 ubuntu 10.04
// LIBAVCODEC_VERSION_MAJOR 52 debian 6
// LIBAVCODEC_VERSION_MAJOR 53 debian 7
// LIBAVCODEC_VERSION_MAJOR 54 ubuntu 14.04
// LIBAVCODEC_VERSION_MAJOR 56 debian 8
// LIBAVCODEC_VERSION_MAJOR 56 ubuntu 15.10

#if LIBAVCODEC_VERSION_MAJOR > 55
#define CODEC_ID_AC3                AV_CODEC_ID_AC3
#define CODEC_ID_MPEG2VIDEO         AV_CODEC_ID_MPEG2VIDEO
#define AVCODEC_ALLOC_FRAME         av_frame_alloc
#define AVCODEC_FREE_FRAME(_frame)  av_frame_free(_frame);
#else
#define AVCODEC_ALLOC_FRAME         avcodec_alloc_frame
#define AVCODEC_FREE_FRAME(_frame)  av_free(*(_frame));
#endif

struct avcodec_ac3
{
    AVCodecContext* codec_context;
    AVCodec* codec;
    AVFrame* frame;
};

struct avcodec_mpeg2
{
    AVCodecContext* codec_context;
    AVCodec* codec;
    AVFrame* frame;
};

int
hdhome_run_avcodec_init(void)
{
    avcodec_register_all();
    return 0;
}

int
hdhome_run_avcodec_ac3_create(void** obj)
{
    struct avcodec_ac3* self;
    int error;

    if (obj == NULL)
    {
        return 1;
    }
    self = (struct avcodec_ac3*)malloc(sizeof(struct avcodec_ac3));
    if (self == NULL)
    {
        return 2;
    }
    memset(self, 0, sizeof(struct avcodec_ac3));
    self->codec_context = avcodec_alloc_context3(NULL);
    if (self->codec_context == NULL)
    {
        free(self);
        return 3;
    }
    self->codec = avcodec_find_decoder(CODEC_ID_AC3);
    if (self->codec == NULL)
    {
        avcodec_close(self->codec_context);
        free(self);
        return 4;
    }
    error = avcodec_open2(self->codec_context, self->codec, NULL);
    if (error != 0)
    {
        avcodec_close(self->codec_context);
        free(self);
        return 5;
    }
    self->frame = AVCODEC_ALLOC_FRAME();
    *obj = self;
    return 0;
}

int
hdhome_run_avcodec_ac3_delete(void* obj)
{
    struct avcodec_ac3* self;

    self = (struct avcodec_ac3*)obj;
    if (self == NULL)
    {
        return 0;
    }
    avcodec_close(self->codec_context);
    AVCODEC_FREE_FRAME(&(self->frame));
    free(self);
    return 0;
}

int
hdhome_run_avcodec_ac3_decode(void* obj, void* cdata, int cdata_bytes,
                              int* cdata_bytes_processed, int* decoded)
{
    struct avcodec_ac3* self;
    AVPacket pkt;
    int len;
    int bytes_processed;

    self = (struct avcodec_ac3*)obj;
    if (self == NULL)
    {
        return 1;
    }
    *cdata_bytes_processed = 0;
    *decoded = 0;
    bytes_processed = 0;
    av_init_packet(&pkt);
    pkt.data = cdata;
    pkt.size = cdata_bytes;
    while (pkt.size > 0)
    {
        len = avcodec_decode_audio4(self->codec_context,
                                    self->frame,
                                    decoded, &pkt);
        if (len < 0)
        {
            return 1;
        }
        pkt.size -= len;
        pkt.data += len;
        bytes_processed += len;
        if (*decoded)
        {
            *cdata_bytes_processed = bytes_processed;
            return 0;
        }
    }
    *cdata_bytes_processed = bytes_processed;
    return 0;
}

int
hdhome_run_avcodec_ac3_get_frame_info(void* obj, int* channels, int* format,
                                      int* bytes)
{
    struct avcodec_ac3* self;
    int frame_size;

    self = (struct avcodec_ac3*)obj;
    if (self == NULL)
    {
        return 1;
    }
    if ((self->frame->format != AV_SAMPLE_FMT_S16) &&
        (self->frame->format != AV_SAMPLE_FMT_FLTP))
    {
        return 2;
    }
    frame_size = av_samples_get_buffer_size(NULL, 
                                            self->codec_context->channels,
                                            self->frame->nb_samples,
                                            AV_SAMPLE_FMT_S16,
                                            1);
    *channels = self->codec_context->channels;
    *format = AV_SAMPLE_FMT_S16;
    *bytes = frame_size;
    return 0;
}

int
hdhome_run_avcodec_ac3_get_frame_data(void* obj, void* data, int data_bytes)
{
    struct avcodec_ac3* self;

    self = (struct avcodec_ac3*)obj;
    if (self == NULL)
    {
        return 1;
    }
    if (self->frame->format == AV_SAMPLE_FMT_S16)
    {
        memcpy(data, self->frame->data[0], data_bytes);
    }
    else if (self->frame->format == AV_SAMPLE_FMT_FLTP)
    {
        /* convert float to sint16 */
#if LIBAVCODEC_VERSION_MAJOR > 53
        float* src[8];
        short* dst;
        int index;

        src[0] = (float*)(self->frame->data[0]);
        src[1] = (float*)(self->frame->data[1]);
        src[2] = (float*)(self->frame->data[2]);
        src[3] = (float*)(self->frame->data[3]);
        src[4] = (float*)(self->frame->data[4]);
        src[5] = (float*)(self->frame->data[5]);
        dst = (short*)data;
        for (index = 0; index < self->frame->nb_samples; index++)
        {
            if (data_bytes < 6)
            {
                break;
            }
            dst[0] = src[0][index] * 32768; 
            dst[1] = src[1][index] * 32768;
            dst[2] = src[2][index] * 32768;
            dst[3] = src[3][index] * 32768;
            dst[4] = src[4][index] * 32768;
            dst[5] = src[5][index] * 32768;
            dst += 6;
            data_bytes -= 6;
        }
#else
        /* self->frame->data only has 4 elements */
        return 1;
#endif
    }
    else
    {
        return 1;
    }
    return 0;
}

int
hdhome_run_avcodec_mpeg2_create(void** obj)
{
    struct avcodec_mpeg2* self;
    int error;

    if (obj == NULL)
    {
        return 1;
    }
    self = (struct avcodec_mpeg2*)malloc(sizeof(struct avcodec_mpeg2));
    if (self == NULL)
    {
        return 2;
    }
    memset(self, 0, sizeof(struct avcodec_mpeg2));
    self->codec_context = avcodec_alloc_context3(NULL);
    if (self->codec_context == NULL)
    {
        free(self);
        return 3;
    }
    self->codec = avcodec_find_decoder(CODEC_ID_MPEG2VIDEO);
    if (self->codec == NULL)
    {
        avcodec_close(self->codec_context);
        free(self);
        return 4;
    }
    error = avcodec_open2(self->codec_context, self->codec, NULL);
    if (error != 0)
    {
        avcodec_close(self->codec_context);
        free(self);
        return 5;
    }
    self->frame = AVCODEC_ALLOC_FRAME();
    *obj = self;
    return 0;
}

int
hdhome_run_avcodec_mpeg2_delete(void* obj)
{
    struct avcodec_mpeg2* self;

    self = (struct avcodec_mpeg2*)obj;
    if (self == NULL)
    {
        return 0;
    }
    avcodec_close(self->codec_context);
    AVCODEC_FREE_FRAME(&(self->frame));
    free(self);
    return 0;
}

int
hdhome_run_avcodec_mpeg2_decode(void* obj, void* cdata, int cdata_bytes,
                                int* cdata_bytes_processed, int* decoded)
{
    struct avcodec_mpeg2* self;
    AVPacket pkt;
    int len;
    int bytes_processed;

    self = (struct avcodec_mpeg2*)obj;
    if (self == NULL)
    {
        return 1;
    }
    *cdata_bytes_processed = 0;
    *decoded = 0;
    bytes_processed = 0;
    av_init_packet(&pkt);
    pkt.data = cdata;
    pkt.size = cdata_bytes;
    while (pkt.size > 0)
    {
        len = avcodec_decode_video2(self->codec_context,
                                    self->frame,
                                    decoded, &pkt);
        if (len < 0)
        {
            return 1;
        }
        pkt.size -= len;
        pkt.data += len;
        bytes_processed += len;
        if (*decoded)
        {
            *cdata_bytes_processed = bytes_processed;
            return 0;
        }
    }
    *cdata_bytes_processed = bytes_processed;
    return 0;
}

int
hdhome_run_avcodec_mpeg2_get_frame_info(void* obj, int* width, int* height,
                                        int* format, int* bytes)
{
    struct avcodec_mpeg2* self;
    int frame_size;

    self = (struct avcodec_mpeg2*)obj;
    if (self == NULL)
    {
        return 1;
    }
    frame_size = avpicture_get_size(self->frame->format,
                                    self->frame->width,
                                    self->frame->height);
    *width = self->codec_context->width;
    *height = self->codec_context->height;
    *format = self->frame->format;
    *bytes = frame_size;
    return 0;
}

int
hdhome_run_avcodec_mpeg2_get_frame_data(void* obj, void* data, int data_bytes)
{
    struct avcodec_mpeg2* self;
    AVFrame* frame;

    self = (struct avcodec_mpeg2*)obj;
    if (self == NULL)
    {
        return 1;
    }
    frame = AVCODEC_ALLOC_FRAME();
    avpicture_fill((AVPicture*)frame, data,
                   self->frame->format,
                   self->frame->width,
                   self->frame->height);
    av_picture_copy((AVPicture*)frame,
                    (AVPicture*)(self->frame),
                    self->frame->format,
                    self->frame->width,
                    self->frame->height);
    AVCODEC_FREE_FRAME(&frame);
    return 0;
}

