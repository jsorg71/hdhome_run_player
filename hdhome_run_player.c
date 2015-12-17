
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <hdhomerun.h>

#include "mpeg_ts.h"

#include <libavcodec/avcodec.h>

struct video_info
{
    int started;
    int pad0;
    char* frame_data;
    int frame_data_bytes;
    int frame_data_pos;
    int continuity_counter;
    int pad1;
    AVCodecContext* codec_context;
    AVCodec* codec;
    AVFrame* frame;
};

struct audio_info
{
    int started;
    int pad0;
    char* frame_data;
    int frame_data_bytes;
    int frame_data_pos;
    int continuity_counter;
    int pad1;
    AVCodecContext* codec_context;
    AVCodec* codec;
    AVFrame* frame;
};

struct video_audio_info
{
    struct video_info* vi;
    struct audio_info* ai;
};

/*****************************************************************************/
int
tmpegts_video_cb(const void* data, int data_bytes,
                 const struct tmpegts* mpegts, void* udata)
{
    int decoded;
    int len;
    int rv;
    AVPacket pkt;
    struct video_info* vi;

    rv = 0;
    vi = ((struct video_audio_info*)udata)->vi;
    if (mpegts->payload_unit_start_indicator)
    {
        //printf("video data bytes %d\n", vi->frame_data_pos);
        if (vi->frame_data_pos > 0)
        {
            av_init_packet(&pkt);
            pkt.data = (unsigned char*) (vi->frame_data);
            pkt.size = vi->frame_data_pos;
            while (pkt.size > 0)
            {
                decoded = 0;
                len = avcodec_decode_video2(vi->codec_context,
                                            vi->frame,
                                            &decoded, &pkt);
                if (len < 0)
                {
                    printf("tmpegts_video_cb: error decoding %d\n", len);
                    rv = 1;
                    break;
                }
                pkt.size -= len;
                pkt.data += len;
            }
            vi->frame_data_pos = 0;
        }
        vi->started = 1;
    }

    if (vi->started)
    {
        memcpy(vi->frame_data + vi->frame_data_pos, data, data_bytes);
        vi->frame_data_pos += data_bytes;
    }

    if (vi->continuity_counter == -1)
    {
        vi->continuity_counter = mpegts->continuity_counter;
    }
    if (vi->continuity_counter != mpegts->continuity_counter)
    {
        printf("%d %d\n", vi->continuity_counter, mpegts->continuity_counter);
    }
    vi->continuity_counter++;
    if (vi->continuity_counter > 15)
    {
        vi->continuity_counter = 0;
    }
    return rv;
}

/*****************************************************************************/
int
tmpegts_audio_cb(const void* data, int data_bytes,
                 const struct tmpegts* mpegts, void* udata)
{
    int decoded;
    int len;
    int rv;
    AVPacket pkt;
    struct audio_info* ai;

    rv = 0;
    ai = ((struct video_audio_info*)udata)->ai;
    if (mpegts->payload_unit_start_indicator)
    {
        printf("audio data bytes %d\n", ai->frame_data_pos);
        if (ai->frame_data_pos > 0)
        {
            av_init_packet(&pkt);
            pkt.data = (unsigned char*) (ai->frame_data);
            pkt.size = ai->frame_data_pos;
            while (pkt.size > 0)
            {
                decoded = 0;
                //len = pkt.size;
                len = avcodec_decode_audio4(ai->codec_context,
                                            ai->frame,
                                            &decoded, &pkt);
                if (len < 0)
                {
                    printf("tmpegts_audio_cb: error decoding %d\n", len);
                    rv = 1;
                    break;
                }
                pkt.size -= len;
                pkt.data += len;
            }
            ai->frame_data_pos = 0;
        }
        ai->started = 1;
    }
    if (ai->started)
    {
        memcpy(ai->frame_data + ai->frame_data_pos, data, data_bytes);
        ai->frame_data_pos += data_bytes;
    }

    if (ai->continuity_counter == -1)
    {
        ai->continuity_counter = mpegts->continuity_counter;
    }
    if (ai->continuity_counter != mpegts->continuity_counter)
    {
        printf("%d %d\n", ai->continuity_counter, mpegts->continuity_counter);
    }
    ai->continuity_counter++;
    if (ai->continuity_counter > 15)
    {
        ai->continuity_counter = 0;
    }
    return rv;
}

