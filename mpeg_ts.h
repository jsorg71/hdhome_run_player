
#ifndef _MPEG_TS_H_
#define _MPEG_TS_H_

struct tmpegts;

typedef int (*tmpegts_cb_proc)(const void* data, int data_bytes, const struct tmpegts* mpegts, void* udata);

struct tmpegts_cb
{
    int pids[32];
    tmpegts_cb_proc procs[32];
};

struct tmpegts
{
    int sync_byte;
    int transport_error_indicator; /* boolean */
    int payload_unit_start_indicator; /* boolean */
    int transport_priority; /* boolean */
    int pid;
    int scrambling_control;
    int adaptation_field_flag; /* boolean */
    int payload_flag; /* boolean */
    int continuity_counter;

    int adaptation_field_length;
    int discontinuity_indicator;
    int random_access_indicator;
    int elementary_stream_priority_indicator;
    int pcr_flag;
    int opcr_flag;
    int splicing_point_flag;
    int transport_private_data_flag;
    int adaptation_field_extension_flag;

    unsigned char pcr[6];

};

int get_mstime(void);
int hex_dump(const void* data, int bytes);
int process_mpeg_ts_packet(const void* data, int bytes, const struct tmpegts_cb* cb, void* udata);

#endif
