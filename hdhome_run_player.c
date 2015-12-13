
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <hdhomerun.h>

#include "mpeg_ts.h"

int main(int argc, char** argv)
{
    struct hdhomerun_device_t* hdhr;
    struct hdhomerun_video_sock_t* hdhr_vsck;
    hdhomerun_sock_t hdhr_sck;
    const char* dev_name;
    int error;
    //int fd;
    size_t bytes;
    uint8_t* data;
    fd_set rfds;

    //fd = open("/tmp/video.ts", O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
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
                    //printf("got data size %d\n", (int)bytes);
                    //hex_dump(data, 64);
                    //if (write(fd, data, bytes) != bytes)
                    //{
                    //    printf("error write\n");
                    //}
                    error = process_mpeg_ts_packet(data, bytes);
                    //printf("process_mpeg_ts_packet rv %d\n", error);

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
    //close(fd);
    return 0;
}
