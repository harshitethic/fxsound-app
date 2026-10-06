#import "FxNativeTapBridge.h"

#import <Foundation/Foundation.h>
#import <CoreAudio/AudioHardware.h>
#import <CoreAudio/AudioHardwareTapping.h>
#import <CoreAudio/CATapDescription.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>
#include <unistd.h>

namespace {

struct NativeTapHandle {
    AudioObjectID tap{kAudioObjectUnknown};
    AudioObjectID aggregate{kAudioObjectUnknown};
    AudioDeviceIOProcID proc{nullptr};
    dispatch_queue_t queue{nullptr};
};

void setError(char* buffer, size_t size, const char* text, OSStatus status = noErr)
{
    if (!buffer || size == 0) return;
    if (status == noErr)
        std::snprintf(buffer, size, "%s", text);
    else
        std::snprintf(buffer, size, "%s (OSStatus %d)", text, static_cast<int>(status));
}

AudioObjectID processObjectForPID(pid_t pid)
{
    AudioObjectPropertyAddress address{
        kAudioHardwarePropertyTranslatePIDToProcessObject,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };
    AudioObjectID object = kAudioObjectUnknown;
    UInt32 size = sizeof(object);
    const OSStatus status = AudioObjectGetPropertyData(
        kAudioObjectSystemObject,
        &address,
        sizeof(pid),
        &pid,
        &size,
        &object);
    return status == noErr ? object : kAudioObjectUnknown;
}

UInt32 inputChannels(AudioObjectID device)
{
    AudioObjectPropertyAddress address{
        kAudioDevicePropertyStreamConfiguration,
        kAudioObjectPropertyScopeInput,
        kAudioObjectPropertyElementMain
    };
    UInt32 size = 0;
    if (AudioObjectGetPropertyDataSize(device, &address, 0, nullptr, &size) != noErr || size == 0)
        return 0;

    std::unique_ptr<unsigned char[]> memory(new unsigned char[size]);
    auto* list = reinterpret_cast<AudioBufferList*>(memory.get());
    if (AudioObjectGetPropertyData(device, &address, 0, nullptr, &size, list) != noErr)
        return 0;

    UInt32 channels = 0;
    for (UInt32 i = 0; i < list->mNumberBuffers; ++i)
        channels += list->mBuffers[i].mNumberChannels;
    return channels;
}

void cleanup(NativeTapHandle* handle)
{
    if (!handle) return;

    if (handle->aggregate != kAudioObjectUnknown && handle->proc) {
        AudioDeviceStop(handle->aggregate, handle->proc);
        AudioDeviceDestroyIOProcID(handle->aggregate, handle->proc);
        handle->proc = nullptr;
    }

    if (handle->aggregate != kAudioObjectUnknown) {
        AudioHardwareDestroyAggregateDevice(handle->aggregate);
        handle->aggregate = kAudioObjectUnknown;
    }

    if (handle->tap != kAudioObjectUnknown) {
        AudioHardwareDestroyProcessTap(handle->tap);
        handle->tap = kAudioObjectUnknown;
    }
}

} // namespace

