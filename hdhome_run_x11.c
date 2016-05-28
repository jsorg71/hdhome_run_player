/**
 * x11 calls
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
#include <sys/ipc.h>
#include <sys/shm.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XShm.h>
#include <X11/extensions/Xv.h>
#include <X11/extensions/Xvlib.h>

#include "hdhome_run_x11.h"

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

static Display* g_disp = 0;
static int g_screenNumber = 0;
static Window g_win = 0;
static long g_eventMask = 0;
static int g_disp_fd = 0;
static GC g_gc;

static int g_swidth = 0;
static int g_sheight = 0;
static unsigned int g_xv_event_base = 0;
static int g_xv_port = 0;

static int g_xv_shmid = -1;
static void * g_xv_shmaddr = 0;
static int g_xv_shmbytes = 0;

/*****************************************************************************/
int
hdhome_run_x11_init(void)
{
    int white;
    int black;
    int ret;
    unsigned int index;
    unsigned int version;
    unsigned int release;
    unsigned int event_base;
    unsigned int error_base;
    unsigned int request_base;
    unsigned int num_adaptors;
    XvAdaptorInfo* ai;
    XEvent evt;

    LLOGLN(0, ("hdhomerun_x11_init:"));
    g_disp = XOpenDisplay(NULL);
    if (g_disp == 0)
    {
        LLOGLN(0, ("error opening X display"));
        return 1;
    }
    g_screenNumber = DefaultScreen(g_disp);
    white = WhitePixel(g_disp, g_screenNumber);
    black = BlackPixel(g_disp, g_screenNumber);
    g_win = XCreateSimpleWindow(g_disp, DefaultRootWindow(g_disp),
                                50, 50, 800, 600, 0, black, white);
    XMapWindow(g_disp, g_win);
    g_eventMask = StructureNotifyMask | MapNotify | VisibilityChangeMask |
                  ButtonPressMask | ButtonReleaseMask | KeyPressMask;
    XSelectInput(g_disp, g_win, g_eventMask);
    XMaskEvent(g_disp, VisibilityNotify, &evt);
    g_disp_fd = ConnectionNumber(g_disp);
    ret = XvQueryExtension(g_disp, &version, &release, &request_base,
                           &event_base, &error_base);
    if (ret != Success)
    {
        LLOGLN(0, ("XvQueryExtension failedd"));
    }
    g_xv_event_base = event_base;
    ret = XvQueryAdaptors(g_disp, DefaultRootWindow(g_disp), &num_adaptors,
                          &ai);
    if (ret != Success)
    {
        LLOGLN(0, ("XvQueryAdaptors failed"));
    }

    for (index = 0; index < num_adaptors; index++)
    {
        if (g_xv_port == 0 && index == num_adaptors - 1)
        {
            g_xv_port = ai[index].base_id;
        }
    }

    g_gc = XCreateGC(g_disp, g_win, 0, NULL);

    XFlush(g_disp);

    return 0;
}

/*****************************************************************************/
int
hdhome_run_x11_get_buffer(int width, int height, int format,
                          void** buffer, int* buffer_bytes)
{
    int bytes;

    LLOGLN(10, ("hdhome_run_x11_get_buffer:"));
    bytes = width * height * 2;
    if (bytes > g_xv_shmbytes)
    {
        shmdt(g_xv_shmaddr);
        g_xv_shmbytes = bytes;
        g_xv_shmid = shmget(IPC_PRIVATE, g_xv_shmbytes, IPC_CREAT | 0777);
        g_xv_shmaddr = shmat(g_xv_shmid, 0, 0);
        shmctl(g_xv_shmid, IPC_RMID, NULL);
    }
    *buffer = g_xv_shmaddr;
    *buffer_bytes = g_xv_shmbytes;
    return 0;
}

