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
    self = (struct avcodec_ac3 *)malloc(sizeof(struct avcodec_ac3)); 
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
    self->frame = avcodec_alloc_frame();
    //self->frame = av_frame_alloc();
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
    //av_frame_free(&self->frame);
    av_free(self->frame);
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
    frame_size = av_samples_get_buffer_size(NULL,
                                            self->codec_context->channels,
                                            self->frame->nb_samples,
                                            self->frame->format,
                                            1);
    *channels = self->codec_context->channels;
    *format = self->frame->format;
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
    memcpy(data, self->frame->data[0], data_bytes);
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
    self = (struct avcodec_mpeg2 *)malloc(sizeof(struct avcodec_mpeg2)); 
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
    self->frame = avcodec_alloc_frame();
    //self->frame = av_frame_alloc();
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
    //av_frame_free(&self->frame);
    av_free(self->frame);
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
    frame = avcodec_alloc_frame();
    //frame = av_frame_alloc();
    avpicture_fill((AVPicture*)frame, data,
                   self->frame->format,
                   self->frame->width,
                   self->frame->height);
    av_picture_copy((AVPicture*)frame,
                    (AVPicture*)(self->frame),
                    self->frame->format,
                    self->frame->width,
                    self->frame->height);
    av_free(frame);
    return 0;
}

