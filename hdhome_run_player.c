/**
 * hdhome_run_player
 *
 * Copyright 2015-2016 Jay Sorg <jay.sorg@gmail.com>
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
    int this_pdu_bytes;
    char* frame_data_alloc;
    char* frame_data;
    int frame_data_bytes;
    int frame_data_pos;
    struct list* frame_list;
    int frame_delay;
    int pad2;
    void* mpeg2_handle;
};

struct audio_info
{
    int started;
    int this_pdu_bytes;
    char* frame_data_alloc;
    char* frame_data;
    int frame_data_bytes;
    int frame_data_pos;
    void* ac3_handle;
    void* pa_handle;
    struct list* audio_delay_list;
};

struct program_info
{
    int started;
    int pad0;
    char* frame_data_alloc;
    char* frame_data;
    int frame_data_bytes;
    int frame_data_pos;
};

struct zero_info
{
    int started;
    int pad0;
    char* frame_data_alloc;
    char* frame_data;
    int frame_data_bytes;
    int frame_data_pos;
};

struct video_audio_info
{
    struct video_info* vi;
    struct audio_info* ai;
    struct program_info* pi;
    struct zero_info* zi;
    int audio_latency;
    int pad0;
    int main_to_worker_video_pipe[2];
    int worker_to_main_video_pipe[2];
    int main_to_worker_audio_pipe[2];
    pthread_mutex_t* mutex;
    struct list* main_to_worker_video_list;
    struct list* worker_to_main_video_list;
    struct list* main_to_worker_audio_list;
    int got_cdiff;
    int cdiff;
    int last_video_dts;
    int last_audio_dts;
    int main_to_worker_video_bytes;
    int worker_to_main_video_bytes;
    int main_to_worker_audio_bytes;
    int pad1;
};

struct mlcb_info
{
    struct hdhomerun_device_t* hdhr;
    struct tmpegts_cb* cb;
    struct video_audio_info* vai;
    int term_pipe[2];
};

struct main_to_worker_video_item
{
    unsigned char* data;
    int data_bytes;
    int mstime_dts;
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
    int mstime_dts;
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
    if (item == NULL)
    {
        return 0;
    }
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
    if (count > 0)
    {
        average = (acc + (count / 2)) / count;
    }
    else
    {
        average = acc;
    }
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
    pthread_mutex_lock(vai->mutex);
    if (vai->main_to_worker_video_list->count > 0)
    {
        mtwvi = (struct main_to_worker_video_item*)
                list_get_item(vai->main_to_worker_video_list, 0);
        LLOGLN(10, ("get_main_to_worker_video_item: ms wait time %d",
               (int)(mtwvi->mstime_dts - mstime)));
        if ((mtwvi->mstime_dts > mstime) &&
            (mtwvi->mstime_dts - mstime < 10000))
        {
            *wait_mstime = mtwvi->mstime_dts - mstime;
            pthread_mutex_unlock(vai->mutex);
            return NULL;
        }
        list_remove_item(vai->main_to_worker_video_list, 0);
        vai->main_to_worker_video_bytes -= mtwvi->data_bytes;
    }
    pthread_mutex_unlock(vai->mutex);
    return mtwvi;
}

/*****************************************************************************/
static int
add_main_to_worker_video_item(struct video_audio_info* vai,
                              struct main_to_worker_video_item* mtwvi)
{
    pthread_mutex_lock(vai->mutex);
    list_add_item(vai->main_to_worker_video_list, (long)mtwvi);
    vai->main_to_worker_video_bytes += mtwvi->data_bytes;
    pthread_mutex_unlock(vai->mutex);
    if (vai->main_to_worker_video_bytes > 10 * 1024 * 1024)
    {
        LLOGLN(0, ("add_main_to_worker_video_item: "
               "main_to_worker_video_bytes %d",
               vai->main_to_worker_video_bytes));
    }
    return 0;
}

