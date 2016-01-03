/**
 * hdhome_run_player
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
#include <unistd.h>
#include <pthread.h>

#include <hdhomerun.h>

#include "mpeg_ts.h"
#include "hdhome_run_x11.h"
#include "hdhome_run_pa.h"
#include "list.h"
#include "hdhome_run_avcodec.h"

#define HD_AUDIO_MSDELAY 1000
#define MSTIME_HISTORY 4000

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
    int last_mstime;
    int frame_lo;
    int frame_hi;
    int time_flags;
    int time_point;
    int time_count;
    int fps;
    int pad3;
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
    struct list* audio_delay_list;
};

struct video_audio_info
{
    struct video_info* vi;
    struct audio_info* ai;
    int audio_latency;
    int pad0;
    int main_to_worker_video_pipe[2];
    int worker_to_main_video_pipe[2];
    int main_to_worker_audio_pipe[2];
    int term_pipe[2];
    pthread_mutex_t mutex;
    struct list* main_to_worker_video_list;
    struct list* worker_to_main_video_list;
    struct list* main_to_worker_audio_list;
};

struct mlcb_info
{
    struct hdhomerun_device_t* hdhr;
    struct tmpegts_cb* cb;
    struct video_audio_info* vai;
};

struct main_to_worker_video_item
{
    unsigned char* data;
    int data_bytes;
    int mstime;
};

struct worker_to_main_video_item
{
    unsigned char* data;
    int data_bytes;
    int format;
    int width;
    int height;
};

struct main_to_worker_audio_item
{
    unsigned char* data;
    int data_bytes;
    int mstime_in;
    int mstime_queued;
    int pad0;
};

struct average_item
{
    int diff;
    int mstime;
};

/*****************************************************************************/
/* returns boolean */
static int
pipe_is_set(int pipe[])
{
    fd_set rfds;
    struct timeval time;
    int rv;

    memset(&time, 0, sizeof(time));
    FD_ZERO(&rfds);
    FD_SET(pipe[0], &rfds);
    rv = select(pipe[0] + 1, &rfds, 0, 0, &time);
    if (rv == 1)
    {
        return 1;
    }
    return 0;
}

/*****************************************************************************/
static int
pipe_clear(int pipe[])
{
    char buf[4];

    while (pipe_is_set(pipe))
    {
        if (read(pipe[0], buf, 4) != 4)
        {
            return 1;
        }
    }
    return 0;
}

/*****************************************************************************/
static int
pipe_set(int pipe[])
{
    if (pipe_is_set(pipe))
    {
        return 0;
    }
    if (write(pipe[1], "sig", 4) != 4)
    {
        return 1;
    }
    return 0;
}

/*****************************************************************************/
static int
audio_delay_list_get_average(struct list* alist, int now, int diff)
{
    int index;
    int count;
    int acc;
    int average;
    struct average_item* item;

    item = (struct average_item*)calloc(sizeof(struct average_item), 1);
    item->diff = diff;
    item->mstime = now;
    list_add_item(alist, (long)item);
    item = (struct average_item*)list_get_item(alist, 0);
    while (item != 0)
    {
        if (item->mstime > now - MSTIME_HISTORY)
        {
            break;
        }
        list_remove_item(alist, 0);
        item = (struct average_item*)list_get_item(alist, 0);
    }
    acc = 0;
    count = alist->count;
    for (index = 0; index < count; index++)
    {
        item = (struct average_item*)list_get_item(alist, index);
        acc += item->diff;
    }
    average = (acc + (count / 2)) / count;
    LLOGLN(10, ("audio_delay_list_get_average: count %d average %d",
           count, average));
    return average;
}

