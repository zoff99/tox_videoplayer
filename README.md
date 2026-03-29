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


<br>
Any use of this project's code by GitHub Copilot, past or present, is done
without our permission.  We do not consent to GitHub's use of this project's
code in Copilot.
<br>
No part of this work may be used or reproduced in any manner for the purpose of training artificial intelligence technologies or systems.