/*****************************************************************************/
static struct worker_to_main_video_item*
get_worker_to_main_video_item(struct video_audio_info* vai)
{
    struct worker_to_main_video_item* wtmvi;

    wtmvi = NULL;
    pthread_mutex_lock(vai->mutex);
    if (vai->worker_to_main_video_list->count > 0)
    {
        wtmvi = (struct worker_to_main_video_item*)
                list_get_item(vai->worker_to_main_video_list, 0);
        list_remove_item(vai->worker_to_main_video_list, 0);
        vai->worker_to_main_video_bytes -= wtmvi->data_bytes;
    }
    pthread_mutex_unlock(vai->mutex);
    return wtmvi;
}

/*****************************************************************************/
static int
add_worker_to_main_video_item(struct video_audio_info* vai,
                              struct worker_to_main_video_item* wtmvi)
{
    pthread_mutex_lock(vai->mutex);
    list_add_item(vai->worker_to_main_video_list, (long)wtmvi);
    vai->worker_to_main_video_bytes += wtmvi->data_bytes;
    pthread_mutex_unlock(vai->mutex);
    if (vai->worker_to_main_video_bytes > 10 * 1024 * 1024)
    {
        LLOGLN(0, ("add_worker_to_main_video_item: "
               "worker_to_main_video_bytes %d",
               vai->worker_to_main_video_bytes));
    }
    return 0;
}

/*****************************************************************************/
static struct main_to_worker_audio_item*
get_main_to_worker_audio_item(struct video_audio_info* vai)
{
    struct main_to_worker_audio_item* mtwai;

    mtwai = NULL;
    pthread_mutex_lock(vai->mutex);
    if (vai->main_to_worker_audio_list->count > 0)
    {
        mtwai = (struct main_to_worker_audio_item*)
                list_get_item(vai->main_to_worker_audio_list, 0);
        list_remove_item(vai->main_to_worker_audio_list, 0);
        vai->main_to_worker_audio_bytes -= mtwai->data_bytes;
    }
    pthread_mutex_unlock(vai->mutex);
    return mtwai;
}

/*****************************************************************************/
static int
add_main_to_worker_audio_item(struct video_audio_info* vai,
                              struct main_to_worker_audio_item* mtwai)
{
    pthread_mutex_lock(vai->mutex);
    list_add_item(vai->main_to_worker_audio_list, (long)mtwai);
    vai->main_to_worker_audio_bytes += mtwai->data_bytes;
    pthread_mutex_unlock(vai->mutex);
    if (vai->main_to_worker_audio_bytes > 10 * 1024 * 1024)
    {
        LLOGLN(0, ("add_main_to_worker_audio_item: "
               "main_to_worker_audio_bytes %d",
               vai->main_to_worker_audio_bytes));
    }
    return 0;
}

/*****************************************************************************/
/* video thread */
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
                if (out_data != NULL)
                {
                    error = hdhome_run_avcodec_mpeg2_get_frame_data(vi->mpeg2_handle,
                                                                    out_data,
                                                                    out_data_bytes);
                    if (error == 0)
                    {
                        wtmvi = (struct worker_to_main_video_item*)
                                calloc(sizeof(struct worker_to_main_video_item), 1);
                        if (wtmvi != NULL)
                        {
                            wtmvi->data = out_data;
                            wtmvi->data_bytes = out_data_bytes;
                            wtmvi->format = format;
                            wtmvi->width = width;
                            wtmvi->height = height;
                            add_worker_to_main_video_item(vai, wtmvi);
                            pipe_set(vai->worker_to_main_video_pipe);
                        }
                    }
                    else
                    {
                        free(out_data);
                    }
                }
            }
        }
    }
    return 0;
}

/*****************************************************************************/
/* video thread */
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
/* video thread */
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
        FD_SET(mlcbi->term_pipe[0], &rfds_set);
        if (mlcbi->term_pipe[0] > max_fd)
        {
            max_fd = mlcbi->term_pipe[0];
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
            if (FD_ISSET(mlcbi->term_pipe[0], &rfds_set))
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
            mtwvi = get_main_to_worker_video_item(mlcbi->vai, now,
                                                  &wait_mstime);
            while (mtwvi != NULL)
            {
                video_process_item(mlcbi, mtwvi);
                now = get_mstime();
                wait_mstime = 1000;
                mtwvi = get_main_to_worker_video_item(mlcbi->vai, now,
                                                      &wait_mstime);
            }
        }
    }
    return 0;
}