/*****************************************************************************/
static struct main_to_worker_video_item*
get_main_to_worker_video_item(struct video_audio_info* vai,
                              int mstime, int* wait_mstime)
{
    struct main_to_worker_video_item* mtwvi;

    LLOGLN(10, ("get_main_to_worker_video_item: count %d",
           vai->main_to_worker_video_list->count));
    mtwvi = NULL;
    pthread_mutex_lock(&(vai->mutex));
    if (vai->main_to_worker_video_list->count > 0)
    {
        mtwvi = (struct main_to_worker_video_item*)
                list_get_item(vai->main_to_worker_video_list, 0);
        if (mtwvi->mstime > mstime)
        {
            *wait_mstime = mtwvi->mstime - mstime;
            pthread_mutex_unlock(&(vai->mutex));
            return NULL;
        }
        list_remove_item(vai->main_to_worker_video_list, 0); 
    }
    pthread_mutex_unlock(&(vai->mutex));
    return mtwvi;
}

/*****************************************************************************/
static int
add_main_to_worker_video_item(struct video_audio_info* vai,
                              struct main_to_worker_video_item* mtwvi)
{
    pthread_mutex_lock(&(vai->mutex));
    list_add_item(vai->main_to_worker_video_list, (long)mtwvi);
    pthread_mutex_unlock(&(vai->mutex));
    return 0;
}

/*****************************************************************************/
static struct worker_to_main_video_item*
get_worker_to_main_video_item(struct video_audio_info* vai)
{
    struct worker_to_main_video_item* wtmvi;

    wtmvi = NULL;
    pthread_mutex_lock(&(vai->mutex));
    if (vai->worker_to_main_video_list->count > 0)
    {
        wtmvi = (struct worker_to_main_video_item*)
                list_get_item(vai->worker_to_main_video_list, 0);
        list_remove_item(vai->worker_to_main_video_list, 0);
    }
    pthread_mutex_unlock(&(vai->mutex));
    return wtmvi;
}

/*****************************************************************************/
static int
add_worker_to_main_video_item(struct video_audio_info* vai,
                              struct worker_to_main_video_item* wtmvi)
{
    pthread_mutex_lock(&(vai->mutex));
    list_add_item(vai->worker_to_main_video_list, (long)wtmvi);
    pthread_mutex_unlock(&(vai->mutex));
    return 0;
}

/*****************************************************************************/
static struct main_to_worker_audio_item*
get_main_to_worker_audio_item(struct video_audio_info* vai)
{
    struct main_to_worker_audio_item* mtwai;

    mtwai = NULL;
    pthread_mutex_lock(&(vai->mutex));
    if (vai->main_to_worker_audio_list->count > 0)
    {
        mtwai = (struct main_to_worker_audio_item*)
                list_get_item(vai->main_to_worker_audio_list, 0);
        list_remove_item(vai->main_to_worker_audio_list, 0);
    }
    pthread_mutex_unlock(&(vai->mutex));
    return mtwai;
}

/*****************************************************************************/
static int
add_main_to_worker_audio_item(struct video_audio_info* vai,
                              struct main_to_worker_audio_item* mtwai)
{
    pthread_mutex_lock(&(vai->mutex));
    list_add_item(vai->main_to_worker_audio_list, (long)mtwai);
    pthread_mutex_unlock(&(vai->mutex));
    return 0;
}

/*****************************************************************************/
static int
decode_video_and_send_back(struct video_audio_info* vai,
                           struct main_to_worker_video_item* mtwvi)
{
    int error;
    int width;
    int height;
    int format;
    int out_data_bytes;
    int cdata_bytes_processed;
    int decoded;
    unsigned char* out_data;
    struct worker_to_main_video_item* wtmvi;
    unsigned char* cdata;
    int cdata_bytes;
    struct video_info* vi;

