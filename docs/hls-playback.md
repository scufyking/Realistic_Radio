# Native HLS playback and startup

## Reproduce

On the tested Windows 11 / ETS2 1.62.2.1s installation with the released Realistic Radio 1.7.0 DLL:

1. Load a save in Italy; disable custom URL overrides.
2. Select Rai Radio Tutta Italiana, Rai Isoradio or Radio Italia using their original HLS URLs.
3. The original code calls `MFCreateSourceReaderFromURL` with the M3U8 URL and logs `Media Foundation cannot open ...playlist.m3u8 (hr=0xc00d36c4)`, before audio reaches the mixer. The same URLs work in VLC.

Example original URL:
`https://radiotuttaitaliana-live.akamaized.net/hls/live/2032596/radiotuttaitaliana/radiotuttaitaliana/playlist.m3u8`

Changing network routing did not fix this Source Reader opening failure. Native Media Foundation did decode downloaded AAC/MPEG-TS fragments on the same computer. This concerns that particular URL-opening API path, not every Windows playback API or HLS stream.

## Implementation

`HlsInput` downloads master/media playlists and ordered fragments with cancellable WinHTTP requests. It resolves relative URLs against redirected playlist URLs, selects audio near 128 kbit/s, refreshes the live window, checks sequence continuity, supports byte ranges/retries and strips leading packed-audio ID3 headers. One persistent Media Foundation Source Reader reads an `IMFByteStream`, preserving decoder state across ordinary fragments.

Input buffering is limited to 4 MiB, retaining up to 1 MiB for decoder probe seeks. A single HTTP response is separately limited to 4 MiB. IMFAttributes/IMFByteStream share one COM identity. Reads wait for data rather than treating temporary starvation as EOF; cancellation aborts network requests and wakes reads. Existing PCM conversion, ring buffer, effects, volume and FMOD output are retained. Switching cancellation no longer logs a misleading open failure.

The first implementation chose the lowest rendition. At 32 kbit/s, three six-second fragments contain too little data for the observed approximately 256 KiB MF probe read, causing tens of seconds of silence. Startup now prefers a rendition near 128 kbit/s and requests sufficient advertised history for that probe. Additional history is capped at 90 seconds, while preserving at least three target durations. Direct media playlists estimate bitrate from one reused fragment. This trades live-edge latency for faster startup; very short advertised windows may still require waiting.

Supported: unencrypted ADTS AAC, MP3 and MPEG-TS in a fixed rendition. Live AAC/TS were tested. Encryption and fMP4/EXT-X-MAP fail explicitly. Sequence gaps, discontinuities or container changes end the current reader and use the existing reconnect loop. Adaptive quality, seamless discontinuity handling and HLS song-title extraction are not implemented.

Existing GameAudio/CardStyles/Phone/Speech files missing from CMake are included so the published checkout links. Explicit standard includes fix LLVM-MinGW compilation. Localization and station URL overrides are outside this change.

## Validation

- Original Rai Tutta Italiana, Isoradio and Radio Italia: >65 seconds each of native nonzero stereo PCM. No timestamp gaps >50 ms in these tests.
- Native before/after first-PCM startup: Detskoe Radio 42.75 → 0.45 s; Dorozhnoe FM 36.46 → 0.33 s. These are individual measurements, not network-independent guarantees.
- Updated Detskoe: >65 seconds PCM; ten lifecycle tests across actual Detskoe/Dorozhnoe `StreamSession` instances passed opening cancellation, full-buffer stop and repeated sessions.
- A strict timestamp assertion flags an approximately 70 ms jump for Detskoe. An ordinary local AAC file reproduces the exact same expected/actual timestamps and bit-identical PCM to byte-stream decoding. That assertion is not presented as passed. Forced 32 kbit/s HE-AAC has a separate timestamp anomaly requiring further investigation.
- The user confirmed previously failing Italian stations play inside ETS2. Game logs confirm Deejay 30 Songs decoding MPEG-TS. A later game log shows Avtoradio playing for roughly nine minutes without logged reconnects/errors and Comedy Radio opening successfully; first decoder-ready logs followed HLS loading by approximately 0.37/0.26 s.
- No speaker output is used by standalone probes. PCM duration is not wall-clock listening duration. HTTP 12029 is a separate connection failure and is not fixed here.

Optional native diagnostics (Windows):

```powershell
cmake -S . -B build -A x64 -DRR_BUILD_HLS_PROBE=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
build\Release\hls-probe.exe "https://radiotuttaitaliana-live.akamaized.net/hls/live/2032596/radiotuttaitaliana/radiotuttaitaliana/playlist.m3u8"
```

The local validation used portable LLVM-MinGW and native WinHTTP/MF APIs. No FFmpeg/VLC, external player process or loopback proxy is required by the plugin. CMake diagnostic targets are provided; the recorded original measurements used a direct portable compiler build.
