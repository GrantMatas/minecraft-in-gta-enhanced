# Windows setup

## Prerequisites

- Windows 10/11 x64 and **PowerShell 7** for setup/installer scripts.
- Owned GTA V Enhanced on Steam and Minecraft Java Edition.
- Steam/Rockstar launchers already installed and signed in by you.
- JDK **25**, including `bin/java.exe` and `bin/javac.exe`.
- Visual Studio Build Tools: Desktop development with C++, MSVC x64 and Windows SDK. ReShade interfaces require Microsoft's C++ ABI.
- Internet for Gradle/Fabric/Minecraft dependencies and ReShade headers.
- A checkout in a writable directory outside the game installation.

This repo does not install system tools, games, drivers or accounts automatically.

## Runtime downloads

1. Check your `GTA5_Enhanced.exe` version against the [official Script Hook V page](https://www.dev-c.com/gtav/scripthookv/). Obtain its compatible runtime and SDK ZIPs. Enhanced's runtime must contain `ScriptHookV.dll`, `dinput8.dll` and **`xinput1_4.dll`**.
2. Obtain [ReShade 6.8.0 with add-on support](https://reshade.me/) and extract **`ReShade64.dll`** with an archive tool. Setup expects this 64-bit DLL, not the installer EXE. `gta/fetch_deps.sh` is an alternative fetch/extraction helper for Bash/curl/unzip users.
3. Optionally supply a trusted **Achroma 1.1.0 JAR for Minecraft 26.3** from its author. The full mod is not bundled.

Downloads/SDKs/mod JARs stay local and ignored. The included Gradle wrapper JAR is a standard build bootstrap.

## Offline Story Mode

Close GTA normally. Configure Enhanced's Steam launch options to include `-nobattleye` for the offline modded Story Mode session. Do this yourself in Steam. Setup preserves Steam options and never changes anti-cheat services. The staged runtime also contains an offline launch argument file.

Do not enter Online with the package installed. The native plugin has an additional Online guard; offline Story Mode is the supported workflow. Remove the integration before ordinary Online play.

## Build and install

From the checkout root in PowerShell 7:

```powershell
.\scripts\setup.ps1 `
  -GtaDir 'D:\SteamLibrary\steamapps\common\Grand Theft Auto V Enhanced' `
  -JavaHome 'C:\Program Files\Java\jdk-25' `
  -ScriptHookArchive 'D:\Downloads\ScriptHookV-runtime.zip' `
  -ScriptHookSdkArchive 'D:\Downloads\ScriptHookV-SDK.zip' `
  -ReShadeRuntime 'D:\Downloads\ReShade64.dll' `
  -SteamExecutable 'C:\Program Files (x86)\Steam\steam.exe' `
  -AchromaJar 'D:\Downloads\achroma-1.1.0.jar'
```

Replace example paths. Omit `-AchromaJar` if unused. Set `VCVARS` to your `vcvars64.bat` if automatic MSVC detection fails.

Setup extracts the SDK/runtime locally, fetches official ReShade headers, builds Java/native/launcher source and stages the package. It creates ignored `launcher.local.json` with your Java/Steam paths. Installation records owned hashes and refuses to overwrite unrelated/modified files.

Add `-BuildOnly` to prepare everything without game writes. Then install explicitly:

```powershell
.\scripts\install-gta.ps1 -GtaDir 'D:\SteamLibrary\steamapps\common\Grand Theft Auto V Enhanced' -Action Install
```

Use `-Action Update` for an installation already recorded by this checkout. If game-folder permissions require elevation, review the prepared package and elevate only the necessary install step.

## Launch and checks

Run **Minecraft in GTA.exe**. It creates/loads a dedicated void creative world under `mc/run`, using the Gradle development runtime, then starts Enhanced through Steam. It does not use your normal Minecraft launcher profile or existing worlds. Keep this game directory isolated from personal Minecraft data.

Enter Story Mode, select Steve and check inventory, placement, block-top movement, flight collision, switching back, and pause/resume. Optional Achroma requires its JAR in `mc/run/mods` and a normal Minecraft restart.

```powershell
.\'Minecraft in GTA.exe' --check
.\gta\tests\build_pause_overlay.bat
.\gta\tests\build_block_collision.bat
```

Native tests require `VCVARS`. Logs are in `logs/` and `runtime/reshade/ReShade.log`. Sanitize paths/usernames before sharing them. To remove unchanged project-owned game files with GTA closed:

```powershell
.\scripts\install-gta.ps1 -GtaDir 'D:\SteamLibrary\steamapps\common\Grand Theft Auto V Enhanced' -Action Uninstall
```

Modified files are preserved for review. Removal leaves saves, external software and the dedicated Minecraft world intact.