extern "C" bool fxsoundNativeTapStart(AudioDeviceIOProc callback,
                                        void* client_data,
                                        AudioObjectID* out_device,
                                        AudioDeviceIOProcID* out_proc,
                                        void** out_handle,
                                        char* error_buffer,
                                        size_t error_buffer_size)
{
    if (out_device) *out_device = kAudioObjectUnknown;
    if (out_proc) *out_proc = nullptr;
    if (out_handle) *out_handle = nullptr;
    if (error_buffer && error_buffer_size) error_buffer[0] = '\0';

    if (!callback || !out_device || !out_proc || !out_handle) {
        setError(error_buffer, error_buffer_size, "invalid native tap arguments");
        return false;
    }

    if (@available(macOS 14.2, *)) {
        @autoreleasepool {
            auto handle = std::make_unique<NativeTapHandle>();

            const AudioObjectID self_process = processObjectForPID(getpid());
            NSArray<NSNumber*>* excluded =
                self_process == kAudioObjectUnknown ? @[] : @[ @(self_process) ];

            CATapDescription* description =
                [[CATapDescription alloc] initStereoGlobalTapButExcludeProcesses:excluded];
            description.name = @"FxSound System Audio Tap";
            description.UUID = [NSUUID UUID];
            description.privateTap = YES;
            description.muteBehavior = CATapMutedWhenTapped;

            OSStatus status = AudioHardwareCreateProcessTap(description, &handle->tap);
            if (status != noErr) {
                setError(error_buffer, error_buffer_size, "could not create CoreAudio process tap", status);
                cleanup(handle.get());
                return false;
            }

            NSDictionary* tap_entry = @{
                @kAudioSubTapUIDKey: description.UUID.UUIDString
            };
            NSDictionary* composition = @{
                @kAudioAggregateDeviceNameKey: @"FxSound Private System Tap",
                @kAudioAggregateDeviceUIDKey: [[NSUUID UUID] UUIDString],
                @kAudioAggregateDeviceIsPrivateKey: @YES,
                @kAudioAggregateDeviceTapListKey: @[ tap_entry ]
            };

            status = AudioHardwareCreateAggregateDevice(
                (__bridge CFDictionaryRef)composition, &handle->aggregate);
            if (status != noErr) {
                setError(error_buffer, error_buffer_size, "could not create private tap aggregate", status);
                cleanup(handle.get());
                return false;
            }

            // Aggregate publication is asynchronous inside the HAL. Wait briefly
            // for its stereo input stream to become visible before registering IO.
            UInt32 channels = 0;
            for (int attempt = 0; attempt < 20; ++attempt) {
                channels = inputChannels(handle->aggregate);
                if (channels >= 2) break;
                usleep(25000);
            }
            if (channels < 2) {
                setError(error_buffer, error_buffer_size, "native tap aggregate has no stereo input");
                cleanup(handle.get());
                return false;
            }

            usleep(100000);

            // macOS 26 can block indefinitely in the legacy
            // AudioDeviceCreateIOProcID path for process-tap aggregates.
            // Apple's current tap implementations use the block API with a
            // non-null queue, which remains reliable across HAL restarts.
            handle->queue = dispatch_queue_create(
                "com.harshitethic.fxsound.system-tap",
                DISPATCH_QUEUE_SERIAL);

            const AudioObjectID aggregate = handle->aggregate;
            AudioDeviceIOProc client_callback = callback;
            void* context = client_data;
            status = AudioDeviceCreateIOProcIDWithBlock(
                &handle->proc,
                handle->aggregate,
                handle->queue,
                ^(const AudioTimeStamp* now,
                  const AudioBufferList* input,
                  const AudioTimeStamp* input_time,
                  AudioBufferList* output,
                  const AudioTimeStamp* output_time) {
                    client_callback(
                        aggregate,
                        now,
                        input,
                        input_time,
                        output,
                        output_time,
                        context);
                });
            if (status != noErr || !handle->proc) {
                setError(error_buffer, error_buffer_size, "could not create native tap block IOProc", status);
                cleanup(handle.get());
                return false;
            }

            status = AudioDeviceStart(handle->aggregate, handle->proc);
            if (status != noErr) {
                setError(error_buffer, error_buffer_size, "could not start native system tap", status);
                cleanup(handle.get());
                return false;
            }

            *out_device = handle->aggregate;
            *out_proc = handle->proc;
            *out_handle = handle.release();
            return true;
        }
    }

    setError(error_buffer, error_buffer_size, "native CoreAudio system tap requires macOS 14.2 or later");
    return false;
}

extern "C" void fxsoundNativeTapStop(void* opaque)
{
    auto* handle = static_cast<NativeTapHandle*>(opaque);
    cleanup(handle);
    delete handle;
}
