#import <Cocoa/Cocoa.h>

static NSWindow* fxsoundWindowForHandle(void* native_handle)
{
    if (native_handle == nullptr)
        return nil;
    NSView* view = (__bridge NSView*) native_handle;
    return [view window];
}

extern "C" void fxsoundMacMiniaturize(void* native_handle)
{
    NSWindow* window = fxsoundWindowForHandle(native_handle);
    if (window != nil)
        [window miniaturize:nil];
}

extern "C" void fxsoundMacDeminiaturize(void* native_handle)
{
    NSWindow* window = fxsoundWindowForHandle(native_handle);
    if (window != nil)
        [window deminiaturize:nil];
}

extern "C" bool fxsoundMacIsMiniaturized(void* native_handle)
{
    NSWindow* window = fxsoundWindowForHandle(native_handle);
    return window != nil && [window isMiniaturized];
}
