
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mpeg_ts.h"

int hex_dump(const void* data, int bytes)
{
    const unsigned char *line;
    int i;
    int thisline;
    int offset;

    line = (const unsigned char *)data;
    offset = 0;
    while (offset < bytes)
    {
        printf("%04x ", offset);
        thisline = bytes - offset;
        if (thisline > 16)
        {
            thisline = 16;
        }
        for (i = 0; i < thisline; i++)
        {
            printf("%02x ", line[i]);
        }
        for (; i < 16; i++)
        {
            printf("   ");
        }
        for (i = 0; i < thisline; i++)
        {
            printf("%c", (line[i] >= 0x20 && line[i] < 0x7f) ? line[i] : '.');
        }
        printf("\n");
        offset += thisline;
        line += thisline;
    }
    return 0;
}

int process_mpeg_ts_packet(const void* data, int bytes, const struct tmpegts_cb* cb, void* udata)
{
    unsigned int header;
    struct tmpegts mpegts;
    const unsigned char* data8;
    const unsigned char* data8_end;
    int cb_bytes;
    int index;

    memset(&mpegts, 0, sizeof(mpegts));
    data8 = (const unsigned char*) data;
    data8_end = data8 + bytes;
    header = (data8[0] << 24) | (data8[1] << 16) | (data8[2] << 8) | data8[3];
    data8 += 4;
    mpegts.sync_byte = (header & 0xff000000) >> 24;
    mpegts.transport_error_indicator = (header & 0x800000) != 0;
    mpegts.payload_unit_start_indicator = (header & 0x400000) != 0;
    mpegts.transport_priority = (header & 0x200000) != 0;
    mpegts.pid = (header & 0x1fff00) >> 8;
    mpegts.scrambling_control = (header & 0xc0) >> 6;
    mpegts.adaptation_field_flag = (header & 0x20) != 0;
    mpegts.payload_flag = (header & 0x10) != 0;
    mpegts.continuity_counter = header & 0xf;
    if (mpegts.sync_byte != 0x47)
    {
        /* must be parse error */
        return 1;
    }
    if (mpegts.scrambling_control != 0)
    {
        /* not supported */
        return 1;
    }
    if (mpegts.adaptation_field_flag)
    {
        mpegts.adaptation_field_length = data8[0];
        data8++;
        header = data8[0];
        data8 += mpegts.adaptation_field_length;
        mpegts.discontinuity_indicator = (header & 0x80) != 0;
        mpegts.random_access_indicator = (header & 0x40) != 0;
        mpegts.elementary_stream_priority_indicator = (header & 0x20) != 0;
        mpegts.pcr_flag = (header & 0x10) != 0;
        mpegts.opcr_flag = (header & 0x08) != 0;
        mpegts.splicing_point_flag = (header & 0x04) != 0;
        mpegts.transport_private_data_flag = (header & 0x02) != 0;
        mpegts.adaptation_field_extension_flag = (header & 0x01) != 0;
        if (mpegts.random_access_indicator)
        {
            //printf("random_access_indicator set\n");
        }
        if (mpegts.pcr_flag)
        {
            /* 48 bit */
        }
        if (mpegts.opcr_flag)
        {
            /* 48 bit */
        }
        if (mpegts.splicing_point_flag)
        {
            printf("splicing_point_flag set\n");
            //return 1;
        }
        if (mpegts.transport_private_data_flag)
        {
            printf("transport_private_data_flag set\n");
            //return 1;
        }
        if (mpegts.transport_private_data_flag)
        {
            printf("transport_private_data_flag set\n");
            //return 1;
        }
        if (mpegts.adaptation_field_extension_flag)
        {
            printf("adaptation_field_extension_flag set\n");
            //return 1;
        }
    }

    //printf("pid 0x%4.4x\n", mpegts.pid);
    cb_bytes = (int) (data8_end - data8);
    if ((cb != 0) && (cb_bytes > 0) && mpegts.payload_flag)
    {
        index = 0;
        while ((cb->pids[index] != 0) && (index < 32))
        {
            if (cb->pids[index] == mpegts.pid)
            {
                if (cb->procs[index] != 0)
                {
                    if ((cb->procs[index])(data8, cb_bytes, &mpegts, udata) != 0)
                    {
                        //return 1;
                    }
                }
            }
            index++;
        }
    }

    return 0;
}