/*****************************************************************************/
/* audio thread */
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
    int pa_flags;
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
                if (out_data != NULL)
                {
                    error =  hdhome_run_avcodec_ac3_get_frame_data(ai->ac3_handle,
                                                                   out_data,
                                                                   out_data_bytes);
                    if (error == 0)
                    {
                        if (ai->pa_handle == NULL)
                        {
                            error = hdhome_run_pa_init("hdhome_run_player",
                                                       &(ai->pa_handle));
                            if (error == 0)
                            {
                                switch (channels)
                                {
                                    case 1:
                                        LLOGLN(0, ("decode_and_present_audio: "
                                               "starting 1 channel 48000 audio"));
                                        pa_flags = CAP_PA_FORMAT_48000_1CH_16LE;
                                        break;
                                    case 2:
                                        LLOGLN(0, ("decode_and_present_audio: "
                                               "starting 2 channel 48000 audio"));
                                        pa_flags = CAP_PA_FORMAT_48000_2CH_16LE;
                                        break;
                                    default:
                                        LLOGLN(0, ("decode_and_present_audio: "
                                               "starting 6 channel 48000 audio"));
                                        pa_flags = CAP_PA_FORMAT_48000_6CH_16LE;
                                        break;
                                }
                                hdhome_run_pa_start(ai->pa_handle,
                                                    "hdhome_run_player",
                                                    HD_AUDIO_MSDELAY, pa_flags);
                            }
                        }
                        if (ai->pa_handle != NULL)
                        {
#if 1
                            hdhome_run_pa_play(ai->pa_handle,
                                               out_data, out_data_bytes);
#else
                            hdhome_run_pa_play_non_blocking(ai->pa_handle,
                                                            out_data,
                                                            out_data_bytes, 0);
#endif
                            now = get_mstime();
                            mtwai->mstime_queued = now;
                            diff = now - mtwai->mstime_dts;
                            vai->audio_latency =
                                audio_delay_list_get_average(ai->audio_delay_list,
                                                             now, diff);
                            LLOGLN(10, ("decode_and_present_audio: "
                                   "audio_latency %d diff %d",
                                   vai->audio_latency, diff));
                        }
                    }
                    free(out_data);
                }
            }
        }
    }
    return 0;
}

/*****************************************************************************/
/* audio thread */
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
/* audio thread */
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
        FD_SET(mlcbi->term_pipe[0], &rfds_set);
        if (mlcbi->term_pipe[0] > max_fd)
        {
            max_fd = mlcbi->term_pipe[0];
        }
        FD_SET(mlcbi->vai->main_to_worker_audio_pipe[0], &rfds_set);
        if (mlcbi->vai->main_to_worker_audio_pipe[0] > max_fd)
        {
            max_fd = mlcbi->vai->main_to_worker_audio_pipe[0];
        }
        status = select(max_fd + 1, &rfds_set, NULL, NULL, NULL);
        if (status >= 0)
        {
            if (FD_ISSET(mlcbi->term_pipe[0], &rfds_set))
            {
                cont = 0;
                break;
            }
            if (FD_ISSET(mlcbi->vai->main_to_worker_audio_pipe[0], &rfds_set))
            {
                pipe_clear(mlcbi->vai->main_to_worker_audio_pipe);
            }
            mtwai = get_main_to_worker_audio_item(mlcbi->vai);
            while (mtwai != NULL)
            {
                audio_process_item(mlcbi, mtwai);
                mtwai = get_main_to_worker_audio_item(mlcbi->vai);
            }
        }
    }
    return 0;
}

