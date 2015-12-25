
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <hdhomerun.h>

#include "mpeg_ts.h"
#include "hdhome_run_x11.h"
#include "hdhome_run_pa.h"
#include "list.h"
#include "hdhome_run_avcodec.h"

#define HD_AUDIO_MSDELAY 1600

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
    struct list* frame_list;
    int frame_delay;
    int pad2;
    void* mpeg2_handle;
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
    void* ac3_handle;
    void* pa_handle;
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
};

/*****************************************************************************/
int
tmpegts_video_cb(const void* data, int data_bytes,
                 const struct tmpegts* mpegts, void* udata)
{
    int decoded;
    int rv;
    int remainder;
    int decoded_data_bytes;
    struct video_info* vi;
    uint8_t* decoded_data;
    struct ptr_size* ps;
    unsigned char* cdata;
    int cdata_bytes;
    int cdata_bytes_processed;
    int error;
    int width;
    int height;
    int format;
    int out_data_bytes;

    rv = 0;
    vi = ((struct video_audio_info*)udata)->vi;
    if (mpegts->payload_unit_start_indicator)
    {
        if (vi->frame_data_pos > 0)
        {
            ps = (struct ptr_size*) calloc(sizeof(struct ptr_size), 1);
            ps->ptr = (unsigned char*) malloc(vi->frame_data_pos);
            memcpy(ps->ptr, vi->frame_data, vi->frame_data_pos);
            ps->bytes = vi->frame_data_pos;
            list_add_item(vi->frame_list, (long)ps);
            ps = (struct ptr_size*)list_get_item(vi->frame_list, 0);
            while (vi->frame_list->count > vi->frame_delay)
            {
                list_remove_item(vi->frame_list, 0);
                /* https://en.wikipedia.org/wiki/Packetized_elementary_stream */
                remainder = ps->ptr[8];
                cdata = ps->ptr + (9 + remainder);
                cdata_bytes = ps->bytes - (9 + remainder);
                while (cdata_bytes > 0)
                {
                    error = hdhome_run_avcodec_mpeg2_decode(vi->mpeg2_handle,
                                                            cdata, cdata_bytes,
                                                            &cdata_bytes_processed,
                                                            &decoded);
                    if (error != 0)
                    {
                        printf("tmpegts_video_cb: error decoding %d\n", error);
                        break;
                    }
                    cdata += cdata_bytes_processed;
                    cdata_bytes -= cdata_bytes_processed;
                    if (decoded)
                    {
                        error = hdhome_run_avcodec_mpeg2_get_frame_info(vi->mpeg2_handle,
                                                                        &width, &height,
                                                                        &format,
                                                                        &out_data_bytes);
                        if (error == 0)
                        {
                            if (height > 720)
                            {
                                vi->frame_delay = (30 * HD_AUDIO_MSDELAY) / 1000;
                            }
                            else
                            {
                                vi->frame_delay = (60 * HD_AUDIO_MSDELAY) / 1000;
                            }
                            error = hdhome_run_x11_get_buffer(width, height, format,
                                                              (void**)(&decoded_data),
                                                              &decoded_data_bytes);
                            if (error == 0)
                            {
                                if (decoded_data_bytes >= out_data_bytes)
                                {
                                    error = hdhome_run_avcodec_mpeg2_get_frame_data(vi->mpeg2_handle,
                                                                                    decoded_data,
                                                                                    out_data_bytes);
                                    if (error == 0)
                                    {
                                        hdhome_run_x11_show_buffer(width, height, format,
                                                                   decoded_data);
                                    }
                                }
                            }
                        }
                    }
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
    int rv;
    int remainder;
    struct audio_info* ai;
    unsigned char* cdata;
    void* out_data;
    int cdata_bytes;
    int cdata_bytes_processed;
    int out_data_bytes;
    int out_data_bytes_processed;
    int error;
    int decoded;
    int channels;
    int format;

    rv = 0;
    ai = ((struct video_audio_info*)udata)->ai;
    if (mpegts->payload_unit_start_indicator)
    {
        if (ai->frame_data_pos > 0)
        {
            /* https://en.wikipedia.org/wiki/Packetized_elementary_stream */
            remainder = (unsigned char)(ai->frame_data[8]);
            cdata = (unsigned char*)(ai->frame_data + (9 + remainder));
            cdata_bytes = ai->frame_data_pos - (9 + remainder);
            while (cdata_bytes > 0)
            {
                error = hdhome_run_avcodec_ac3_decode(ai->ac3_handle,
                                                      cdata, cdata_bytes,
                                                      &cdata_bytes_processed,
                                                      &decoded);
                if (error != 0)
                {
                    printf("tmpegts_audio_cb: error decoding %d\n", error);
                    break;
                }
                cdata += cdata_bytes_processed;
                cdata_bytes -= cdata_bytes_processed;
                if (decoded)
                {
                    error =  hdhome_run_avcodec_ac3_get_frame_info(ai->ac3_handle,
                                                                   &channels,
                                                                   &format,
                                                                   &out_data_bytes);
                    if ((error == 0) && (out_data_bytes > 0))
                    {
                        out_data = malloc(out_data_bytes);
                        error =  hdhome_run_avcodec_ac3_get_frame_data(ai->ac3_handle,
                                                                       out_data,
                                                                       out_data_bytes);
                        if (error == 0)
                        {
                            if (ai->pa_handle == 0)
                            {
                                ai->pa_handle = hdhome_run_pa_init("hdhome_run_player");
                                if (ai->pa_handle != 0)
                                {
                                    hdhome_run_pa_start(ai->pa_handle, "hdhome_run_player",
                                                        HD_AUDIO_MSDELAY,
                                                        CAP_PA_FORMAT_48000_6CH_16LE);
                                }
                            }
                            hdhome_run_pa_play_non_blocking(ai->pa_handle,
                                                            out_data, out_data_bytes,
                                                            &out_data_bytes_processed);
                            if (out_data_bytes != out_data_bytes_processed)
                            {
                                printf("tmpegts_audio_cb: dropping audio data, %d bytes\n",
                                       out_data_bytes - out_data_bytes_processed);
                            }
                        }
                        free(out_data);
                    }
                }
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
    while ((data != 0) && (error == 0))
    {
        while ((error == 0) && (bytes > 3))
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

    if (hdhome_run_avcodec_init() != 0)
    {
        printf("hdhome_run_avcodec_init failed\n");
        return 1;
    }

    if (hdhome_run_x11_init() != 0)
    {
        printf("hdhome_run_x11_init failed\n");
        return 1;
    }

    error = hdhome_run_avcodec_ac3_create(&(ai.ac3_handle));
    if (error != 0)
    {
        printf("hdhome_run_avcodec_ac3_create failed error %d\n", error);
    }

    error = hdhome_run_avcodec_mpeg2_create(&(vi.mpeg2_handle));
    if (error != 0)
    {
        printf("hdhome_run_avcodec_mpeg2_create failed error %d\n", error);
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
