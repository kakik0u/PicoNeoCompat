# PicoNeoCompat

[Japanese Version](https://github.com/kakik0u/PicoNeoCompat/blob/main/README.md)

A compatibility patch that enables SteamVR to work with PICO Neo2 / Neo2 Eye / Neo3 headsets using Streaming Assistant on NVIDIA driver versions 582.xx and later.

> [!WARNING]
> This is an unofficial patch and is not affiliated with PICO, Valve, or NVIDIA. Use it at your own risk.

## Installation

### 0. Prerequisites

- SteamVR must be installed.
- Streaming Assistant must be installed and its initial setup completed.

### 1. Download the Prebuilt Package

Download `PicoNeoCompat-win64.zip` from the [latest release](https://github.com/kakik0u/PicoNeoCompat/releases/latest), then extract it.

### 2. Exit SteamVR

Completely exit SteamVR and make sure it is no longer running in the system tray.

### 3. Run `Install.bat`

Double-click `Install.bat` in the extracted folder. Press any key at the confirmation prompt. When the Windows administrator permission dialog appears, select **Yes**.

The installer checks the default installation paths for the PICO driver and SteamVR. If either one is installed in a different location, you will be prompted to enter its directory path, for example:

- `C:\Program Files (x86)\Streaming Assistant\driver`

### 4. Launch SteamVR

If SteamVR starts without errors, the installation was successful. Yay!

SteamVR may automatically block `pico` because of previous crash history. In that case, you may see a notification indicating that an add-on has been blocked.

Open **SteamVR Settings** → **Startup/Shutdown** → **Manage Add-ons**, unblock `pico`, and then restart SteamVR.

### Changes Made by the Installer

1. Copies the PICO driver to `%LOCALAPPDATA%\PicoNeo2NvencCompat\driver`
2. Patches only `VEncPlugin.dll` in the copied driver; the original file is not modified
3. Configures `RVRPlugin.ini` in the copied driver to use H.264
4. Installs the compatibility DLL
5. Redirects the OpenVR driver registration to the copied driver

## Uninstallation

Exit SteamVR, then double-click `Uninstall.bat` in the extracted folder. Select **Yes** when the administrator permission dialog appears. This restores the original driver registration.

To uninstall from the command line:

```powershell
.\scripts\uninstall.ps1 -RemovePatchedDriver
```

## Tested Configuration

- PICO Neo 2 Eye
- GeForce RTX 3060 Ti (Ampere)
- NVIDIA driver 596.49
- SteamVR 2.16.7
- Windows 11 x64
- Legacy OpenVR driver bundled with PICO Streaming Assistant

## How It Works

```text
PICO VEncPlugin.dll
        │  Loaded as nvEncCompat64.dll
        ▼
Compatibility patch
  ├─ Legacy preset GUIDs → P1–P7 + tuning info
  ├─ Deprecated RC modes → CBR/VBR + multipass
  └─ NvEncGetEncodePresetConfig → ...ConfigEx
        │
        ▼
Windows\System32\nvEncodeAPI64.dll (the actual NVIDIA driver)
```

## Building

Make sure the **Desktop development with C++** workload for Visual Studio 2022 or Build Tools 2022 is installed, then run the following commands in PowerShell:

```powershell
Set-ExecutionPolicy -Scope Process Bypass
.\scripts\build.ps1
```

This generates `build\nvEncCompat64.dll` and `build\smoke_test.exe`. A smoke test that loads the actual NVIDIA driver is also run automatically.

After building, you can use `Install.bat`, or run `install.ps1` with explicit paths:

```powershell
.\scripts\install.ps1 `
  -PicoDriverPath 'C:\Program Files (x86)\Streaming Assistant\driver' `
  -SteamVrPath 'C:\Program Files (x86)\Steam\steamapps\common\SteamVR'
```

## Logs

`nvenc_compat.log` is generated in the same folder as the compatibility DLL.

## Notes

- At this time, only the configuration listed above has been tested on actual hardware. If you verify another configuration, please open an issue and let us know.
- SteamVR updates or integrity checks may remove the compatibility patch. If that happens, reinstall it.

## Technical References

- [Technical overview (Japanese)](https://zenn.dev/articles/66f8061fc269f3/)
- [NVIDIA NVENC Preset Migration Guide](https://docs.nvidia.com/video-technologies/video-codec-sdk/13.0/nvenc-preset-migration-guide/index.html)
- [NVIDIA NVENC deprecation notices](https://docs.nvidia.com/video-technologies/video-codec-sdk/13.0/deprecation-notices/index.html)
- [NVIDIA NVENC programming guide](https://docs.nvidia.com/video-technologies/video-codec-sdk/13.0/nvenc-video-encoder-api-prog-guide/index.html)

## License

The project itself is licensed under the [MIT License](LICENSE). See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for information about the NVENC headers.
