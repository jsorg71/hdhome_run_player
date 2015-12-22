
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

static Display* g_disp = 0;
static int g_screenNumber = 0;
static Window g_win = 0;
static long g_eventMask = 0;
static int g_disp_fd = 0;
static int g_visible = 0;
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

    printf("hdhomerun_x11_init:\n");
    g_disp = XOpenDisplay(NULL);
    if (g_disp == 0)
    {
        printf("error opening X display\n");
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
    g_disp_fd = ConnectionNumber(g_disp);

    ret = XvQueryExtension(g_disp, &version, &release, &request_base, &event_base, &error_base);
    if (ret != Success)
    {
        printf("XvQueryExtension failedd\n");
    }
    g_xv_event_base = event_base;
    ret = XvQueryAdaptors(g_disp, DefaultRootWindow(g_disp), &num_adaptors, &ai);
    if (ret != Success)
    {
        printf("XvQueryAdaptors failedd\n");
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

    //printf("hdhome_run_x11_get_buffer:\n");
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
    XShmSegmentInfo shminfo;
    XvImage * image;

    //printf("hdhome_run_x11_show_buffer:\n");
    switch (format)
    {
        //dst_pixfmt = 0x30323449; /* I420 */
        //dst_pixfmt = 0x32595559; /* YUY2 */
        //dst_pixfmt = 0x59565955; /* UYVY */
        case 0:
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
    XvShmPutImage(g_disp, g_xv_port, g_win, g_gc, image, 0, 0,
                  width, height, 0, 0, g_swidth, g_sheight, 0);
    XSync(g_disp, 0);
    XShmDetach(g_disp, &shminfo);
    XFree(image);
    return 0;
}

/*****************************************************************************/
int
hdhome_run_x11_main_loop(int sck, tmlcb cb, void* udata)
{
    XEvent evt;
    fd_set rfds_set;
    int max_fd;
    int status;
    int error;
    int cont;

    printf("hdhome_run_x11_main_loop:\n");
    error = 0;
    cont = 1;
    while (cont)
    {
        max_fd = 0; 
        FD_ZERO(&rfds_set);
        //printf("--%d\n", g_disp_fd);
        FD_SET(g_disp_fd, &rfds_set);
        if (g_disp_fd > max_fd)
        {
            max_fd = g_disp_fd;
        }
        if (g_visible)
        {
            FD_SET(sck, &rfds_set);
            if (sck > max_fd)
            {
                max_fd = sck;
            }
        }
        status = select(max_fd + 1, &rfds_set, NULL, NULL, NULL);
        if (status > 0)
        {
            if (g_visible)
            {
                if (FD_ISSET(sck, &rfds_set))
                {
                    error = cb(sck, udata);
                }
            }
            while (XPending(g_disp) > 0)
            {
                memset(&evt, 0, sizeof(evt));
                XNextEvent(g_disp, &evt);
                switch (evt.type)
                {
                    case VisibilityNotify:
                        g_visible = 1;
                        break;
                    case KeyPress:
                        //cont = 0;
                        break;
                    case ButtonRelease:
                        break;
                    case ConfigureNotify:
                        //printf("ConfigureNotify: width %d height %d\n",
                        //       evt.xconfigure.width, evt.xconfigure.height);
                        g_swidth = evt.xconfigure.width;
                        g_sheight = evt.xconfigure.height;
                        break;
                }
            }
        }
    }
    return error;
}