/*****************************************************************************/
static int
read_time(const void* ptr, int* time)
{
    const unsigned char* pui8;
    unsigned int t1;

    pui8 = (const unsigned char*) ptr;
    /* 11100000 00000000 00000000 00000000 3 bits */
    /* 00011111 11100000 00000000 00000000 8 bits */
    /* 00000000 00011111 11000000 00000000 7 bits */
    /* 00000000 00000000 00111111 11000000 8 bits */
    /* 00000000 00000000 00000000 00111111 6 bits */
    t1 = ((pui8[0] & 0x0E) << 28) | ((pui8[1] & 0xFF) << 21) |
         ((pui8[2] & 0xFE) << 13) | ((pui8[3] & 0xFF) <<  6) |
         ((pui8[4] & 0xFE) >>  2);
    t1 = t1 / 45;
    *time = t1;
    return 0;
}

/*****************************************************************************/
static int
read_pts_dts(const void* ptr, int* pts, int* dts)
{
    const unsigned char* pui8;

    pui8 = (const unsigned char*) ptr;
    if ((pui8[7] & 0xc0) == 0xc0)
    {
        LLOGLN(10, ("read_pts_dts: got pts/dts"));
        read_time(pui8 + 9, pts);
        read_time(pui8 + 9 + 5, dts);
    }
    else if ((pui8[7] & 0x80) == 0x80)
    {
        LLOGLN(10, ("read_pts_dts: got pts"));
        read_time(pui8 + 9, pts);
        *dts = *pts;
    }
    else
    {
        return 1;
    }
    return 0;
}

/*****************************************************************************/
static int
read_pcr(const void* ptr, int* pcr)
{
    const unsigned char* pui8;
    unsigned int t1;

    pui8 = (const unsigned char*) ptr;
    t1 = (pui8[0] << 24) | (pui8[1] << 16) | (pui8[2] << 8) | pui8[3];
    t1 = t1 / 45;
    *pcr = t1;
    return 0;
}

/*****************************************************************************/
/* main thread */
int
tmpegts_video_cb(const void* data, int data_bytes,
                 const struct tmpegts* mpegts, void* udata)
{
    int rv;
    int remainder;
    struct mlcb_info* mlcbi;
    struct video_audio_info* vai;
    struct video_info* vi;
    unsigned char* cdata;
    int cdata_bytes;
    int now;
    int mstime_dts;
    struct main_to_worker_video_item* mtwvi;
    int pts;
    int dts;
    int pcr;
    int cdiff;

    rv = 0;
    mlcbi = (struct mlcb_info*)udata;
    vai = mlcbi->vai;
    vi = vai->vi;

    if (mpegts->pcr_flag)
    {
        LLOGLN(10, ("tmpegts_video_cb: got pcr_flag"));
        /* 33 bit time */
        if (read_pcr(mpegts->pcr, &pcr) == 0)
        {
            /* get the difference between our clock and
               server clock */
            LLOGLN(10, ("tmpegts_video_cb: update clock diff"));
            vai->got_cdiff = 1;
            now = get_mstime();
            cdiff = pcr - now;
            vai->cdiff = cdiff;
            LLOGLN(10, ("tmpegts_video_cb: update clock diff %d", vai->cdiff));
        }
    }

    if (mpegts->payload_unit_start_indicator)
    {
        LLOGLN(10, ("tmpegts_video_cb: start"));
        //hex_dump(vi->frame_data, 64);
        vi->this_pdu_bytes = 0;
        /* https://en.wikipedia.org/wiki/Packetized_elementary_stream */
        if ((vi->frame_data[0] == 0) && (vi->frame_data[1] == 0) &&
            (vi->frame_data[2] == 1))
        {
            /* for video packet, this is zero */
            vi->this_pdu_bytes = (vi->frame_data[4] << 8) | vi->frame_data[5];
            vi->this_pdu_bytes += 6;
        }
        if (vi->frame_data_pos > 0)
        {
            //hex_dump(vi->frame_data, 128);
            now = get_mstime();
            if (vai->got_cdiff == 0)
            {
                mstime_dts = now;
            }
            else if (read_pts_dts(vi->frame_data, &pts, &dts) == 0)
            {
                LLOGLN(10, ("tmpegts_video_cb: pts %d dts %d", pts, dts));
                vai->last_video_dts = dts;
                mstime_dts = dts - vai->cdiff;
            }
            else
            {
                LLOGLN(0, ("tmpegts_video_cb: read_pts_dts failed"));
                mstime_dts = now;
            }
            LLOGLN(10, ("tmpegts_video_cb: now - mstime_dts = %d", now - mstime_dts));
            /* https://en.wikipedia.org/wiki/Packetized_elementary_stream */
            remainder = (unsigned char)(vi->frame_data[8]);
            cdata = (unsigned char*)(vi->frame_data + (9 + remainder));
            cdata_bytes = vi->frame_data_pos - (9 + remainder);
            LLOGLN(10, ("cdata_bytes %d", cdata_bytes));
            mtwvi = (struct main_to_worker_video_item*)
                    calloc(sizeof(struct main_to_worker_video_item), 1);
            if (mtwvi != NULL)
            {
                mtwvi->data = (unsigned char*)malloc(cdata_bytes);
                if (mtwvi->data != NULL)
                {
                    memcpy(mtwvi->data, cdata, cdata_bytes); 
                    mtwvi->data_bytes = cdata_bytes;
                    LLOGLN(10, ("tmpegts_video_cb: audio_latency %d",
                           vai->audio_latency));
                    mtwvi->mstime_dts = mstime_dts + vai->audio_latency +
                                        HD_AUDIO_MSDELAY - 500;
                    add_main_to_worker_video_item(vai, mtwvi);
                    pipe_set(vai->main_to_worker_video_pipe);
                }
            }
        }
        vi->frame_data_pos = 0;
        vi->started = 1;
    }
    if (mpegts->payload_flag && vi->started)
    {
        memcpy(vi->frame_data + vi->frame_data_pos, data, data_bytes);
        vi->frame_data_pos += data_bytes;
        LLOGLN(10, ("vi->frame_data_pos %d vi->this_pdu_bytes %d",
               vi->frame_data_pos, vi->this_pdu_bytes));
    }
    return rv;
}

