# OBS YTMDesktop Sync (`obs-ytmdesktop`)

A native C++/Qt6 OBS Studio plugin that connects directly to the **YouTube Music Desktop App (YTMDesktop v2+)** Companion Server and renders a customizable **Now Playing** overlay card inside OBS Studio.

## Why `obs-ytmdesktop`?

- **Native OBS Source** — renders directly to a hardware-composited RGBA texture (`gs_texture_t`) without Chromium Browser Source overhead or writing temporary text/image files to disk.
- **Dedicated to YTMDesktop v2** — talks directly to YTMDesktop's Companion Server API (`http://127.0.0.1:9863/api/v1`) instead of relying on Windows SMTC (which can accidentally pick up audio from browser tabs or other media players).
- **Rate-Limit Aware & Smooth** — polls `/api/v1/state` on a background `QThread` with automatic HTTP `429` backoff, while interpolating the progress bar smoothly at 60 FPS.
- **Native HTTPS Album Art** — fetches album art via Windows `WinHTTP` with a 5 MB download cap and strict HTTP/HTTPS scheme validation (no external OpenSSL or Qt TLS plugin required).
- **One-Click Pairing** — built-in **Pair with YTMDesktop** button handles the `/api/v1/auth/requestcode` + `/api/v1/auth/request` handshake and stores the token in your OBS scene collection.

## Prerequisites

1. **OBS Studio** (31.x+ on Windows x64)
2. **YouTube Music Desktop App (YTMDesktop v2.0.0+)**
   - Open **Settings → Integrations**
   - Enable **Companion Server** (default port `9863`)
   - *(Recommended)* Enable **Companion Authorization**

## Installation (Windows)

Copy the built plugin files into OBS Studio's system plugin directory (`C:\ProgramData\obs-studio\plugins\obs-ytmdesktop\`):

- `obs-ytmdesktop.dll` → `C:\ProgramData\obs-studio\plugins\obs-ytmdesktop\bin\64bit\obs-ytmdesktop.dll`
- `data/locale/en-US.ini` → `C:\ProgramData\obs-studio\plugins\obs-ytmdesktop\data\locale\en-US.ini`

Restart OBS Studio after copying the files.

## Usage in OBS Studio

1. In your Scene's **Sources** dock, click **+ → YTMDesktop Now Playing**.
2. Click **Pair with YTMDesktop** in the source properties.
3. Approve the pairing popup inside YTMDesktop (matching the displayed code).
4. Customize the card dimensions, colors, font size, album art, progress bar, and visibility options.

### Source Properties

| Property | Description |
| --- | --- |
| **YTMDesktop Host** | Hostname or IP of YTMDesktop (default `127.0.0.1`) |
| **Companion Server Port** | Companion Server port (default `9863`) |
| **API Token** | Stored automatically after pairing |
| **Pair with YTMDesktop** | Initiates the Companion Server pairing handshake |
| **Width / Height** | Dimensions of the rendered card in pixels |
| **Show Album Art** | Toggle album artwork thumbnail |
| **Show Progress Bar** | Toggle progress bar and `elapsed / total` timer |
| **Hide When Paused / Disconnected** | Automatically hides the source when playback stops |
| **Background Color / Opacity** | Card background color and alpha transparency |
| **Accent / Text / Subtext Colors** | Progress bar fill and typography colors |
| **Title Font Size / Corner Radius** | Typography scale and rounded-corner radius |

## Building from Source (Windows x64)

### Requirements
- Visual Studio 2022 (Desktop development with C++)
- CMake 3.28+

### Build Commands
```powershell
cmake --preset windows-x64
cmake --build --preset windows-x64 --config RelWithDebInfo
```

The compiled DLL will be output to `build_x64\RelWithDebInfo\obs-ytmdesktop.dll`.

## License

GPL-2.0 — see [LICENSE](LICENSE).
