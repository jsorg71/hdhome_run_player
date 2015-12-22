
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <hdhomerun.h>

#include "mpeg_ts.h"
#include "hdhome_run_x11.h"
#include "hdhome_run_pa.h"
#include "list.h"

#include <libavcodec/avcodec.h>

struct video_info
{
    int started;
    int pad0;
    char* frame_data_alloc;
    char* frame_data;
    int frame_data_bytes;
    int frame_data_pos;
    int continuity_counter;
    int pad1;
    AVCodecContext* codec_context;
    AVCodec* codec;
    AVFrame* frame;
    struct list* frame_list;
    int frame_delay;
};

struct audio_info
{
    int started;
    int pad0;
    char* frame_data_alloc;
    char* frame_data;
    int frame_data_bytes;
    int frame_data_pos;
    int continuity_counter;
    int pad1;
    AVCodecContext* codec_context;
    AVCodec* codec;
    AVFrame* frame;
    unsigned char* left_over;
    int left_over_bytes;
};

struct video_audio_info
{
    struct video_info* vi;
    struct audio_info* ai;
};

struct ptr_size
{
    unsigned char* ptr;
    int bytes;
    unsigned int mstime;
};

static unsigned int g_mstime = 0;
static void* g_hshome_run_pa = 0;

/*****************************************************************************/
int
tmpegts_video_cb(const void* data, int data_bytes,
                 const struct tmpegts* mpegts, void* udata)
{
    int decoded;
    int len;
    int rv;
    int remainder;
    int decoded_size;
    int decoded_data_bytes;
    AVPacket pkt;
    struct video_info* vi;
    uint8_t* decoded_data;
    AVFrame* frame;
    struct ptr_size* ps;

    rv = 0;
    vi = ((struct video_audio_info*)udata)->vi;
    if (mpegts->payload_unit_start_indicator) 
    {
        //printf("video data bytes %d\n", vi->frame_data_pos);
        if (vi->frame_data_pos > 0)
        {
            ps = (struct ptr_size*) calloc(sizeof(struct ptr_size), 1);
            ps->ptr = (unsigned char*) malloc(vi->frame_data_pos);
            memcpy(ps->ptr, vi->frame_data, vi->frame_data_pos);
            ps->bytes = vi->frame_data_pos;
            ps->mstime = get_mstime();
            list_add_item(vi->frame_list, (long)ps);
            ps = (struct ptr_size*)list_get_item(vi->frame_list, 0);
            while (vi->frame_list->count > vi->frame_delay)
            {
                list_remove_item(vi->frame_list, 0);
                //hex_dump(vi->frame_data, 128);
                /* https://en.wikipedia.org/wiki/Packetized_elementary_stream */
                remainder = ps->ptr[8];
                av_init_packet(&pkt);
                pkt.data = ps->ptr + (9 + remainder);
                pkt.size = ps->bytes - (9 + remainder);
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
                    if (decoded)
                    {

                        if (vi->codec_context->height > 720)
                        {
                            vi->frame_delay = 30;
                        }
                        else
                        {
                            vi->frame_delay = 60;
                        }

                        decoded_size = avpicture_get_size(vi->codec_context->pix_fmt,
                                                          vi->codec_context->width,
                                                          vi->codec_context->height);
                        if (hdhome_run_x11_get_buffer(vi->codec_context->width,
                                                      vi->codec_context->height,
                                                      vi->codec_context->pix_fmt,
                                                      (void**)(&decoded_data),
                                                      &decoded_data_bytes) == 0)
                        {
                            if (decoded_size <= decoded_data_bytes)
                            {
                                frame = avcodec_alloc_frame();
                                avpicture_fill((AVPicture*)frame, decoded_data,
                                               vi->codec_context->pix_fmt,
                                               vi->codec_context->width,
                                               vi->codec_context->height);
                                av_picture_copy((AVPicture*)frame,
                                                (AVPicture*)(vi->frame),
                                                vi->codec_context->pix_fmt,
                                                vi->codec_context->width,
                                                vi->codec_context->height);
                                av_free(frame);
                                hdhome_run_x11_show_buffer(vi->codec_context->width,
                                                           vi->codec_context->height,
                                                           vi->codec_context->pix_fmt,
                                                           decoded_data);
                            }
                        }
                    }
                    pkt.size -= len; 
                    pkt.data += len;
                }
                free(ps->ptr);
                free(ps);
                ps = (struct ptr_size*)list_get_item(vi->frame_list, 0);
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
        vi->continuity_counter = mpegts->continuity_counter;
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
    int remainder;
    int frame_size;
    int play_data_bytes;
    int play_data_bytes1;
    unsigned char* play_data;
    AVPacket pkt;
    struct audio_info* ai;

    rv = 0;
    ai = ((struct video_audio_info*)udata)->ai;
    if (mpegts->payload_unit_start_indicator)
    {
        g_mstime = get_mstime();
        //printf("audio data bytes %d\n", ai->frame_data_pos);
        if (ai->frame_data_pos > 0)
        {
            //hex_dump(ai->frame_data, 128);
            /* https://en.wikipedia.org/wiki/Packetized_elementary_stream */
            remainder = (unsigned char)(ai->frame_data[8]);
            av_init_packet(&pkt);
            pkt.data = (unsigned char*)(ai->frame_data + (9 + remainder));
            pkt.size = ai->frame_data_pos - (9 + remainder);
            while (pkt.size > 0)
            {
                decoded = 0;
                len = avcodec_decode_audio4(ai->codec_context,
                                            ai->frame,
                                            &decoded, &pkt);
                if (len < 0)
                {
                    printf("tmpegts_audio_cb: error decoding %d\n", len);
                    rv = 1;
                    break;
                }
                if (decoded)
                {
                    frame_size = av_samples_get_buffer_size(NULL,
                                                            ai->codec_context->channels,
                                                            ai->frame->nb_samples,
                                                            ai->codec_context->sample_fmt,
                                                            1);
                    if (frame_size > 0)
                    {
                        play_data_bytes = ai->left_over_bytes + frame_size;
                        play_data = (unsigned char*)malloc(play_data_bytes);
                        memcpy(play_data, ai->left_over, ai->left_over_bytes);
                        memcpy(play_data + ai->left_over_bytes, ai->frame->data[0], frame_size);
                        play_data_bytes1 = play_data_bytes;
                        hdhome_run_pa_play_non_blocking(g_hshome_run_pa,
                                                        play_data,
                                                        &play_data_bytes);
                        if (play_data_bytes != 0)
                        {
                            if (play_data_bytes > frame_size)
                            {
                                play_data_bytes = frame_size;
                            }
                            free(ai->left_over);
                            ai->left_over = (unsigned char*)malloc(play_data_bytes);
                            memcpy(ai->left_over, play_data + play_data_bytes1 - play_data_bytes, play_data_bytes);
                            ai->left_over_bytes = play_data_bytes;
                        }
                        else
                        {
                            ai->left_over_bytes = 0;
                        }
                        free(play_data);
                    }
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
        ai->continuity_counter = mpegts->continuity_counter;
    }
    ai->continuity_counter++;
    if (ai->continuity_counter > 15)
    {
        ai->continuity_counter = 0;
    }
    return rv;
}

struct mlcb_info
{
    struct hdhomerun_device_t* hdhr;
    struct tmpegts_cb* cb;
    struct video_audio_info* vai;
};

/*****************************************************************************/
int
main_loop_callback(int sck, void* udata)
{
    size_t bytes;
    uint8_t* data;
    int error;
    int lbytes;
    struct mlcb_info* mlcbi;

    mlcbi = (struct mlcb_info*)udata;
    error = 0;
    bytes = 32 * 1024;
    data = hdhomerun_device_stream_recv(mlcbi->hdhr, bytes, &bytes);
    while (data != 0 && error == 0)
    {
        while (error == 0 && bytes > 3)
        {
            lbytes = bytes;
            if (lbytes > 188)
            {
                lbytes = 188;
            }
            error = process_mpeg_ts_packet(data, lbytes, mlcbi->cb, mlcbi->vai);
            data += lbytes; 
            bytes -= lbytes;
        }
        if (error == 0)
        {
            bytes = 32 * 1024; 
            data = hdhomerun_device_stream_recv(mlcbi->hdhr, bytes, &bytes);
        }
    }
    return error;
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
    struct tmpegts_cb cb;
    struct video_info vi;
    struct audio_info ai;
    struct video_audio_info vai;

    struct mlcb_info mlcbi;

    memset(&vi, 0, sizeof(vi));
    vi.frame_data_bytes = 1024 * 1024;
    vi.frame_data_alloc = (char*)malloc(vi.frame_data_bytes + 16);
    vi.frame_data = (char*)((((long)vi.frame_data_alloc) + 15) & ~15);
    vi.continuity_counter = -1;
    vi.frame_list = list_create();

    memset(&ai, 0, sizeof(ai));
    ai.frame_data_bytes = 1024 * 1024;
    ai.frame_data_alloc = (char*)malloc(ai.frame_data_bytes + 16);
    ai.frame_data = (char*)((((long)ai.frame_data_alloc) + 15) & ~15);
    ai.continuity_counter = -1;

    memset(&cb, 0, sizeof(cb));
    cb.pids[0] = 0x31;
    cb.pids[1] = 0x34;
    cb.procs[0] = tmpegts_video_cb;
    cb.procs[1] = tmpegts_audio_cb;

    memset(&vai, 0, sizeof(vai));
    vai.vi = &vi;
    vai.ai = &ai;

    if (hdhome_run_x11_init() != 0)
    {
        printf("hdhome_run_x11_init failed\n");
        return 1;
    }

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
    error = avcodec_open2(vi.codec_context, vi.codec, NULL);
    printf("video avcodec_open2 error %d\n", error);
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

    g_hshome_run_pa = hdhome_run_pa_init("hdhome_run_player");
    if (g_hshome_run_pa != 0)
    {
        hdhome_run_pa_start(g_hshome_run_pa, "hdhome_run_player", 1000,
        //hdhome_run_pa_start(g_hshome_run_pa, "hdhome_run_player", 0,
                            CAP_PA_FORMAT_48000_6CH_16LE);
    }

    //if (ai.codec->capabilities & CODEC_CAP_TRUNCATED)
    //{
    //    ai.codec_context->flags |= CODEC_FLAG_TRUNCATED;
    //}

    //ai.codec_context->channels = 6;
    //ai.codec_context->sample_rate = 48000;
    //ai.codec_context->bit_rate = 448000;
    //ai.codec_context->block_align = 0;
    //ai.codec_context->codec_id = CODEC_ID_AC3;
    //ai.codec_context->codec_type = 1;
    //ai.codec_context->bits_per_raw_sample = 16;
    //printf("FF_INPUT_BUFFER_PADDING_SIZE %d\n", FF_INPUT_BUFFER_PADDING_SIZE);

    error = avcodec_open2(ai.codec_context, ai.codec, NULL);
    printf("audio avcodec_open2 error %d\n", error);
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
            memset(&mlcbi, 0, sizeof(mlcbi));
            mlcbi.hdhr = hdhr;
            mlcbi.cb = &cb;
            mlcbi.vai = &vai;
            hdhome_run_x11_main_loop(hdhr_sck, main_loop_callback, &mlcbi);
        }
        hdhomerun_device_destroy(hdhr);
    }
    return 0;
}
