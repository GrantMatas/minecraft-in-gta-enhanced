# Launcher

`scripts/build-launcher.ps1` compiles the C# files in `launcher/` into **Minecraft in GTA.exe**, using the installed .NET Framework compiler. Keep it in the checkout root with the source/build/runtime folders. It is not a single-file redistributable installer.

First-time setup is `scripts/setup.ps1`, which needs compatible runtime downloads, JDK 25 and MSVC. It creates the local installation manifest and package. These generated artifacts are not in the public source checkout.

Normal launch:

1. Reads `diagnostics/install-Enhanced.json` to locate the installation prepared by this checkout.
2. Checks required files, redirects writable ReShade state to `runtime/reshade`, and resets the saved render gate before a new GTA launch.
3. Compares staged/installed hashes. Updates require GTA closed; owned files are backed up, unrelated/modified files preserved, and failed updates rolled back.
4. Probes `127.0.0.1:25599`. Starts `mc/gradlew.bat runClient` if absent, waits for the dedicated world, or asks for an old client to close normally.
5. Starts Enhanced through Steam app **3240220**, preserving Steam options.

The current handshake requires `inventory-ui-v3` and `movement-v4`. Update the gate when a protocol change makes an old client incompatible.

## Configuration

Setup writes ignored **`launcher.local.json`**. Its structure is shown in `launcher/launcher.settings.example.json`: `javaHome` and `steamExecutable`. Without it, Java uses `JAVA_HOME` or a conventional JDK 25 path; Steam uses its standard Program Files (x86) location. GTA's path comes from the generated installer manifest.

`--check` validates files and the Java 25 major version without launching games/installing updates, writes `logs/launcher-check.log`, and exits 0 for success or 1 for failure. `Minecraft: None` is normal when Minecraft is closed.

The EXE does not install games/tools, authenticate accounts, accept elevation dialogs, update drivers or stop game processes. Local configuration and raw logs must not be published.