    cdata = mtwvi->data;
    cdata_bytes = mtwvi->data_bytes;
    vi = vai->vi;
    while (cdata_bytes > 0)
    {
        error = hdhome_run_avcodec_mpeg2_decode(vi->mpeg2_handle,
                                                cdata, cdata_bytes,
                                                &cdata_bytes_processed,
                                                &decoded);
        if (error != 0)
        {
            LLOGLN(0, ("decode_video_and_send_back: error decoding %d",
                   error));
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
                out_data = (unsigned char*)malloc(out_data_bytes);
                error = hdhome_run_avcodec_mpeg2_get_frame_data(vi->mpeg2_handle,
                                                                out_data,
                                                                out_data_bytes);
                if (error == 0)
                {
                    wtmvi = (struct worker_to_main_video_item*)
                        calloc(sizeof(struct worker_to_main_video_item), 1);
                    wtmvi->data = out_data;
                    wtmvi->data_bytes = out_data_bytes;
                    wtmvi->format = format;
                    wtmvi->width = width;
                    wtmvi->height = height;
                    add_worker_to_main_video_item(vai, wtmvi);
                    pipe_set(vai->worker_to_main_video_pipe);
                }
                else
                {
                    free(out_data);
                }
            }
        }
    }
    return 0;
}

/*****************************************************************************/
static int
video_process_item(struct mlcb_info* mlcbi,
                   struct main_to_worker_video_item* mtwvi)
{
    struct video_audio_info* vai;

    vai = mlcbi->vai;
    decode_video_and_send_back(vai, mtwvi);
    free(mtwvi->data);
    free(mtwvi);
    return 0;
}

/*****************************************************************************/
static void*
video_thread_proc(void* arg)
{
    int cont;
    fd_set rfds_set;
    int max_fd;
    int status;
    int now;
    int wait_mstime;
    struct main_to_worker_video_item* mtwvi;
    struct mlcb_info* mlcbi;
    struct timeval time;

    LLOGLN(0, ("video_thread_proc:"));
    mlcbi = (struct mlcb_info*)arg;
    wait_mstime = 1000;
    cont = 1;
    while (cont)
    {
        max_fd = 0;
        FD_ZERO(&rfds_set);
        FD_SET(mlcbi->vai->term_pipe[0], &rfds_set);
        if (mlcbi->vai->term_pipe[0] > max_fd)
        {
            max_fd = mlcbi->vai->term_pipe[0];
        }
        FD_SET(mlcbi->vai->main_to_worker_video_pipe[0], &rfds_set);
        if (mlcbi->vai->main_to_worker_video_pipe[0] > max_fd)
        {
            max_fd = mlcbi->vai->main_to_worker_video_pipe[0];
        }
        time.tv_sec = wait_mstime / 1000;
        time.tv_usec = (wait_mstime * 1000) % 1000000;
        status = select(max_fd + 1, &rfds_set, NULL, NULL, &time);
        if (status >= 0)
        {
            if (FD_ISSET(mlcbi->vai->term_pipe[0], &rfds_set))
            {
                cont = 0;
                break;
            }
            if (FD_ISSET(mlcbi->vai->main_to_worker_video_pipe[0], &rfds_set))
            {
                pipe_clear(mlcbi->vai->main_to_worker_video_pipe);
            }
            now = get_mstime();
            wait_mstime = 1000;
            mtwvi = get_main_to_worker_video_item(mlcbi->vai, now, &wait_mstime);
            while (mtwvi != NULL)
            {
                video_process_item(mlcbi, mtwvi);
                now = get_mstime();
                wait_mstime = 1000;
                mtwvi = get_main_to_worker_video_item(mlcbi->vai, now, &wait_mstime);
            }
        }
    }
    return 0;
}

/*****************************************************************************/
static int
decode_and_present_audio(struct video_audio_info* vai,
                         struct main_to_worker_audio_item* mtwai)
{
    int cdata_bytes_processed;
    int decoded;
    int error;
    int channels;
    int format;
    int out_data_bytes;
    int diff;
    int now;
    void* out_data;
    unsigned char* cdata;
    int cdata_bytes;
    struct audio_info* ai;

