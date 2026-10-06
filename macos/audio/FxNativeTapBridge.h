#pragma once
#include <CoreAudio/CoreAudio.h>
#include <cstddef>

#ifdef __cplusplus
extern "C" {
#endif

bool fxsoundNativeTapStart(AudioDeviceIOProc callback,
                           void* client_data,
                           AudioObjectID* out_device,
                           AudioDeviceIOProcID* out_proc,
                           void** out_handle,
                           char* error_buffer,
                           size_t error_buffer_size);

void fxsoundNativeTapStop(void* handle);

#ifdef __cplusplus
}
#endif
