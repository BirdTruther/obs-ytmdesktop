# OBS YTMDesktop Sync (`obs-ytmdesktop`)

A native C++/Qt6 OBS Studio plugin that connects to the **YouTube Music Desktop App (YTMDesktop v2+)** Companion Server and renders a customizable **Now Playing** card directly inside OBS.

## Features

- **Native OBS Video Source** (`YTMDesktop Now Playing`) — hardware-composited RGBA texture with crisp text and rounded corners.
- **Real-time Sync** — polls the YTMDesktop Companion Server API (`http://127.0.0.1:9863/api/v1/state`) on a background thread with smooth progress bar interpolation.
- **One-Click Pairing** — built-in **Pair with YTMDesktop** button in the source properties that handles the `/api/v1/auth/requestcode` + `/api/v1/auth/request` handshake and saves the API token in your OBS scene collection.
- **Customizable Appearance** — configurable dimensions, album art toggle, progress bar toggle, "Hide When Paused / Disconnected" option, background color & opacity, accent color, text colors, font size, and corner radius.

## Setup in YTMDesktop

1. Open **YouTube Music Desktop App** (v2.0.0+).
2. Go to **Settings -> Integrations**.
3. Enable **Companion Server** (default port `9863`).
4. *(Recommended)* Enable **Companion Authorization**.

## Usage in OBS Studio

1. Add a new source in your Scene: **+ -> YTMDesktop Now Playing**.
2. Click **Pair with YTMDesktop** in the source properties.
3. Approve the pairing request popup in YTMDesktop (matching the displayed code).
4. Customize the size, colors, and layout to match your stream overlay.

## Building from Source (Windows x64)

### Prerequisites
- Visual Studio 2022 (Desktop development with C++)
- CMake 3.28+

### Build Commands
```powershell
cmake --preset windows-x64
cmake --build --preset windows-x64 --config RelWithDebInfo
```

### Installing to OBS Studio
Copy the built plugin DLL and locale folder to your OBS Studio installation (or `%ProgramData%\obs-studio\plugins\obs-ytmdesktop\`):
- `build_x64/RelWithDebInfo/obs-ytmdesktop.dll` -> `obs-plugins/64bit/obs-ytmdesktop.dll`
- `data/locale/en-US.ini` -> `data/obs-plugins/obs-ytmdesktop/locale/en-US.ini`