/*****************************************************************************/
int
main(int argc, char** argv)
{
    struct hdhomerun_device_t* hdhr;
    struct hdhomerun_video_sock_t* hdhr_vsck;
    hdhomerun_sock_t hdhr_sck;
    const char* dev_name;
    int error;
    size_t bytes;
    int lbytes;
    uint8_t* data;
    fd_set rfds;
    struct tmpegts_cb cb;
    struct video_info vi;
    struct audio_info ai;
    struct video_audio_info vai;

    memset(&vi, 0, sizeof(vi));
    vi.frame_data_bytes = 1024 * 1024;
    vi.frame_data = (char*)malloc(vi.frame_data_bytes);
    vi.continuity_counter = -1;

    memset(&ai, 0, sizeof(ai));
    ai.frame_data_bytes = 1024 * 1024;
    ai.frame_data = (char*)malloc(ai.frame_data_bytes);
    ai.continuity_counter = -1;

    memset(&cb, 0, sizeof(cb));
    cb.pids[0] = 0x31;
    cb.pids[1] = 0x35;
    cb.procs[0] = tmpegts_video_cb;
    cb.procs[1] = tmpegts_audio_cb;

    memset(&vai, 0, sizeof(vai));
    vai.vi = &vi;
    vai.ai = &ai;

    avcodec_register_all();
    vi.codec_context = avcodec_alloc_context3(NULL);
    if (vi.codec_context == 0)
    {
        printf("avcodec_alloc_context3 failed\n");
        return 0;
    }
    vi.codec = avcodec_find_decoder(CODEC_ID_MPEG2VIDEO);
    if (vi.codec == 0)
    {
        printf("avcodec_find_decoder failed\n");
        return 0;
    }
    avcodec_open2(vi.codec_context, vi.codec, NULL); 
    vi.frame = avcodec_alloc_frame();
    if (vi.frame == 0)
    {
        printf("avcodec_alloc_frame failed\n");
        return 0;
    }

    ai.codec_context = avcodec_alloc_context3(NULL);
    if (ai.codec_context == 0)
    {
        printf("avcodec_alloc_context3 failed\n");
        return 0;
    }

    ai.codec = avcodec_find_decoder(CODEC_ID_AC3);
    if (ai.codec == 0)
    {
        printf("avcodec_find_decoder failed\n");
        return 0;
    }

    ai.codec_context->channels = 2;
    ai.codec_context->sample_rate = 48000;
    ai.codec_context->bit_rate = 192000;
    ai.codec_context->block_align = 0;
    ai.codec_context->codec_id = CODEC_ID_AC3;
    ai.codec_context->codec_type = 1;
    ai.codec_context->bits_per_raw_sample = 16;

    avcodec_open2(ai.codec_context, ai.codec, NULL);
    ai.frame = avcodec_alloc_frame();
    if (ai.frame == 0)
    {
        printf("avcodec_alloc_frame failed\n");
        return 0;
    }

    hdhr = hdhomerun_device_create(HDHOMERUN_DEVICE_ID_WILDCARD, 0, 0, 0); 
    if (hdhr != 0)
    {
        hdhr_vsck = hdhomerun_device_get_video_sock(hdhr);
        hdhr_sck = hdhomerun_video_get_sock(hdhr_vsck);
        dev_name = hdhomerun_device_get_name(hdhr);
        printf("opened device %s\n", dev_name);
        error = hdhomerun_device_stream_start(hdhr);
        printf("hdhomerun_device_stream_start %d\n", error);
        if (error == 1)
        {
            while (1)
            {
                bytes = 32 * 1024;
                data = hdhomerun_device_stream_recv(hdhr, bytes, &bytes);
                if (data != 0)
                {
                    error = 0;
                    while (error == 0 && bytes > 3)
                    {
                        lbytes = bytes;
                        if (lbytes > 188)
                        {
                            lbytes = 188;
                        }
                        error = process_mpeg_ts_packet(data, lbytes, &cb, &vai);
                        data += lbytes;
                        bytes -= lbytes;
                    }
                    if (error != 0)
                    {
                        break;
                    }
                }
                else
                {
                    FD_ZERO(&rfds);
                    FD_SET(((unsigned int)hdhr_sck), &rfds);
                    error = select(hdhr_sck + 1, &rfds, 0, 0, 0);
                    if (error < 0)
                    {
                        break;
                    }
                }
            }
        }
        hdhomerun_device_destroy(hdhr);
    }
    return 0;
}
