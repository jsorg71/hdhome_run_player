/**
 * x11 calls
 *
 * Copyright 2015-2018 Jay Sorg <jay.sorg@gmail.com>
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
#include "hdhome_run_log.h"

struct x11_info
{
    Display* disp;
    int screenNumber;
    Window win;
    long eventMask;
    int disp_fd;
    GC gc;
    int swidth;
    int sheight;
    unsigned int xv_event_base;
    int xv_port;
    int xv_shmid;
    void* xv_shmaddr;
    int xv_shmbytes;
    int vwidth;
    int vheight;
    int got_vis_not;
};

/*****************************************************************************/
int
hdhome_run_x11_init(void)
{
    LOGLN(0, (0, LOGF, LOGP));
    return 0;
}

/*****************************************************************************/
int
hdhome_run_x11_create(void** obj)
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
    struct x11_info* self;

    LOGLN(0, (0, LOGF, LOGP));
    self = (struct x11_info*)calloc(sizeof(struct x11_info), 1);
    if (self == NULL)
    {
        return 1;
    }
    self->xv_shmid = -1;
    self->disp = XOpenDisplay(NULL);
    if (self->disp == 0)
    {
        LOGLN(0, (0, LOGF "error opening X display", LOGP));
        return 2;
    }
    self->disp_fd = ConnectionNumber(self->disp);
    self->screenNumber = DefaultScreen(self->disp);
    white = WhitePixel(self->disp, self->screenNumber);
    black = BlackPixel(self->disp, self->screenNumber);
    self->win = XCreateSimpleWindow(self->disp, DefaultRootWindow(self->disp),
                                    50, 50, 800, 600, 0, black, white);
    self->eventMask = StructureNotifyMask | VisibilityChangeMask |
                      ButtonPressMask | ButtonReleaseMask | KeyPressMask;
    XSelectInput(self->disp, self->win, self->eventMask);
    XMapWindow(self->disp, self->win);
    ret = XvQueryExtension(self->disp, &version, &release, &request_base,
                           &event_base, &error_base);
    if (ret != Success)
    {
        LOGLN(0, (0, LOGF "XvQueryExtension failedd", LOGP));
        return 3;
    }
    self->xv_event_base = event_base;
    ret = XvQueryAdaptors(self->disp, DefaultRootWindow(self->disp),
                          &num_adaptors, &ai);
    if (ret != Success)
    {
        LOGLN(0, (0, LOGF "XvQueryAdaptors failed", LOGP));
        return 4;
    }
    for (index = 0; index < num_adaptors; index++)
    {
        if (self->xv_port == 0 && index == num_adaptors - 1)
        {
            self->xv_port = ai[index].base_id;
        }
    }
    XvFreeAdaptorInfo(ai);
    self->gc = XCreateGC(self->disp, self->win, 0, NULL);
    XFlush(self->disp);

    *obj = self;

    return 0;
}

/*****************************************************************************/
int
hdhome_run_x11_delete(void* obj)
{
    return 0;
}

/*****************************************************************************/
int
hdhome_run_x11_get_buffer(void* obj, int width, int height, int format,
                          void** buffer, int* buffer_bytes)
{
    struct x11_info* self;
    int bytes;

    LOGLN(10, (0, LOGF, LOGP));
    self = (struct x11_info*)obj;
    if ((width != self->vwidth) || (height != self->vheight))
    {
        LOGLN(0, (0, LOGF "createing buffer for %dx%d "
               "video", LOGP, width, height));
        self->vwidth = width;
        self->vheight = height;
    }
    bytes = width * height * 2;
    if (bytes > self->xv_shmbytes)
    {
        shmdt(self->xv_shmaddr);
        self->xv_shmbytes = bytes;
        self->xv_shmid = shmget(IPC_PRIVATE, self->xv_shmbytes,
                                IPC_CREAT | 0777);
        self->xv_shmaddr = shmat(self->xv_shmid, 0, 0);
        shmctl(self->xv_shmid, IPC_RMID, NULL);
    }
    *buffer = self->xv_shmaddr;
    *buffer_bytes = self->xv_shmbytes;
    return 0;
}

