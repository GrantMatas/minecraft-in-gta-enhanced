# Prompt for an AI coding agent

Copy this into an agent running locally in a cloned checkout:

```text
Set up and verify this Minecraft-in-GTA Enhanced project on this Windows PC.

Read README.md, docs/SETUP.md, docs/LAUNCHER.md and the setup/installer source.
Reuse the implementation and keep changes focused. Do not modify personal saves,
the normal Minecraft folder, account data or unrelated mods.

Locate this checkout, GTA Enhanced, Steam, JDK 25 and MSVC x64/Windows SDK.
Ask only for missing paths/files or necessary permissions. Verify the actual GTA
version against official Script Hook V support; do not bypass version checks.
Use official Script Hook runtime/SDK and ReShade 6.8.0 with add-on support.
Enhanced requires the compatible official xinput1_4.dll loader.

Prepare builds/package and review exact game files before any required install
approval. Ask me to close GTA normally before replacing locked files. Never
force-kill GTA or Minecraft. Let me handle login, Steam anti-cheat launch options
and elevation/security dialogs. This is offline Story Mode only. Never launch
Online, bypass checks or change anti-cheat services.

Run scripts/setup.ps1 with verified paths and supplied runtime files. Use
-BuildOnly first when appropriate. Install with the reversible installer and
respect its ownership/hash checks. Put tool paths only in ignored
launcher.local.json; preserve unrelated/modified game files and Steam options.

Build Minecraft in GTA.exe, run --check and inspect local logs. Start the EXE
and verify it starts the dedicated Minecraft world and launches Enhanced
through Steam. Ask me to configure -nobattleye for the offline session if needed.
The launcher requires this checkout and its generated files.

In Story Mode verify Steve activation/switching back; inventory click/drag/search;
placement; GTA locomotion and free movement on block tops; double-tap Space
elytra/fireworks; collision with buildings and blocks; Escape hiding the whole
Minecraft layer in GTA menus and restoration on resume only when Steve is active.

Run relevant checks only. Distinguish observed passes from visual checks still
needed. Do not publish saves, downloads, runtime files, recordings, screenshots,
logs, tokens, usernames, private paths or system reports. Sanitize any shared
diagnostics. Finish with the local EXE path, controls, results and removal steps.
```
