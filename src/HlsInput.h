// HLS test implementation. MPL-2.0, like the surrounding project.
#pragma once
#include "Common.h"
#include <mfobjects.h>
#include <memory>

// Owns a cancellable WinHTTP loader and a bounded, rewindable MF byte stream.
// The owner must Cancel/Join before MFShutdown and before unloading the DLL.
class HlsInput {
public:
    explicit HlsInput(std::string url);
    ~HlsInput();
    void Start();
    bool WaitReady();
    IMFByteStream* Stream() const;
    void Cancel();
    void Join();
private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};
