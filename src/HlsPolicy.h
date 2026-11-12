// MPL-2.0. Playback startup policy, independent of HTTP and Media Foundation.
#pragma once
#include <algorithm>
#include <cstdint>
#include <limits>

namespace HlsPolicy {
constexpr uint64_t kPreferredBitrate=128000;
constexpr uint64_t kProbeBytes=256*1024;
inline uint64_t Distance(uint64_t rate){
    if(!rate)return std::numeric_limits<uint64_t>::max();
    return rate>=kPreferredBitrate?rate-kPreferredBitrate:kPreferredBitrate-rate;
}
inline bool Prefer(bool audio,uint64_t rate,bool previousAudio,uint64_t previousRate){
    if(audio!=previousAudio)return audio;
    auto a=Distance(rate),b=Distance(previousRate);
    return a!=b?a<b:rate>previousRate;
}
inline double StartupSeconds(double target,uint64_t bitrate){
    // Keep the RFC's three-target-duration safety margin. At low bitrates,
    // retain enough advertised history to satisfy MF's initial 256 KiB read.
    double probe=bitrate?double(kProbeBytes)*8/double(bitrate)+target:90.;
    return std::max(3*target,std::min(90.,probe));
}
}
