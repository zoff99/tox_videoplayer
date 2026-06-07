# Tox Videoplayer

tox_videoplayer is a command-line application that streams video and audio content over the Tox peer-to-peer network.<br>
The application functions as both a media player and a broadcaster, processing media through FFmpeg and transmitting it to a remote peer via the ToxAV audio/video protocol.

[![License: GPL v3](https://img.shields.io/badge/License-GPL%20v3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0.en.html)
[![Liberapay](https://img.shields.io/liberapay/goal/zoff.svg?logo=liberapay)](https://liberapay.com/zoff/donate)
[![Ask DeepWiki](https://deepwiki.com/badge.svg)](https://deepwiki.com/zoff99/tox_videoplayer)

### Key characteristics:

* Decentralized: Uses Tox DHT for peer discovery, no central servers required
* Encrypted: All media transmission is end-to-end encrypted via the Tox protocol
* Multi-source: Supports local video files (MP4, MKV, etc.), HTTP/HTTPS streams, and X11 desktop capture
* Real-time: Multi-threaded architecture for concurrent video/audio processing and network transmission
* Lightweight: Terminal-based interface with minimal dependencies

### Format conversions:

* Video: Any FFmpeg-supported format → YUV420P (required by ToxAV)
* Audio: Any FFmpeg-supported format → 48kHz PCM, 1-2 channels (required by Opus encoder)
* Scaling: Automatic bounding box calculation to Full HD (1920x1080) maximum


### Keyboard Commands

| Key | Function | Description |
|-----|----------|-------------|
| `Space` | Play/Pause | Toggle between playing and paused states  |
| `f` | Seek Forward | Jump forward in media by `seek_delta_ms` |
| `g` | Fast Forward | Jump forward by `seek_delta_ms_faster` (longer seek) |
| `b` | Seek Backward | Jump backward in media by `seek_delta_ms` |
| `a` | Audio Delay | Increase audio delay in 60ms increments (cycles 0-12) |
| `v` | Video Delay | Increase video delay in 50ms increments (cycles 0-10) |
| `c` | Call | Initiate a call to friend 0 |
| `h` | Hang Up | End the current call |
| `o` | OSD Toggle | Toggle on-screen display on/off  |
| `i` | HDMI Frequency | Cycle through HDMI frequencies: 60→24→25→30→50→60 Hz |


### 🚀 Featured Applications

Join a growing community of security-conscious people. Check out these featured applications:

*   **[TRIfA](https://github.com/zoff99/ToxAndroidRefImpl)**: The Tox flagship secure messenger for Android.
*   **[TRIfA for Desktop](https://github.com/Zoxcore/trifa_material)**: The feature rich Tox Desktop Messaging Client.
*   **[Tox Push Msgs](https://github.com/zoff99/tox_push_msg_app)**: The Companion App for TRIfA and TRIfA for Desktop to enable Push Messages.
*   **[ToxProxy](https://github.com/zoff99/ToxProxy)**: Offline message relay functionality for TRIfA and TRIfA for Desktop.
*   **[ToLoShare](https://github.com/zoff99/ToLoShare)**: A specialized Android application for secure, peer-to-peer real-time location sharing.
*   **[ToLoShare for Desktop](https://github.com/zoff99/ToLoShare_material)**: A cross-platform desktop application for secure peer-to-peer real-time location sharing.
*   **[ToFShare](https://github.com/zoff99/ToFShare)**: Secure decentralized file sharing for Android using the Tox protocol.
*   **[tox_videoplayer](https://github.com/zoff99/tox_videoplayer)**: A command-line application that streams video and audio content over the Tox network.
*   **[Tox Kodi video addon](https://github.com/zoff99/kodi_tox_plugin)**: Kodi add-on for streaming video from a Tox client.



<br>
Any use of this project's code by GitHub Copilot, past or present, is done
without our permission.  We do not consent to GitHub's use of this project's
code in Copilot.
<br>
No part of this work may be used or reproduced in any manner for the purpose of training artificial intelligence technologies or systems.