/*****************************************************************************/
/* main thread */
int
tmpegts_audio_cb(const void* data, int data_bytes,
                 const struct tmpegts* mpegts, void* udata)
{
    int rv;
    int remainder;
    struct mlcb_info* mlcbi;
    struct video_audio_info* vai;
    struct audio_info* ai;
    unsigned char* cdata;
    int cdata_bytes;
    struct main_to_worker_audio_item* mtwai;
    int pts;
    int dts;

    rv = 0;
    mlcbi = (struct mlcb_info*)udata;
    vai = mlcbi->vai;
    ai = vai->ai;
    if (mpegts->payload_unit_start_indicator)
    {
        LLOGLN(10, ("tmpegts_audio_cb: start"));
        ai->this_pdu_bytes = 0;
        /* https://en.wikipedia.org/wiki/Packetized_elementary_stream */
        if ((ai->frame_data[0] == 0) && (ai->frame_data[1] == 0) &&
            (ai->frame_data[2] == 1))
        {
            ai->this_pdu_bytes = (ai->frame_data[4] << 8) | ai->frame_data[5];
            ai->this_pdu_bytes += 6;
        }
        ai->frame_data_pos = 0;
        ai->started = 1;
    }
    if (mpegts->payload_flag && ai->started)
    {
        memcpy(ai->frame_data + ai->frame_data_pos, data, data_bytes);
        ai->frame_data_pos += data_bytes;
        if ((ai->this_pdu_bytes > 6) &&
            (ai->frame_data_pos >= ai->this_pdu_bytes))
        {
            if (read_pts_dts(ai->frame_data, &pts, &dts) == 0)
            {
                LLOGLN(10, ("tmpegts_audio_cb: pts %d dts %d", pts, dts));
                vai->last_audio_dts = dts;
            }
            else
            {
                LLOGLN(0, ("tmpegts_audio_cb: read_pts_dts failed"));
            }
            /* https://en.wikipedia.org/wiki/Packetized_elementary_stream */
            remainder = (unsigned char)(ai->frame_data[8]);
            cdata = (unsigned char*)(ai->frame_data + (9 + remainder));
            cdata_bytes = ai->frame_data_pos - (9 + remainder);
            mtwai = (struct main_to_worker_audio_item*)
                    calloc(sizeof(struct main_to_worker_audio_item), 1);
            if (mtwai != NULL)
            {
                mtwai->data = (unsigned char *)malloc(cdata_bytes); 
                if (mtwai->data != NULL)
                {
                    memcpy(mtwai->data, cdata, cdata_bytes);
                    mtwai->data_bytes = cdata_bytes;
                    mtwai->mstime_in = get_mstime();
                    mtwai->mstime_dts = vai->last_audio_dts - vai->cdiff;
                    add_main_to_worker_audio_item(vai, mtwai);
                    pipe_set(vai->main_to_worker_audio_pipe);
                }
            }
        }
    }
    return rv;
}

