# Architecture

The native ASI has a Script Hook V script for GTA natives/input/world events and a ReShade add-on for render uploads/uniforms. GTA natives stay on the script thread.

The Fabric client runs a loopback WebSocket server at `127.0.0.1:25599`. GTA camera/player messages select the Minecraft render pose. Sampled ground becomes invisible barrier collision in the dedicated world. Minecraft blocks, mobs, projectiles, explosions and optional Achroma events return to GTA.

`Local\MCPassthroughFrame` is a triple-buffered shared-memory export of premultiplied world RGBA, depth and a separate hand/HUD/screen overlay. ReShade uploads a stable snapshot, composites the world against GTA depth, then draws the overlay. Composition uses no cloud/video service.

One block equals one GTA metre: GTA `(x,y,z)` maps to Minecraft `(x,z+yOffset,-y)`. Flight checks Minecraft sweeps and synchronous GTA probes. Native collision props own GTA walking/climbing contacts; fallback sweeps cover blocks beyond the prop budget.

An independent render gate handles pause menus: Escape outside the Minecraft inventory immediately hides composition, persists while ScriptHook is suspended, then the resumed script restores it after confirming gameplay. Steve activation is a separate state.

Optional Achroma mixins export glow depth and preserve premultiplied postprocess coverage. Color drain controls GTA grayscale. Seventy buildup phase ticks stretch to 100 real ticks; later phases and the original deterministic seed remain intact. Exact bolt points/wave fronts drive GTA explosions, bounded fires and entity forces.

The bridge accepts local development commands that can change the dedicated world. Keep it on loopback; do not expose it to a LAN or the internet.