    cdata = mtwai->data;
    cdata_bytes = mtwai->data_bytes;
    ai = vai->ai;
    while (cdata_bytes > 0)
    {
        error = hdhome_run_avcodec_ac3_decode(ai->ac3_handle,
                                              cdata, cdata_bytes,
                                              &cdata_bytes_processed,
                                              &decoded);
        if (error != 0)
        {
            LLOGLN(0, ("decode_and_present_audio: error decoding %d", error));
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
                    if (ai->pa_handle == NULL)
                    {
                        ai->pa_handle = hdhome_run_pa_init("hdhome_run_player");
                        if (ai->pa_handle != NULL)
                        {
                            hdhome_run_pa_start(ai->pa_handle,
                                                "hdhome_run_player",
                                                HD_AUDIO_MSDELAY,
                                                CAP_PA_FORMAT_48000_6CH_16LE);
                        }
                    }
                    hdhome_run_pa_play(ai->pa_handle,
                                       out_data, out_data_bytes);
                    now = get_mstime();
                    mtwai->mstime_queued = now;
                    diff = mtwai->mstime_queued - mtwai->mstime_in;
                    vai->audio_latency =
                        audio_delay_list_get_average(ai->audio_delay_list,
                                                     now, diff);
                }
                free(out_data);
            }
        }
    }
    return 0;
}

/*****************************************************************************/
static int
audio_process_item(struct mlcb_info* mlcbi,
                   struct main_to_worker_audio_item* mtwai)
{
    struct video_audio_info* vai;

    vai = mlcbi->vai;
    decode_and_present_audio(vai, mtwai);
    free(mtwai->data);
    free(mtwai);
    return 0;
}

/*****************************************************************************/
static void*
audio_thread_proc(void* arg)
{
    int cont;
    fd_set rfds_set;
    int max_fd;
    int status;
    struct main_to_worker_audio_item* mtwai;
    struct mlcb_info* mlcbi;

    LLOGLN(0, ("audio_thread_proc:"));
    mlcbi = (struct mlcb_info*)arg;
    cont = 1;
    while (cont)
    {
        max_fd = 0;
        FD_ZERO(&rfds_set);
        FD_SET(mlcbi->vai->term_pipe[0], &rfds_set);
        if (mlcbi->vai->term_pipe[0] > max_fd)
        {
            max_fd = mlcbi->vai->term_pipe[0];
        }
        FD_SET(mlcbi->vai->main_to_worker_audio_pipe[0], &rfds_set);
        if (mlcbi->vai->main_to_worker_audio_pipe[0] > max_fd)
        {
            max_fd = mlcbi->vai->main_to_worker_audio_pipe[0];
        }
        status = select(max_fd + 1, &rfds_set, NULL, NULL, NULL);
        if (status > 0)
        {
            if (FD_ISSET(mlcbi->vai->term_pipe[0], &rfds_set))
            {
                cont = 0;
                break;
            }
            if (FD_ISSET(mlcbi->vai->main_to_worker_audio_pipe[0], &rfds_set))
            {
                pipe_clear(mlcbi->vai->main_to_worker_audio_pipe);
                mtwai = get_main_to_worker_audio_item(mlcbi->vai);
                while (mtwai != NULL)
                {
                    audio_process_item(mlcbi, mtwai);
                    mtwai = get_main_to_worker_audio_item(mlcbi->vai);
                }
            }
        }
    }
    return 0;
}

