
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

int process_mpeg_ts_packet(const void* data, int bytes)
{
    int header;
    int sync_byte;
    int transport_error_indicator; /* boolean */
    int payload_unit_start_indicator; /* boolean */
    int transport_priority; /* boolean */
    int pid;
    int scrambling_control;
    int adaptation_field_flag; /* boolean */
    int payload_flag; /* boolean */
    int continuity_counter;
    const unsigned char* data8;

    data8 = (const unsigned char*) data;
    header = (data8[0] << 24) | (data8[1] << 16) | (data8[2] << 8) | data8[3];
    sync_byte = (header & 0xff000000) >> 24;
    transport_error_indicator = (header & 0x800000) != 0;
    payload_unit_start_indicator = (header & 0x400000) != 0;
    transport_priority = (header & 0x200000) != 0;
    pid = (header & 0x1fff00) >> 8;
    scrambling_control = (header & 0xc0) >> 6;
    adaptation_field_flag = (header & 0x20) != 0;
    payload_flag = (header & 0x10) != 0;
    continuity_counter = header & 0xf;
    printf("sync_byte 0x%2.2x transport_error_indicator %d "
           "payload_unit_start_indicator %d transport_priority %d "
           "pid 0x%4.4x scrambling_control 0x%2.2x adaptation_field_flag %d "
           "payload_flag %d continuity_counter %d\n",
           sync_byte, transport_error_indicator, payload_unit_start_indicator,
           transport_priority, pid, scrambling_control, adaptation_field_flag,
           payload_flag, continuity_counter);
    if (sync_byte != 0x47)
    {
        return 1;
    }
    if (scrambling_control != 0)
    {
        return 1;
    }
    if (pid != 0x31 && pid != 0x34 && pid != 0x35)
    {
        //printf("pid 0x%2.2x\n", pid); 
    }
    if (pid == 0)
    {
        //printf("PAT\n");
        //hex_dump(data, bytes);
    }
    if (payload_unit_start_indicator)
    {
        hex_dump(data, 64);
    }
    return 0; 
}