/*****************************************************************************/
/* main thread */
static int
tmpegts_program_cb(const void* data, int data_bytes,
                   const struct tmpegts* mpegts, void* udata)
{
    struct mlcb_info* mlcbi;
    struct video_audio_info* vai;
    struct program_info* pi;

    LLOGLN(10, ("tmpegts_program_cb: data_bytes %d", data_bytes));
    mlcbi = (struct mlcb_info*)udata;
    vai = mlcbi->vai;
    pi = vai->pi;
    if (mpegts->payload_unit_start_indicator)
    {
        pi->frame_data_pos = 0;
        pi->started = 1;
    }
    if (mpegts->payload_flag && pi->started)
    {
        memcpy(pi->frame_data + pi->frame_data_pos, data, data_bytes);
        //hex_dump(pi->frame_data, 128);
        pi->frame_data_pos += data_bytes;
        if (mlcbi->cb->num_pids == 2)
        {
            LLOGLN(0, ("tmpegts_program_cb: adding pids for "
                   "video and audio"));
            mlcbi->cb->pids[2] = 0x31;
            mlcbi->cb->pids[3] = 0x34;
            //mlcbi->cb->pids[2] = 0x41;
            //mlcbi->cb->pids[3] = 0x44;
            mlcbi->cb->procs[2] = tmpegts_video_cb;
            mlcbi->cb->procs[3] = tmpegts_audio_cb;
            mlcbi->cb->num_pids = 4;
        }
    }
    return 0;
}

/*****************************************************************************/
/* main thread */
static int
tmpegts_zero_cb(const void* data, int data_bytes,
                const struct tmpegts* mpegts, void* udata)
{
    struct mlcb_info* mlcbi;
    struct video_audio_info* vai;
    struct zero_info* zi;
    int program_pid;
    int bytes;
    char* ptr;

    LLOGLN(10, ("tmpegts_zero_cb: data_bytes %d", data_bytes));
    mlcbi = (struct mlcb_info*)udata;
    vai = mlcbi->vai;
    zi = vai->zi;
    if (mpegts->payload_unit_start_indicator)
    {
        zi->frame_data_pos = 0;
        zi->started = 1;
    }
    if (mpegts->payload_flag && zi->started)
    {
        memcpy(zi->frame_data + zi->frame_data_pos, data, data_bytes);
        //hex_dump(zi->frame_data, 128);
        zi->frame_data_pos += data_bytes;
        if (mlcbi->cb->num_pids == 1)
        {
            program_pid = 0;
            if (zi->frame_data[0] == 0 &&
                zi->frame_data[1] == 0 &&
                zi->frame_data[2] == (char)0xb0)
            {
                bytes = (unsigned char)(zi->frame_data[3]);
                bytes -= 9;
                ptr = zi->frame_data + 9;
                while (bytes > 0)
                {
                    LLOGLN(0, ("tmpegts_zero_cb: found program 0x%x", ptr[3]));
                    if (program_pid == 0)
                    {
                        program_pid = (unsigned char)(ptr[3]);
                    }
                    ptr += 4;
                    bytes -= 4;
                }
            }
            if (program_pid != 0)
            {
                LLOGLN(0, ("tmpegts_zero_cb: adding program 0x%x", program_pid));
                mlcbi->cb->pids[1] = program_pid;
                mlcbi->cb->procs[1] = tmpegts_program_cb;
                mlcbi->cb->num_pids = 2;
            }
        }
    }
    return 0;
}