/*****************************************************************************/
int
tmpegts_video_cb(const void* data, int data_bytes,
                 const struct tmpegts* mpegts, void* udata)
{
    int rv;
    int remainder;
    struct video_audio_info* vai;
    struct video_info* vi;
    unsigned char* cdata;
    int cdata_bytes;
    int now;
    int diff;
    struct main_to_worker_video_item* mtwvi;

    rv = 0;
    vai = (struct video_audio_info*)udata;
    vi = vai->vi;
    if (mpegts->payload_unit_start_indicator)
    {
        if (vi->frame_data_pos > 0)
        {
            now = get_mstime();
            if (vi->time_flags == 0)
            {
                vi->time_flags = 1;
                vi->time_point = now;
                vi->time_count = 0;
            }
            else
            {
                vi->time_count++;
                if (now - vi->time_point > MSTIME_HISTORY)
                {
                    vi->fps = (vi->time_count + 2) / 4;
                    vi->frame_lo = (1000 / vi->fps) - 2;
                    vi->frame_hi = (1000 / vi->fps) + 2;
                    LLOGLN(10, ("tmpegts_video_cb: fps %d frame_lo %d "
                           "frame_hi %d",
                           vi->fps, vi->frame_lo, vi->frame_hi));
                    vi->time_point = now;
                    vi->time_count = 0;
                }
            }
            /* https://en.wikipedia.org/wiki/Packetized_elementary_stream */
            remainder = (unsigned char)(vi->frame_data[8]);
            cdata = (unsigned char*)(vi->frame_data + (9 + remainder));
            cdata_bytes = vi->frame_data_pos - (9 + remainder);
            mtwvi = (struct main_to_worker_video_item*)
                    calloc(sizeof(struct main_to_worker_video_item), 1);
            mtwvi->data = (unsigned char*)malloc(cdata_bytes);
            memcpy(mtwvi->data, cdata, cdata_bytes);
            mtwvi->data_bytes = cdata_bytes;
            mtwvi->mstime = now + vai->audio_latency + HD_AUDIO_MSDELAY + 100;
            if (vi->fps > 0)
            {
                diff = mtwvi->mstime - vi->last_mstime;
                if (diff < vi->frame_lo)
                {
                    mtwvi->mstime = vi->last_mstime + vi->frame_lo;
                }
                if (diff > vi->frame_hi)
                {
                    mtwvi->mstime = vi->last_mstime + vi->frame_hi;
                }
            }
            vi->last_mstime = mtwvi->mstime;
            add_main_to_worker_video_item(vai, mtwvi);
            pipe_set(vai->main_to_worker_video_pipe);
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
    struct video_audio_info* vai;
    struct audio_info* ai;
    unsigned char* cdata;
    int cdata_bytes;
    struct main_to_worker_audio_item* mtwai;

    rv = 0;
    vai = (struct video_audio_info*)udata;
    ai = vai->ai;
    if (mpegts->payload_unit_start_indicator)
    {
        if (ai->frame_data_pos > 0)
        {
            /* https://en.wikipedia.org/wiki/Packetized_elementary_stream */
            remainder = (unsigned char)(ai->frame_data[8]);
            cdata = (unsigned char*)(ai->frame_data + (9 + remainder));
            cdata_bytes = ai->frame_data_pos - (9 + remainder);
            mtwai = (struct main_to_worker_audio_item*)
                    calloc(sizeof(struct main_to_worker_audio_item), 1);
            mtwai->data = (unsigned char*)malloc(cdata_bytes);
            memcpy(mtwai->data, cdata, cdata_bytes);
            mtwai->data_bytes = cdata_bytes;
            mtwai->mstime_in = get_mstime();
            add_main_to_worker_audio_item(vai, mtwai);
            pipe_set(vai->main_to_worker_audio_pipe);
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
        ai->continuity_counter = mpegts->continuity_counter;
    }
    ai->continuity_counter++;
    if (ai->continuity_counter > 15)
    {
        ai->continuity_counter = 0;
    }
    return rv;
}

/*****************************************************************************/
static int
hdhome_run_callback(int sck, void* udata)
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
    while ((data != NULL) && (error == 0))
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
static int
video_callback(int sck, void* udata)
{
    struct mlcb_info* mlcbi;
    unsigned char* decoded_data;
    int decoded_data_bytes;
    int error;
    int bytes;
    struct worker_to_main_video_item* wtmvi;

    mlcbi = (struct mlcb_info*)udata;
    if (mlcbi == NULL)
    {
        return 1;
    }
    pipe_clear(mlcbi->vai->worker_to_main_video_pipe);
    wtmvi = get_worker_to_main_video_item(mlcbi->vai);
    while (wtmvi != NULL)
    {
        error = hdhome_run_x11_get_buffer(wtmvi->width, wtmvi->height,
                                          wtmvi->format,
                                          (void**)(&decoded_data),
                                          &decoded_data_bytes);
        if (error == 0)
        {
            bytes = decoded_data_bytes;
            if (bytes > wtmvi->data_bytes)
            {
                bytes = wtmvi->data_bytes;
            }
            memcpy(decoded_data, wtmvi->data, bytes);
            hdhome_run_x11_show_buffer(wtmvi->width, wtmvi->height,
                                       wtmvi->format, decoded_data);
        }
        free(wtmvi->data);
        free(wtmvi);
        wtmvi = get_worker_to_main_video_item(mlcbi->vai);
    }
    return 0;
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
    int scks[32];
    tmlcb mlcbs[32];

    struct mlcb_info mlcbi;

    pthread_t thread;

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
    ai.audio_delay_list = list_create();
    ai.audio_delay_list->auto_free = 1;

    memset(&cb, 0, sizeof(cb));
    cb.pids[0] = 0x31;
    cb.pids[1] = 0x34;
    cb.procs[0] = tmpegts_video_cb;
    cb.procs[1] = tmpegts_audio_cb;

    memset(&vai, 0, sizeof(vai));
    vai.vi = &vi;
    vai.ai = &ai;
    pipe(vai.main_to_worker_video_pipe);
    pipe(vai.worker_to_main_video_pipe);
    pipe(vai.main_to_worker_audio_pipe);
    pipe(vai.term_pipe);
    pthread_mutex_init(&(vai.mutex), 0);
    vai.main_to_worker_video_list = list_create();
    vai.worker_to_main_video_list = list_create();
    vai.main_to_worker_audio_list = list_create();

    if (hdhome_run_avcodec_init() != 0)
    {
        LLOGLN(0, ("hdhome_run_avcodec_init failed"));
        return 1;
    }

    if (hdhome_run_x11_init() != 0)
    {
        LLOGLN(0, ("hdhome_run_x11_init failed"));
        return 1;
    }

    error = hdhome_run_avcodec_ac3_create(&(ai.ac3_handle));
    if (error != 0)
    {
        LLOGLN(0, ("hdhome_run_avcodec_ac3_create failed error %d", error));
    }

    error = hdhome_run_avcodec_mpeg2_create(&(vi.mpeg2_handle));
    if (error != 0)
    {
        LLOGLN(0, ("hdhome_run_avcodec_mpeg2_create failed error %d", error));
    }

    hdhr = hdhomerun_device_create(HDHOMERUN_DEVICE_ID_WILDCARD, 0, 0, 0);
    if (hdhr != NULL)
    {
        hdhr_vsck = hdhomerun_device_get_video_sock(hdhr);
        hdhr_sck = hdhomerun_video_get_sock(hdhr_vsck);
        dev_name = hdhomerun_device_get_name(hdhr);
        LLOGLN(0, ("opened device %s", dev_name));
        error = hdhomerun_device_stream_start(hdhr);
        LLOGLN(0, ("hdhomerun_device_stream_start %d", error));
        if (error == 1)
        {
            memset(&mlcbi, 0, sizeof(mlcbi));
            mlcbi.hdhr = hdhr;
            mlcbi.cb = &cb;
            mlcbi.vai = &vai;
            scks[0] = hdhr_sck;
            mlcbs[0] = hdhome_run_callback;
            scks[1] = vai.worker_to_main_video_pipe[0];
            mlcbs[1] = video_callback;
            thread = 0;
            pthread_create(&thread, 0, video_thread_proc, &mlcbi);
            pthread_detach(thread);
            thread = 0;
            pthread_create(&thread, 0, audio_thread_proc, &mlcbi);
            pthread_detach(thread);
            hdhome_run_x11_main_loop(scks, mlcbs, 2, &mlcbi);
        }
        hdhomerun_device_destroy(hdhr);
    }
    return 0;
}
