# Minecraft in GTA Enhanced

Real Minecraft Java running alongside **GTA V Enhanced Story Mode**. Steve, blocks, items and mobs are composited into GTA with both games' depth buffers.

This experimental Windows integration is derived from [rehan-remade/universal-modder](https://github.com/rehan-remade/universal-modder). It includes source, build scripts and a launcher setup workflow. Games, saves, credentials and proprietary runtime downloads are not bundled.

## Features

- Choose Steve from the character wheel, keeping your current GTA character underneath. Select a GTA character to leave Steve mode.
- GTA walking/running/jumping/climbing, creative inventory with clicking/dragging/search, and the GTA minimap.
- Minecraft blocks with GTA collision; elytra double-tap Space, fireworks and building/block collision checks.
- Minecraft TNT, projectiles, mobs, fire and melee mirrored into GTA.
- Optional Achroma 1.1.0: five-second strike buildup, synchronized GTA grayscale, lightning explosions/fire and outward shockwaves.
- Escape hides the Minecraft layer during GTA's pause menu; resume restores it if Steve mode is active.
- A Windows EXE checks the prepared installation, starts the dedicated Minecraft runtime and launches Enhanced through Steam.

## Start here

1. Follow [setup](docs/SETUP.md) to obtain compatible runtime downloads and build/install with `scripts/setup.ps1`.
2. Run **Minecraft in GTA.exe** in the checkout root. It depends on this checkout and its generated files; it is not a standalone installer for a blank PC.
3. Enter **Story Mode**, then select Steve.

For an AI coding agent, paste [this setup prompt](docs/AI_SETUP_PROMPT.md) into its chat from a cloned checkout.

| Control | Action |
| --- | --- |
| Left Alt / controller D-pad Down | Character wheel; select Steve |
| F7 | Toggle Steve mode |
| E | Creative inventory; Escape closes the inventory first |
| Escape in gameplay | GTA pause menu; Minecraft layer hidden |
| Escape / Resume in GTA menu | Restore layer if Steve remains active |
| Number keys / mouse wheel | Hotbar selection |
| Minecraft mouse actions | Use/place/shoot selected item |
| Double-tap Space | Elytra while airborne |
| Firework rocket | Flight boost |
| F8 | Re-level ground if mapping needs correction |

## Compatibility

Recorded development versions: Minecraft **26.3**, **JDK 25**, Fabric Loader **0.19.5**, Fabric API **0.161.0+26.3**, MSVC x64, ReShade **6.8.0 with add-on support** and GTA Enhanced. A local Enhanced **1.0.1158.16** installation has loaded the integration. These are test versions, not a guarantee for later game/driver updates. Verify official Script Hook V support for your own GTA build before setup.

Both games run simultaneously, so CPU/GPU load is substantial. Frame generation and temporal effects can introduce artifacts around injected geometry. GTA terrain is not Minecraft terrain: explosions damage GTA entities but cannot carve GTA roads/buildings.

## Source layout

| Path | Purpose |
| --- | --- |
| `gta/src/`, `gta/shaders/` | Native Story Mode bridge and ReShade composition |
| `mc/src/` | Fabric capture, gameplay bridge and optional Achroma mixins |
| `launcher/` | Windows Forms EXE source and clean templates |
| `scripts/` | Setup, build, reversible install and public export |
| `gta/tests/`, `mc/tests/` | Collision, pause gate and strike timing checks |
| `docs/` | Setup, launcher, architecture, privacy and AI prompt |

See [launcher behavior](docs/LAUNCHER.md), [architecture](docs/ARCHITECTURE.md), [privacy](docs/PRIVACY.md) and [third-party notices](THIRD_PARTY_NOTICES.md).

## License

MIT project source; upstream attribution is retained in [LICENSE](LICENSE). The modified Achroma shader is MIT under [its notice](licenses/ACHROMA-MIT.txt). The Gradle wrapper uses Apache 2.0. Game/mod runtimes must be obtained separately under their own terms.