/*****************************************************************************/
/* main thread */
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
            error = process_mpeg_ts_packet(data, lbytes, mlcbi->cb, mlcbi);
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
/* main thread */
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
    struct program_info pi;
    struct zero_info zi;
    struct video_audio_info vai;
    int scks[32];
    tmlcb mlcbs[32];
    struct mlcb_info mlcbi;
    pthread_t thread;

    memset(&vi, 0, sizeof(vi));
    vi.frame_data_bytes = 1024 * 1024;
    vi.frame_data_alloc = (char*)malloc(vi.frame_data_bytes + 16);
    vi.frame_data = (char*)((((long)vi.frame_data_alloc) + 15) & ~15);
    vi.frame_list = list_create();
    memset(&ai, 0, sizeof(ai));
    ai.frame_data_bytes = 1024 * 1024;
    ai.frame_data_alloc = (char*)malloc(ai.frame_data_bytes + 16);
    ai.frame_data = (char*)((((long)ai.frame_data_alloc) + 15) & ~15);
    ai.audio_delay_list = list_create();
    ai.audio_delay_list->auto_free = 1;
    memset(&pi, 0, sizeof(pi));
    pi.frame_data_bytes = 1024 * 1024;
    pi.frame_data_alloc = (char*)malloc(pi.frame_data_bytes + 16);
    pi.frame_data = (char*)((((long)pi.frame_data_alloc) + 15) & ~15);
    memset(&zi, 0, sizeof(zi));
    zi.frame_data_bytes = 1024 * 1024;
    zi.frame_data_alloc = (char*)malloc(zi.frame_data_bytes + 16);
    zi.frame_data = (char*)((((long)zi.frame_data_alloc) + 15) & ~15);
    memset(&cb, 0, sizeof(cb));
    cb.pids[0] = 0x00;
    cb.procs[0] = tmpegts_zero_cb;
    cb.num_pids = 1;
    memset(&vai, 0, sizeof(vai));
    vai.vi = &vi;
    vai.ai = &ai;
    vai.pi = &pi;
    vai.zi = &zi;
    if (pipe(vai.main_to_worker_video_pipe) != 0)
    {
        LLOGLN(0, ("pipe failed"));
        return 1;
    }
    if (pipe(vai.worker_to_main_video_pipe) != 0)
    {
        LLOGLN(0, ("pipe failed"));
        return 1;
    }
    if (pipe(vai.main_to_worker_audio_pipe) != 0)
    {
        LLOGLN(0, ("pipe failed"));
        return 1;
    }
    vai.mutex = (pthread_mutex_t*)malloc(sizeof(pthread_mutex_t));
    if (vai.mutex == NULL)
    {
        LLOGLN(0, ("malloc failed"));
        return 1;
    }
    if (pthread_mutex_init(vai.mutex, NULL) != 0)
    {
        LLOGLN(0, ("pthread_mutex_init failed"));
        return 1;
    }
    vai.main_to_worker_video_list = list_create();
    if (vai.main_to_worker_video_list == NULL)
    {
        LLOGLN(0, ("list_create failed"));
        return 1;
    }
    vai.worker_to_main_video_list = list_create();
    if (vai.worker_to_main_video_list == NULL)
    {
        LLOGLN(0, ("list_create failed"));
        return 1;
    }
    vai.main_to_worker_audio_list = list_create();
    if (vai.main_to_worker_audio_list == NULL)
    {
        LLOGLN(0, ("list_create failed"));
        return 1;
    }
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
    //hdhr = hdhomerun_device_create(HDHOMERUN_DEVICE_ID_WILDCARD, 0x0a00000b, 0, 0);
    //hdhr = hdhomerun_device_create_from_str("103BF3FB-1", 0);
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
            if (pipe(mlcbi.term_pipe) != 0)
            {
                LLOGLN(0, ("pipe failed"));
                return 1;
            }
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
            hdhome_run_x11_main_loop(scks, mlcbs, 2, &mlcbi,
                                     mlcbi.term_pipe[0]);
        }
        hdhomerun_device_destroy(hdhr);
    }
    return 0;
}