/*****************************************************************************/
int
hdhome_run_x11_show_buffer(void* obj, int width, int height, int format,
                           void* buffer)
{
    struct x11_info* self;
    int dst_pixfmt;
    int x;
    int y;
    int swidth;
    int sheight;
    XShmSegmentInfo shminfo;
    XvImage * image;
    int ratio;

    LOGLN(10, (0, LOGF, LOGP));
    self = (struct x11_info*)obj;
    if (self->got_vis_not == 0)
    {
        LOGLN(0, (0, LOGF "g_got_vis_not is false", LOGP));
        return 1;
    }

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
    shminfo.shmid = self->xv_shmid;
    shminfo.shmaddr = (char*)self->xv_shmaddr;
    image = XvShmCreateImage(self->disp, self->xv_port, dst_pixfmt, 0,
                             width, height, &shminfo);
    image->data = shminfo.shmaddr;
    shminfo.readOnly = 0;
    if (!XShmAttach(self->disp, &shminfo))
    {
        XFree(image);
        return 1;
    }
    ratio = (width << 16) / height;
    sheight = self->sheight;
    swidth = (sheight * ratio + 32768) >> 16;
    if (swidth > self->swidth)
    {
        ratio = (height << 16) / width;
        swidth = self->swidth;
        sheight = (self->swidth * ratio + 32768) >> 16;
    }
    x = 0;
    if (swidth < self->swidth)
    {
        x = (self->swidth - swidth) / 2;
        XFillRectangle(self->disp, self->win, self->gc, 0, 0, x,
                       self->sheight);
        XFillRectangle(self->disp, self->win, self->gc, x + swidth, 0, x + 1,
                       self->sheight);
    }
    y = 0;
    if (sheight < self->sheight)
    {
        y = (self->sheight - sheight) / 2;
        XFillRectangle(self->disp, self->win, self->gc, 0, 0, self->swidth, y);
        XFillRectangle(self->disp, self->win, self->gc, 0, y + sheight,
                       self->swidth, y + 1);
    }
    XvShmPutImage(self->disp, self->xv_port, self->win, self->gc, image, 0, 0,
                  width, height, x, y, swidth, sheight, 0);
    XSync(self->disp, 0);
    XShmDetach(self->disp, &shminfo);
    XFree(image);
    return 0;
}

/*****************************************************************************/
int
hdhome_run_x11_main_loop(void* obj, int* sck, tmlcb* cb, int count,
                         void* udata, int term_fd)
{
    struct x11_info* self;
    XEvent evt;
    fd_set rfds_set;
    int max_fd;
    int status;
    int error;
    int cont;
    int index;
    int mstimeout;
    struct timeval time;

    LOGLN(0, (0, LOGF, LOGP));
    self = (struct x11_info*)obj;
    mstimeout = 15;
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
        FD_SET(self->disp_fd, &rfds_set);
        if (self->disp_fd > max_fd)
        {
            max_fd = self->disp_fd;
        }
        for (index = 0; index < count; index++)
        {
            if (sck[index] != -1)
            {
                FD_SET(sck[index], &rfds_set);
                if (sck[index] > max_fd)
                {
                    max_fd = sck[index];
                }
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
                    LOGLN(0, (0, LOGF, "cb failed "
                           "index %d sck %d error %d", LOGP,
                           index, sck[index], error));
                }
            }
            while (XPending(self->disp) > 0)
            {
                XNextEvent(self->disp, &evt);
                switch (evt.type)
                {
                    case MapNotify:
                        LOGLN(10, (0, LOGF "MapNotify", LOGP));
                        break;
                    case VisibilityNotify:
                        LOGLN(10, (0, LOGF "VisibilityNotify", LOGP));
                        self->got_vis_not = 1;
                        break;
                    case KeyPress:
                        break;
                    case ButtonRelease:
                        break;
                    case ConfigureNotify:
                        self->swidth = evt.xconfigure.width;
                        self->sheight = evt.xconfigure.height;
                        break;
                }
            }
        }
    }
    return error;
}

