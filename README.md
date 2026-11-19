# GHOST MIRROR

**AirPlay Screen Mirroring Receiver for Windows (Ultra-Low Latency D3D11 + WASAPI)**  
*FAST · EDITION*

![Ghost Mirror Logo](assets/ghost_preview.png)

## Features
- **Zero-Bloat Native Win32 + Direct2D Architectural Pixel UI**: Inspired by Awwwards Nominee *Yoyu Design* — crisp drafting graph-paper canvas with **Light Paper & Dark Obsidian Blueprint** toggle (`T` or header icon), animated 16×16 pixel Ghost mascot with cursor-tracking eyes, hard pixel offset hover shadows (`160ms`), Per-Monitor DPI V2 sharpness, and a header **`MOTION`** toggle (drops idle CPU/GPU to 0% when static or minimized).
- **Direct Segmented Controls**:
  - **Quick Presets**: One-click `1080p LOW-LAT`, `1440p SHARP`, `4K H.265` (with automatic `CUSTOM TUNING` detection).
  - **Direct Pickers**: 4-way Resolution (`720p` | `1080p` | `1440p` | `4K UHD`), 3-way Frame Rate (`30` | `60` | `120 HZ`), 2-way Codec (`H.264` | `H.265`), and one-click `DISPLAY`, `VSYNC`, `PIN LOCK`, `AUDIO` switches with smart `420ms` debounced engine apply.
- **Realtime Connection Sync & Custom Display Chrome**: `<150ms` live connect/disconnect detection synchronized with UxPlay logs + the hardware D3D11 mirroring window, automatically skinned with the Ghost Mirror title, Yoyu Pixel Ghost icon, sharp corners, and `#111111` ink titlebar.
- **Real-Time 20-Segment Pixel WASAPI Fader**: Smooth `0% – 100%` event-driven per-process audio fader + instant mute (`M`) without restarting the AirPlay stream.
- **Hardware-Accelerated Pure Screen Mirroring**: Powered by a customized UxPlay 1.74 + GStreamer `d3d11videosink` pipeline with `-vsync no` for sub-frame latency and patched feature bits to prevent unwanted HLS video-cast hijacks.

---

## Quick Start (Portable Release)
1. Download `GhostMirror-Portable-v2.0.0.zip` from **Releases** and extract it anywhere.
2. Run **`GhostMirror.exe`**.
3. Connect your iPhone / iPad / Mac to the **same Wi-Fi / LAN** as your PC.
4. Open **Control Center** -> **Screen Mirroring** -> Tap **`Ghost`**.

---

## Building from Source
### Prerequisites
- **Visual Studio 2022 Build Tools** (MSVC `cl.exe` & `rc.exe`)
- **MSYS2 UCRT64** (`C:\msys64\ucrt64`) with `mingw-w64-ucrt-x86_64-gstreamer`, `gst-plugins-base`, `gst-plugins-good`, `gst-plugins-bad`, `gst-libav`, `openssl`, `libplist`.

### Build Commands
```bat
:: Build GhostMirror.exe UI + run self-tests
build.bat
```

## License
Released under the **GNU General Public License v3.0 (GPL-3.0)** (includes modified [UxPlay](https://github.com/FDH2/UxPlay) engine under GPLv3).
