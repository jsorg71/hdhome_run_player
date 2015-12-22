
#ifndef _HDHOME_RUN_PA_H
#define _HDHOME_RUN_PA_H

#ifdef __cplusplus
extern "C"
{
#endif

#define CAP_PA_FORMAT_48000_2CH_16LE 1
#define CAP_PA_FORMAT_48000_6CH_16LE 2

void*
hdhome_run_pa_init(const char* name);
int
hdhome_run_pa_deinit(void* handle);
int
hdhome_run_pa_start(void* handle, const char* name, int ms_latency, int format);
int
hdhome_run_pa_stop(void* handle);
int
hdhome_run_pa_play(void* handle, void* data, int data_bytes);
int
hdhome_run_pa_play_non_blocking(void* handle, void* data, int* data_bytes);

#ifdef __cplusplus
}
#endif

#endif