/*****************************************************************************/
int
hdhome_run_x11_show_buffer(int width, int height, int format,
                           void* buffer)
{
    int dst_pixfmt;
    int x;
    int y;
    int swidth;
    int sheight;
    XShmSegmentInfo shminfo;
    XvImage * image;
    int ratio;

    LLOGLN(10, ("hdhome_run_x11_show_buffer:"));
    switch (format)
    {
        //dst_pixfmt = 0x30323449; /* I420 */
        //dst_pixfmt = 0x32595559; /* YUY2 */
        //dst_pixfmt = 0x59565955; /* UYVY */
        case 0: /* PIX_FMT_YUV420P / AV_PIX_FMT_YUV420P */
            dst_pixfmt = 0x30323449; /* I420 */
            break;
        default:
            return 1;
    }
    shminfo.shmid = g_xv_shmid;
    shminfo.shmaddr = (char*)g_xv_shmaddr;
    image = XvShmCreateImage(g_disp, g_xv_port, dst_pixfmt, 0,
                             width, height, &shminfo);
    image->data = shminfo.shmaddr;
    shminfo.readOnly = 0;
    if (!XShmAttach(g_disp, &shminfo))
    {
        XFree(image);
        return 1;
    }
    ratio = (width << 16) / height;
    sheight = g_sheight;
    swidth = (sheight * ratio + 32768) >> 16;
    if (swidth > g_swidth)
    {
        ratio = (height << 16) / width;
        swidth = g_swidth;
        sheight = (g_swidth * ratio + 32768) >> 16;
    }
    x = 0;
    if (swidth < g_swidth)
    {
        x = (g_swidth - swidth) / 2;
        XFillRectangle(g_disp, g_win, g_gc, 0, 0, x, g_sheight);
        XFillRectangle(g_disp, g_win, g_gc, x + swidth, 0, x + 1, g_sheight);
    }
    y = 0;
    if (sheight < g_sheight)
    {
        y = (g_sheight - sheight) / 2;
        XFillRectangle(g_disp, g_win, g_gc, 0, 0, g_swidth, y);
        XFillRectangle(g_disp, g_win, g_gc, 0, y + sheight, g_swidth, y + 1);
    }
    XvShmPutImage(g_disp, g_xv_port, g_win, g_gc, image, 0, 0,
                  width, height, x, y, swidth, sheight, 0);
    XSync(g_disp, 0);
    XShmDetach(g_disp, &shminfo);
    XFree(image);
    return 0;
}

/*****************************************************************************/
int
hdhome_run_x11_main_loop(int* sck, tmlcb* cb, int count, void* udata,
                         int term_fd)
{
    XEvent evt;
    fd_set rfds_set;
    int max_fd;
    int status;
    int error;
    int cont;
    int index;
    int mstimeout;
    struct timeval time;

    LLOGLN(0, ("hdhome_run_x11_main_loop:"));
    mstimeout = 100;
    error = 0;
    cont = 1;
    while (cont)
    {
        max_fd = 0;
        FD_ZERO(&rfds_set);
        FD_SET(term_fd, &rfds_set);
        if (term_fd > max_fd)
        {
            max_fd = term_fd;
        }
        FD_SET(g_disp_fd, &rfds_set);
        if (g_disp_fd > max_fd)
        {
            max_fd = g_disp_fd;
        }
        for (index = 0; index < count; index++)
        {
            FD_SET(sck[index], &rfds_set);
            if (sck[index] > max_fd)
            {
                max_fd = sck[index];
            }
        }
        time.tv_sec = mstimeout / 1000;
        time.tv_usec = (mstimeout * 1000) % 1000000;
        status = select(max_fd + 1, &rfds_set, NULL, NULL, &time);
        if (status >= 0)
        {
            if (FD_ISSET(term_fd, &rfds_set))
            {
                cont = 0;
                error = 1;
                break;
            }
            for (index = 0; index < count; index++)
            {
                error = (cb[index])(sck[index], udata);
                if (error != 0)
                {
                    LLOGLN(0, ("hdhome_run_x11_main_loop: cb failed "
                           "index %d sck %d error %d",
                           index, sck[index], error));
                }
            }
            while (XPending(g_disp) > 0)
            {
                XNextEvent(g_disp, &evt);
                switch (evt.type)
                {
                    case VisibilityNotify:
                        break;
                    case KeyPress:
                        break;
                    case ButtonRelease:
                        break;
                    case ConfigureNotify:
                        g_swidth = evt.xconfigure.width;
                        g_sheight = evt.xconfigure.height;
                        break;
                }
            }
        }
    }
    return error;
}

