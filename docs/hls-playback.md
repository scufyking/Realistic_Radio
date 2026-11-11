# Native HLS playback

## Reproduction and cause

On the tested Windows 11 installation with ETS2 1.62.2.1s and the released plugin 1.7.0, select an Italian station using an original HLS URL, for example Rai Radio Tutta Italiana, Rai Isoradio or Radio Italia. Disable URL overrides when reproducing. The previous path passed the M3U8 URL directly to `MFCreateSourceReaderFromURL`; it failed with `0xc00d36c4` before decoding audio. The same streams played in VLC. Changing network routing did not fix this source-resolver failure.

This is a failure of the tested Media Foundation Source Reader URL-opening path, not evidence that the audio codec or every Windows playback API lacks HLS support. Native Media Foundation successfully decoded downloaded AAC and MPEG-TS fragments on the same computer.

## Change

`HlsInput` fetches master/media playlists and fragments through WinHTTP. It resolves relative URLs against the final redirected playlist URL, selects an audio rendition, refreshes the live window, checks sequence continuity, removes leading packed-audio ID3 headers and feeds one persistent Source Reader through `IMFByteStream`. Ordinary fragment boundaries do not recreate the decoder.

The byte stream has a bounded 4 MiB data buffer, retaining up to 1 MiB of history for decoder probing. A single HTTP response is separately limited to 4 MiB. IMFAttributes and IMFByteStream share one COM identity. Reads wait for data; temporary starvation does not report end-of-stream. Cancellation wakes readers and stops the current HTTP request. Existing resampling, PCM buffering, mixing and FMOD output remain in place.

Supported: unencrypted ADTS AAC, MP3 and MPEG-TS in one fixed rendition. Live AAC and TS were tested; live MP3-HLS was not. Encryption and fMP4/EXT-X-MAP fail explicitly. A sequence gap, discontinuity or container change ends the current decoder and invokes the normal reconnect loop. Seamless discontinuity handling, adaptive rendition changes and HLS song-title extraction are not implemented.

The checked-in CMake source list also omitted existing GameAudio, CardStyles, Phone and Speech implementations; they are included so that this checkout can link without removing those features. Explicit standard-library includes fix compilation with LLVM-MinGW.

## Validation

- Native C++ transport/MF test: original Rai Tutta Italiana, Rai Isoradio and Radio Italia URLs each decoded over 65 seconds of nonzero stereo float PCM at 48 kHz. No timestamp gaps above 50 ms were detected. Cancellation completed in approximately 4–12 ms. PCM duration is not wall-clock listening duration.
- Actual `StreamSession` test: cancellation during opening, cancellation while the PCM buffer is full, and three successive sessions in one process passed. Each consumed session delivered over eight seconds through the existing PCM ring. Stops completed in 15–47 ms.
- DLL loaded and unloaded successfully with Windows; telemetry exports were present.
- The user tested the HLS build inside ETS2 in Italy and reported that previously failing stations now play. The game log additionally confirms Deejay 30 Songs (MPEG-TS) reaching `Media Foundation playing ... (48000 Hz, 2 ch)` with the original M3U8 URL. This is not a claim that every HLS station is supported.

Build standalone native diagnostics on Windows:

```powershell
cmake -S . -B build -A x64 -DRR_BUILD_HLS_PROBE=ON -DRR_BUILD_LOCALIZATION_PROBE=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
build\Release\hls-probe.exe "https://radiotuttaitaliana-live.akamaized.net/hls/live/2032596/radiotuttaitaliana/radiotuttaitaliana/playlist.m3u8"
```

HLS probing performs network requests and has a watchdog. It does not play audio on speakers. The local validation used a portable LLVM-MinGW build; the CMake diagnostic targets have not themselves been exercised in that toolchain.

## Russian localization and release/source mismatch

The released 1.7.0 DLL advertises Russian localization, but this checkout still exposes only DE/EN and does not contain the release's localization implementation. Rebuilding it lost the Russian language option. `Localization` restores the built-in Russian option using the 450-entry community translation from the Russian example file distributed by that release, credited in `RussianTranslations.inc`. English/German remain selectable and missing translations fall back to English. This does not implement the release's full external-language/plugin-add-on system.

German (`menuLanguage: "de"`) remains the default for a fresh installation. This change does not force Russian or migrate the user's selected language. Russian is an additional selectable option, as it was in the installed release DLL.

Translation is applied to known UI labels and literal choices, not indiscriminately to all drawn strings, so station names and user-entered text are not replaced globally. SAPI selects Russian language 419 when available. The native regression test verifies Russian selection, menu labels, all page views, a phone label, missing-text fallback and switching back to English/German; it also renders `russian-menu.bmp` for visual inspection. This restoration still needs a user check inside ETS2. The HLS version was already verified there before this localization change.
