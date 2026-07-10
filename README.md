# FFmpeg README

FFmpeg is a collection of libraries and tools to process multimedia content
such as audio, video, subtitles and related metadata.

## Libraries

* `libavcodec` provides implementation of a wider range of codecs.
* `libavformat` implements streaming protocols, container formats and basic I/O access.
* `libavutil` includes hashers, decompressors and miscellaneous utility functions.
* `libavfilter` provides means to alter decoded audio and video through a directed graph of connected filters.
* `libavdevice` provides an abstraction to access capture and playback devices.
* `libswresample` implements audio mixing and resampling routines.
* `libswscale` implements color conversion and scaling routines.

## Tools

* [ffmpeg](https://ffmpeg.org/ffmpeg.html) is a command line toolbox to
  manipulate, convert and stream multimedia content.
* [ffplay](https://ffmpeg.org/ffplay.html) is a minimalistic multimedia player.
* [ffprobe](https://ffmpeg.org/ffprobe.html) is a simple analysis tool to inspect
  multimedia content.
* Additional small tools such as `aviocat`, `ismindex` and `qt-faststart`.

## Documentation

The offline documentation is available in the **doc/** directory.

The online documentation is available in the main [website](https://ffmpeg.org)
and in the [wiki](https://trac.ffmpeg.org).

### Examples

Coding examples are available in the **doc/examples** directory.

## License

FFmpeg codebase is mainly LGPL-licensed with optional components licensed under
GPL. Please refer to the LICENSE file for detailed information.

## Contributing

Patches should be submitted to the ffmpeg-devel mailing list using
`git format-patch` or `git send-email`. Github pull requests should be
avoided because they are not part of our review process and will be ignored.
# Android MediaCodec FFmpeg Fork For Jellyfin Android Transcoder

This public fork carries the FFmpeg patches used by the Android worker app in:

```text
https://github.com/doctorpangloss/jellyfin-android-transcoder
```

Related repositories:

- Android worker + Jellyfin plugin: https://github.com/doctorpangloss/jellyfin-android-transcoder
- Patched FFmpeg fork: https://github.com/doctorpangloss/forks-ffmpeg-android
- Integration tests: https://github.com/doctorpangloss/jellyfin-android-transcoder-integration

The active branch is:

```text
mediacodec-surface-hwframes
```

The Android app release does not require end users to build this repository. The `jellyfin-android-transcoder` APK/AAB already includes `libffmpeg.so` for `arm64-v8a`, `armeabi-v7a`, `x86`, and `x86_64`.

The FFmpeg revision used by the verified `jellyfin-android-transcoder` `v1.1.13` release is recorded by the component repository and integration-test submodules. End users should install that release rather than copying a binary from this source tree.

Agents rebuilding FFmpeg should run from the component repository:

```bash
cd /path/to/jellyfin-android-transcoder
FFMPEG_SRC=/path/to/forks-ffmpeg-android \
ANDROID_NDK_ROOT=/path/to/android-ndk-r27d \
./scripts/build-android-ffmpeg.sh
```

That script builds static FFmpeg executables packaged as Android native libraries at:

```text
android-transcoder/app/src/main/jniLibs/<abi>/libffmpeg.so
```

The build uses Android MediaCodec/GLES patches for surface-backed decode/encode and links with 16 KB page-size flags for modern Android devices.

---
