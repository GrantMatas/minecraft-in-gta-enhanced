// ScriptHookV half: every game frame, send GTA's camera and player to the Minecraft mod, feed it the ground
// around the player (as barrier columns), forward input to Minecraft, hide GTA's own player, retain its HUD, and
// turn Minecraft's explosions into GTA explosions.
//
// Coordinates: 1 GTA metre = 1 block. GTA (x, y, z) with z up -> Minecraft (x, z + yOffset, -y).
// Minecraft yaw = 180 - GTA heading, pitch = -GTA pitch. yOffset puts the local ground on a whole block.
//
// Keys: F7 toggles the passthrough, F8 re-levels the Minecraft ground to where the player stands.
#include "compositor.h"
#include "natives.h"
#include "ws.h"
#include "block_collision.h"
#include <main.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <map>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace
{
	constexpr int kPort = 25599;
	constexpr int kGroundRadius = 40;        // blocks around the player that get collision
	constexpr int kGroundProbesPerTick = 160;
	constexpr int kGroundDepth = 2;          // barrier layers under each surface
	constexpr Hash kWeaponUnarmed = 0xA2719263;
	// Experimental gun mode (F9 cycles): GTA's weapons in Steve's hands.
	constexpr Hash kGuns[] = {0x83BF0278, 0x42BF8A85, 0xB1CA77B1};
	int g_gun = -1; // index into kGuns, -1 = Minecraft owns the mouse (blocks, items)
	// Police mode: a wanted level that stays (the player is invincible either way)
	bool g_police = false;
	// Minecraft's projectiles in flight (by entity id), traced segment by segment through GTA's world
	struct Projectile
	{
		float x, y, z;
		bool firework;
		bool seen;
	};
	std::map<int, Projectile> g_projectiles;
	std::vector<Entity> g_army; // tanks, helicopters and their crews ("army" op), to clear them again
	// Minecraft's mobs vs GTA's people (mobs_tick). Minecraft lists its hostile mobs ("mobs"); each gets an invisible,
	// frozen GTA "double" ped in a group the police hate, so GTA's cops really shoot at them, and the double's lost
	// health goes back to the mob ("mobdmg"). GTA lists its people ("peds"); Minecraft gives each an invisible proxy
	// that its mobs hunt, and a mob's hit on a proxy comes back ("mobhit") as damage to the real person.
	struct MobDouble
	{
		Ped ped = 0;
		float x = 0, y = 0, z = 0; // the mob's feet, GTA coordinates
		int health = 0;             // the double's health after the last reset
		bool seen = false;
	};
	std::map<int, MobDouble> g_mobs; // by Minecraft entity id
	std::unordered_set<Ped> g_doublePeds;
	std::unordered_set<Ped> g_pedsSent; // the last "peds" list (a mobhit must name one of these)
	std::map<Ped, Ped> g_copTarget;    // cop -> the double it was told to fight
	std::vector<Ped> g_squad;          // spawned cops ("cops" op) and their cars
	std::vector<Vehicle> g_squadCars;
	int g_pendingCops = 0;
	float g_copsDist = 40.0f;
	bool g_copsLine = false; // park the squad broadside across the ground ahead (a barricade), not on the nearest road
	Hash g_mobGroup = 0;
	constexpr Hash kCopGroup = 0xA49E591C, kArmyGroup = 0xE3D976F3;
	int g_mobsSeenAt = -100000;
	int g_statHits = 0, g_statDmg = 0, g_statDoubles = 0; // debug counters (mobinfo)
	int g_nextPedsAt = 0, g_nextCopTaskAt = 0;
	// Minecraft explosions GTA mirrors: they already hurt Minecraft's mobs, so the doubles' losses from them don't count
	struct RecentBoom
	{
		float x = 0, y = 0, z = 0;
		int until = 0;
	} g_booms[8];
	int g_boomNext = 0;
	int g_doubleVis = 0;              // 0 hidden (SetEntityVisible), 1 locally invisible per frame, 2 alpha 0, 3 shown (debug)
	float g_mobHitScale = 12.0f;      // Minecraft damage (hearts x2) -> GTA damage
	float g_mobDmgScale = 0.125f;     // GTA damage -> Minecraft damage
	constexpr int kDoubleHealth = 5000;
	constexpr size_t kMaxDoubles = 48;
	bool g_huntCopsOnly = false;      // Minecraft's mobs hunt only the police (and army), not passers-by
	// The Nether opening ("nether" from Minecraft): hell comes to GTA's world (hell_tick), Minecraft's hot blocks set
	// GTA's people and cars on fire (hot_tick) and light GTA's world (hell_lights).
	struct Hell
	{
		bool on = false;
		int t0 = 0;
		int fromMinutes = 0;
		bool locked = false, shaking = false;
		int nextCopsAt = 0;
		float x = 0, y = 0, z = 0; // the portal's centre, GTA coordinates
	} g_hell;
	struct HotCluster
	{
		float sx = 0, sy = 0, sz = 0; // sums of cell centres, Minecraft coordinates
		int n = 0;
		int kind = 0; // 0 lava, 1 fire, 2 soul fire
	};
	std::unordered_map<int64_t, int> g_hot;              // Minecraft cell -> kind
	std::unordered_map<int64_t, HotCluster> g_hotClusters; // 4x4-column groups of hot cells, per kind (lights)
	int g_nextHotCheckAt = 0;
	// Screen effects, done on the finished picture so GTA and Minecraft move as one (screen_fx_tick): a camera shake
	// (GTA's own camera shake is stopped every frame: Minecraft's frame can't follow it, it lags a frame behind) and
	// the nether portal's warp while Steve stands in a portal.
	struct ScreenFx
	{
		float shake = 0.0f;       // current shake strength (decays)
		float rumble = 0.0f;      // a floor for it, while rumbling
		int rumbleUntil = 0;
		float warp = 0.0f, warpTarget = 0.0f;
		int warpPulseUntil = 0;
		int64_t last = 0;
		float t = 0.0f;
		// GTA's own camera shake is kept (Minecraft follows it through re-projection, measured to ~1 px); the picture
		// shake is only for testing now ("fx" op)
		bool screenShake = false, suppress = false;
		int gtaShakeFrames = 0;                  // frames GTA's own camera shake was found (and stopped)
	} g_fx;
	float g_meleePed = 12.0f, g_meleeUp = 6.0f, g_meleeCar = 25.0f, g_meleeCarUp = 12.0f;
	Cam g_aimCam = 0; // while aiming: GTA's aim camera moved out past Steve's (much wider) head
	// The gun Steve visibly holds: GTA puts its own where its ped's hands are, inside Steve's blocky arms, so a
	// second copy of the weapon model sits in his hands (Minecraft's crossbow-hold pose), pointing where he aims.
	struct GunFit
	{
		Object held = 0;
		Hash heldHash = 0;
		// grip from Steve's feet: shouldered at his right cheek, so it shows past his head over the shoulder;
		// weapon models point along +x (yaw 90 turns that forward) and tilt about y
		float fwd = 0.30f, right = 0.30f, up = 1.42f, yaw = 90.0f, pitch = 0.0f, roll = 0.0f, tilt = -1.0f;
	} g_gunFit;
	// GTA's attack/aim/melee/reload: disabled while Minecraft owns the mouse, GTA's own in gun mode
	const int kGunControls[] = {24, 25, 257, 140, 141, 142, 143, 263, 264, 45};
	constexpr double kMaxMinecraftPixels = 1920.0 * 1080.0;

	HMODULE g_module = nullptr;
	WsClient g_ws;
	bool g_started = false;
	bool g_enabled = false; // Explicit F7 activation after entering Story Mode.
	Ped g_hiddenPlayer = 0;
	std::atomic<bool> g_mcUiOpen{false}, g_inventoryToggle{false}, g_inventoryClose{false};
	std::atomic<int> g_jumpPresses{0};
	int g_firstJumpAt = -1;
	int g_pendingJumpUntil = 0;
	bool g_controllerJumpDown = false;
	bool g_wheelOpen = false;
	int g_wheelChoice = 3, g_switchControl = 0, g_switchUntil = 0;
	float g_wheelX = 0.0f, g_wheelY = 0.0f;
	int g_generation = -1;
	int g_viewSent = 0;

	// Minecraft-driven flight: Minecraft's physics (elytra, fireworks) move the player; GTA follows with a chase cam.
	struct Drive
	{
		bool on = false;
		bool walk = false;             // vanilla walking/jumping/elytra, rather than a director flight
		Ped ped = 0;                   // restore the exact ped frozen on entry, even after a switch
		float eye = 1.62f;
		Cam cam = 0;
		Vector3 pos = {}, vel = {};     // Minecraft's player (feet), GTA coordinates / velocity per second
		int64_t posNanos = 0;           // when Minecraft sampled it (its System.nanoTime = this process's QPC clock)
		bool havePos = false;
		float heading = 0, pitch = 0;   // where Steve looks (steers): director, or the mouse (gameplay cam)
		float sHeading = 0, sPitch = 0; // the same, smoothed per frame (steering updates arrive unevenly)
		int lookUntil = 0;
		float camX = 0, camY = 0, camZ = 0;
		bool camInit = false;
		float follow = 21.0f;           // chase-cam smoothing rate (1/s); starts low so the switch from GTA's camera is a blend
		int64_t lastTick = 0;
		// this frame's chase cam and the Steve position it frames (Minecraft draws Steve exactly there)
		float outX = 0, outY = 0, outZ = 0, outPitch = 0, outHeading = 0, outFov = 60.0f;
		float steveX = 0, steveY = 0, steveZ = 0;
		bool haveOut = false;
		float dist = 5.5f, height = 1.4f;
		// armed: start flying (elytra, launched along heading/pitch) as soon as the player drops off an edge
		bool armed = false;
		float armZ = 0, armHeading = 0, armPitch = 0, armSpeed = 1.0f, armDrop = 0.6f;
		int armHold = 3000;             // ms the arm heading/pitch steer before the director or the mouse does
		// the player flies it: the mouse steers, any real fall off something tall opens the elytra, and touching
		// down lands (back to walking)
		bool user = false;
		int fallingSince = 0;           // game time the player's current fall began (0 = not falling)
		int launchedAt = 0, armAfter = 0;
	} g_drive;
	float g_yOffset = 0.0f;
	bool g_haveOffset = false;
	std::unordered_set<int64_t> g_sampled;

	// Minecraft blocks mirrored as GTA props: solid for peds and cars, and (visible, a bit smaller than the
	// block so Minecraft always covers them) casting GTA shadows.
	struct BlockProps
	{
		std::string model = "prop_box_wood01a"; // 0.97 x 0.96 x 0.80 m: just inside a block
		bool visible = false; // visible props peek out of Minecraft's blocks during camera moves
		int alpha = 255;      // visible but faded: maybe shadows without the box showing
		Hash hash = 0;
		Vector3 mn = {}, mx = {};
		std::map<std::tuple<int, int, int>, Object> live;
		std::map<std::tuple<int, int, int>, bool> solid;
		std::vector<std::tuple<int, int, int>> pending;
		static constexpr size_t kMax = 400; // GTA crashes (access violation) with ~1500 script objects about
	} g_props;
	Vector3 g_lastWalk{};
	bool g_haveWalk = false;
	int g_nextBlockSync = 0;
	void props_clear_all();
	std::vector<std::pair<int, int>> g_spiral;
	std::atomic<bool> g_toggle{false};
	std::atomic<bool> g_cycleGun{false};
	std::atomic<bool> g_relevel{false};

	// Controls GTA must not act on while Minecraft owns the mouse: attacking/aiming, melee, weapon selection.
	const int kDisabledControls[] = {
		24, 25, 257, 140, 141, 142, 143, 263, 264,       // attack, aim, attack 2, melee
		14, 15, 16, 17, 37, 261, 262,                    // weapon wheel / next / previous
		157, 158, 159, 160, 161, 162, 163, 164, 165,     // weapon slots (number keys)
		44, 45, 47, 58,                                  // cover, reload, detonate, throw grenade
		68, 69, 70, 91, 92, 99, 100, 114, 115, 116,      // vehicle / passenger weapons
	};
	// Number keys 1..9 are GTA's weapon-slot controls in this order.
	const int kHotbarControls[9] = {157, 158, 160, 164, 165, 159, 161, 162, 163};

	int64_t column_key(int x, int z)
	{
		return (int64_t(x) << 32) ^ uint32_t(z);
	}

	float wrap_degrees(float a)
	{
		a = std::fmod(a, 360.0f);
		if (a > 180.0f)
			a -= 360.0f;
		if (a <= -180.0f)
			a += 360.0f;
		return a;
	}

	void sendf(const char *format, ...)
	{
		char buffer[8192];
		va_list args;
		va_start(args, format);
		vsnprintf(buffer, sizeof(buffer), format, args);
		va_end(args);
		g_ws.send(buffer);
	}

	void show_player(Ped ped, bool visible)
	{
		if (g_hiddenPlayer && (visible || g_hiddenPlayer != ped))
		{
			if (natives::DoesEntityExist(g_hiddenPlayer))
				natives::SetEntityVisible(g_hiddenPlayer, TRUE, FALSE);
			g_hiddenPlayer = 0;
		}
		natives::SetEntityVisible(ped, visible ? TRUE : FALSE, FALSE);
		if (!visible)
		{
			natives::SetEntityLocallyInvisible(ped);
			g_hiddenPlayer = ped;
		}
	}

	/// Find the ground under columns around the player that haven't been sampled yet, nearest first.
	void sample_ground(const Vector3 &player)
	{
		if (g_spiral.empty())
		{
			for (int dx = -kGroundRadius; dx <= kGroundRadius; ++dx)
				for (int dz = -kGroundRadius; dz <= kGroundRadius; ++dz)
					if (dx * dx + dz * dz <= kGroundRadius * kGroundRadius)
						g_spiral.emplace_back(dx, dz);
			std::sort(g_spiral.begin(), g_spiral.end(), [](auto &a, auto &b) {
				return a.first * a.first + a.second * a.second < b.first * b.first + b.second * b.second;
			});
		}
		const int px = int(std::floor(player.x)), pz = int(std::floor(-player.y));
		std::string columns;
		int probes = 0;
		for (const auto &[dx, dz] : g_spiral)
		{
			const int x = px + dx, z = pz + dz;
			if (g_sampled.count(column_key(x, z)))
				continue;
			if (++probes > kGroundProbesPerTick)
				break;
			float groundZ = 0.0f;
			// Minecraft column (x, z) covers GTA x in [x, x+1) and y in (-z-1, -z]: probe its centre.
			// probe from just above the player's head: indoors that finds the floor, not the roof
			if (!natives::GetGroundZFor3dCoord(x + 0.5f, -(z + 0.5f), player.z + 1.5f, &groundZ, FALSE, FALSE))
				continue; // collision not streamed in yet: try again later
			g_sampled.insert(column_key(x, z));
			const int top = int(std::floor(groundZ + g_yOffset + 0.5f)) - 1;
			char entry[64];
			snprintf(entry, sizeof(entry), "%s%d,%d,%d,%d", columns.empty() ? "" : ",", x, z, top - kGroundDepth + 1, top);
			columns += entry;
		}
		if (!columns.empty())
			g_ws.send("{\"t\":\"ground\",\"c\":[" + columns + "]}");
	}

	// Flat JSON objects only (what the director and the mod send), with or without spaces around ':'.
	const char *json_value(const std::string &m, const char *key)
	{
		const std::string k = std::string("\"") + key + "\"";
		for (size_t at = m.find(k); at != std::string::npos; at = m.find(k, at + 1))
		{
			const char *p = m.c_str() + at + k.size();
			while (*p == ' ' || *p == '\t')
				++p;
			if (*p != ':')
				continue;
			++p;
			while (*p == ' ' || *p == '\t')
				++p;
			return p;
		}
		return nullptr;
	}

	double json_num(const std::string &m, const char *key, double fallback)
	{
		const char *p = json_value(m, key);
		return p ? std::atof(p) : fallback;
	}

	std::string json_str(const std::string &m, const char *key)
	{
		const char *p = json_value(m, key);
		if (p == nullptr || *p != '"')
			return {};
		const char *end = std::strchr(p + 1, '"');
		return end ? std::string(p + 1, end) : std::string(p + 1);
	}

	/// Keep GTA's (invisible) player alive, unragdolled and unwanted through Minecraft's explosions.
	void make_safe(Ped ped)
	{
		const Player player = natives::PlayerId();
		natives::SetEntityInvincible(ped, TRUE);
		natives::SetPlayerInvincible(player, TRUE);
		natives::SetEntityProofs(ped, TRUE, TRUE, TRUE, TRUE, TRUE);
		natives::SetPedCanRagdoll(ped, FALSE);
		natives::SetMaxWantedLevel(0);
		natives::ClearPlayerWantedLevel(player);
		natives::SetPoliceIgnorePlayer(player, TRUE);
		natives::SetDispatchCopsForPlayer(player, FALSE);
	}

	void send_state(Ped ped)
	{
		const Vector3 p = natives::GetEntityCoords(ped, TRUE);
		const Vector3 c = natives::GetFinalRenderedCamCoord();
		const Vector3 r = natives::GetFinalRenderedCamRot(2);
		sendf("{\"t\":\"gtastate\",\"frame\":%d,\"pos\":[%.3f,%.3f,%.3f],\"h\":%.2f,\"cam\":[%.3f,%.3f,%.3f],\"rot\":[%.2f,%.2f,%.2f],"
			  "\"fov\":%.2f,\"view\":%d,\"interior\":%d,\"yoff\":%.4f,\"render\":%u,\"paused\":%d,\"faded\":%d,\"cutscene\":%d,\"switching\":%d}",
			natives::GetFrameCount(), p.x, p.y, p.z, natives::GetEntityHeading(ped), c.x, c.y, c.z, r.x, r.y, r.z,
			  natives::GetFinalRenderedCamFov(), natives::GetFollowPedCamViewMode(), natives::GetInteriorFromEntity(ped), g_yOffset,
			  compositor::status(), natives::IsPauseMenuActive(), natives::IsScreenFadedOut(), natives::IsCutscenePlaying(), natives::IsPlayerSwitchInProgress());
	}

	/// Nanoseconds on the QueryPerformanceCounter clock, computed the way Java's System.nanoTime does on Windows,
	/// so Minecraft's timestamps compare directly.
	int64_t now_nanos()
	{
		static const double freq = [] { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return double(f.QuadPart); }();
		LARGE_INTEGER c;
		QueryPerformanceCounter(&c);
		return int64_t(double(c.QuadPart) / freq * 1e9);
	}

	/// A jolt of shake: `strength` 1 is a big explosion close by.
	void shake_impulse(float strength)
	{
		g_fx.shake = std::min(1.6f, std::max(g_fx.shake, 0.0f) + strength);
	}

	/// A jolt scaled by how far away something blew up (GTA coordinates).
	void shake_from(float x, float y, float z, float strength)
	{
		const Vector3 c = natives::GetFinalRenderedCamCoord();
		const float d = std::sqrt((x - c.x) * (x - c.x) + (y - c.y) * (y - c.y) + (z - c.z) * (z - c.z));
		shake_impulse(strength * std::clamp(1.25f - d / 45.0f, 0.0f, 1.0f));
	}


	/// Gun mode on (index into kGuns) or off (-1): GTA takes the mouse to aim and fire its weapon, Minecraft's
	/// clicks stop, and Steve holds it (Minecraft poses his arms; the weapon stays visible though the ped isn't).
	void gun_set(Ped ped, int gun)
	{
		// Release held Minecraft actions when changing which game owns the mouse.
		g_ws.send("{\"t\":\"key\",\"k\":\"attack\",\"down\":false}");
		g_ws.send("{\"t\":\"key\",\"k\":\"use\",\"down\":false}");
		g_gun = gun;
		if (gun >= 0)
		{
			natives::GiveWeaponToPed(ped, kGuns[gun], 9999, FALSE, TRUE);
			natives::SetCurrentPedWeapon(ped, kGuns[gun], TRUE);
			natives::SetPedInfiniteAmmoClip(ped, TRUE);
		}
		else
		{
			natives::SetPedInfiniteAmmoClip(ped, FALSE);
			natives::SetCurrentPedWeapon(ped, kWeaponUnarmed, TRUE);
		}
	}

	/// Keep the visible gun in Steve's hands: his arms point where the camera looks (Minecraft's crossbow hold
	/// follows the head), from shoulders at his height; the grip sits `fwd` out along that line.
	void gun_model_tick(Ped ped, bool show)
	{
		const Hash want = show && g_gun >= 0 ? kGuns[g_gun] : 0;
		if (g_gunFit.held != 0 && g_gunFit.heldHash != want)
		{
			natives::DeleteObject(&g_gunFit.held);
			g_gunFit.held = 0;
		}
		if (want == 0)
			return;
		const Vector3 p = natives::GetEntityCoords(ped, TRUE);
		if (g_gunFit.held == 0)
		{
			natives::RequestWeaponAsset(want);
			if (!natives::HasWeaponAssetLoaded(want))
				return;
			g_gunFit.held = natives::CreateWeaponObject(want, p.x, p.y, p.z);
			g_gunFit.heldHash = want;
			natives::SetEntityCollision(g_gunFit.held, FALSE, FALSE);
		}
		const Vector3 r = natives::GetGameplayCamRot(2);
		const float d2r = 3.14159265f / 180.0f, h = r.z * d2r, pt = r.x * d2r;
		const float fx = -std::sin(h) * std::cos(pt), fy = std::cos(h) * std::cos(pt), fz = std::sin(pt);
		const float rx = std::cos(h), ry = std::sin(h);
		const float feetZ = p.z - 1.0f;
		const float x = p.x + rx * g_gunFit.right + fx * g_gunFit.fwd;
		const float y = p.y + ry * g_gunFit.right + fy * g_gunFit.fwd;
		const float z = feetZ + g_gunFit.up + fz * g_gunFit.fwd;
		natives::SetEntityCoordsNoOffset(g_gunFit.held, x, y, z);
		natives::SetEntityRotation(g_gunFit.held, g_gunFit.pitch, g_gunFit.roll + r.x * g_gunFit.tilt, r.z + g_gunFit.yaw);
	}

	/// The crosshair: where GTA's aim (its own camera's centre ray) meets the world, as seen from the camera that
	/// renders (the aim camera is moved out past Steve's head, so that isn't the middle of the screen).
	void reticle_tick(Ped ped)
	{
		const Vector3 c = natives::GetGameplayCamCoord();
		const Vector3 r = natives::GetGameplayCamRot(2);
		const float d2r = 3.14159265f / 180.0f, h = r.z * d2r, pt = r.x * d2r;
		const float fx = -std::sin(h) * std::cos(pt), fy = std::cos(h) * std::cos(pt), fz = std::sin(pt);
		float tx = c.x + fx * 150.0f, ty = c.y + fy * 150.0f, tz = c.z + fz * 150.0f;
		const int probe = natives::StartShapeTestLosProbe(c.x + fx * 1.5f, c.y + fy * 1.5f, c.z + fz * 1.5f, tx, ty, tz, -1, ped);
		BOOL hit = FALSE;
		Vector3 end = {}, normal = {};
		Entity entity = 0;
		if (natives::GetShapeTestResult(probe, &hit, &end, &normal, &entity) == 2 && hit)
		{
			tx = end.x; ty = end.y; tz = end.z;
		}
		float sx = 0.5f, sy = 0.5f;
		if (!natives::GetScreenCoordFromWorldCoord(tx, ty, tz, &sx, &sy))
			return;
		natives::DrawRect(sx, sy, 0.011f, 0.0021f, 255, 255, 255, 220);
		natives::DrawRect(sx, sy, 0.0012f, 0.019f, 255, 255, 255, 220);
	}

	/// GTA's over-the-shoulder aim camera is framed for GTA's ped, whose head is far narrower than Steve's: while
	/// aiming, render from the same camera moved out to the right and back, so Steve's head clears the reticle.
	void aim_cam_tick(bool aiming)
	{
		if (!aiming)
		{
			if (g_aimCam != 0)
			{
				natives::RenderScriptCams(FALSE, TRUE, 200);
				natives::DestroyCam(g_aimCam);
				g_aimCam = 0;
			}
			return;
		}
		const Vector3 c = natives::GetGameplayCamCoord();
		const Vector3 r = natives::GetGameplayCamRot(2);
		const float d2r = 3.14159265f / 180.0f, h = r.z * d2r, pt = r.x * d2r;
		const float fx = -std::sin(h) * std::cos(pt), fy = std::cos(h) * std::cos(pt), fz = std::sin(pt);
		const float rx = std::cos(h), ry = std::sin(h);
		const float x = c.x + rx * 0.45f - fx * 0.35f, y = c.y + ry * 0.45f - fy * 0.35f, z = c.z - fz * 0.35f + 0.05f;
		if (g_aimCam == 0)
		{
			g_aimCam = natives::CreateCam("DEFAULT_SCRIPTED_CAMERA");
			natives::SetCamCoord(g_aimCam, x, y, z);
			natives::SetCamRot(g_aimCam, r.x, r.y, r.z);
			natives::SetCamFov(g_aimCam, natives::GetGameplayCamFov());
			natives::SetCamActive(g_aimCam, TRUE);
			natives::RenderScriptCams(TRUE, TRUE, 200);
		}
		natives::SetCamCoord(g_aimCam, x, y, z);
		natives::SetCamRot(g_aimCam, r.x, r.y, r.z);
		natives::SetCamFov(g_aimCam, natives::GetGameplayCamFov());
	}

	/// Minecraft-driven flight on/off. On: a scripted chase camera takes over from GTA's, starting where GTA's camera
	/// is and blending in; GTA's player is frozen and rides along with Minecraft's.
	void drive_set(Ped ped, bool want, bool ease = false)
	{
		if (want && !g_drive.on)
		{
			const Vector3 c = natives::GetFinalRenderedCamCoord();
			const Vector3 r = natives::GetFinalRenderedCamRot(2);
			g_drive.cam = natives::CreateCam("DEFAULT_SCRIPTED_CAMERA");
			natives::SetCamCoord(g_drive.cam, c.x, c.y, c.z);
			natives::SetCamRot(g_drive.cam, r.x, r.y, r.z);
			natives::SetCamFov(g_drive.cam, natives::GetFinalRenderedCamFov());
			natives::SetCamActive(g_drive.cam, TRUE);
			natives::RenderScriptCams(TRUE);
			natives::FreezeEntityPosition(ped, TRUE);
			natives::SetEntityCollision(ped, FALSE, FALSE);
			g_drive.ped = ped;
			g_drive.heading = g_drive.sHeading = natives::GetGameplayCamRot(2).z;
			g_drive.pitch = g_drive.sPitch = 0;
			g_drive.camX = c.x; g_drive.camY = c.y; g_drive.camZ = c.z;
			g_drive.camInit = true;
			g_drive.follow = 4.0f;
			g_drive.lastTick = now_nanos();
			g_drive.havePos = false;
			g_drive.haveOut = false;
			const Vector3 p = natives::GetEntityCoords(ped, TRUE);
			g_drive.steveX=p.x; g_drive.steveY=p.y; g_drive.steveZ=p.z-1.0f;
		}
		else if (!want && g_drive.on)
		{
			natives::RenderScriptCams(FALSE, ease, 700);
			natives::DestroyCam(g_drive.cam);
			g_drive.cam = 0;
			if(natives::DoesEntityExist(g_drive.ped)) {
				natives::FreezeEntityPosition(g_drive.ped, FALSE);
				natives::SetEntityCollision(g_drive.ped, TRUE, TRUE);
			}
			g_drive.ped = 0;
		}
		g_drive.on = want;
		g_drive.armed = false;
	}

	/// A Minecraft projectile went from (ax..) to (bx..) this tick (GTA coordinates): if that crosses anything of GTA's,
	/// a firework blows up there and an arrow lands a bullet in a person or car (or sticks in a wall), and
	/// Minecraft is told where (so the rocket bursts / the arrow stops there too).
	bool projectile_segment(Ped ped, int id, const Projectile &pr, float bx, float by, float bz)
	{
		BOOL hit = FALSE;
		Vector3 end = {}, normal = {};
		Entity entity = 0;
		float sx0 = pr.x, sy0 = pr.y, sz0 = pr.z;
		Entity ignore = ped;
		for (int tries = 0;; ++tries)
		{
			const int probe = natives::StartShapeTestLosProbe(sx0, sy0, sz0, bx, by, bz, 1 | 2 | 4 | 8 | 16, ignore);
			if (natives::GetShapeTestResult(probe, &hit, &end, &normal, &entity) != 2 || !hit)
				return false;
			if (entity == 0 || !g_doublePeds.count(entity))
				break;
			// a Minecraft mob's double: Minecraft resolves hits on its own mobs; trace on past it
			if (tries >= 3)
				return false;
			const float ex = bx - end.x, ey = by - end.y, ez = bz - end.z, el = std::sqrt(ex * ex + ey * ey + ez * ez);
			if (el < 0.4f)
				return false;
			sx0 = end.x + ex / el * 0.3f;
			sy0 = end.y + ey / el * 0.3f;
			sz0 = end.z + ez / el * 0.3f;
			ignore = entity;
		}
		const float dx = bx - pr.x, dy = by - pr.y, dz = bz - pr.z;
		const float len = std::max(0.001f, std::sqrt(dx * dx + dy * dy + dz * dz));
		const float ux = dx / len, uy = dy / len, uz = dz / len;
		const int kind = entity != 0 ? natives::GetEntityType(entity) : 0; // 1 ped, 2 vehicle, 3 object
		bool stick = false;
		if (pr.firework)
		{
			natives::AddExplosion(end.x, end.y, end.z, 4 /* rocket */, 1.0f, TRUE, FALSE, 0.3f, FALSE);
			shake_from(end.x, end.y, end.z, 0.6f);
		}
		else if (kind == 1 || kind == 2)
			natives::ShootSingleBulletBetweenCoords(end.x - ux * 1.0f, end.y - uy * 1.0f, end.z - uz * 1.0f,
				end.x + ux * 0.6f, end.y + uy * 0.6f, end.z + uz * 0.6f, 250, 0x05FC3C11 /* sniper rifle */, ped);
		else
			stick = true;
		const float sx = end.x - ux * 0.15f, sy = end.y - uy * 0.15f, sz = end.z - uz * 0.15f;
		sendf("{\"t\":\"projhit\",\"id\":%d,\"pos\":[%.3f,%.3f,%.3f],\"stick\":%s}", id, sx, sz + g_yOffset, -sy, stick ? "true" : "false");
		return true;
	}

	/// {"t":"proj","p":[[id,"arrow"|"firework",x,y,z],...]} (Minecraft coordinates), every tick while any fly.
	void projectiles_message(Ped ped, const std::string &message)
	{
		for (auto &[id, pr] : g_projectiles)
			pr.seen = false;
		const char *at = json_value(message, "p");
		const Vector3 me = natives::GetEntityCoords(ped, TRUE);
		while (at && (at = std::strchr(at + 1, '[')) != nullptr)
		{
			int id = 0;
			char kind[16] = {};
			double x, y, z;
			if (sscanf_s(at, "[%d,\"%15[^\"]\",%lf,%lf,%lf]", &id, kind, unsigned(sizeof(kind)), &x, &y, &z) != 5)
				continue;
			const float gx = float(x), gy = float(-z), gz = float(y) - g_yOffset;
			auto it = g_projectiles.find(id);
			// first sighting: trace from Steve's chest, so point-blank shots count
			const Projectile from = it != g_projectiles.end() ? it->second
				: Projectile{me.x, me.y, me.z + 0.4f, std::strcmp(kind, "firework") == 0, true};
			if (projectile_segment(ped, id, from, gx, gy, gz))
			{
				if (it != g_projectiles.end())
					g_projectiles.erase(it);
				g_projectiles[id] = {gx, gy, gz, from.firework, true};
				g_projectiles[id].seen = true;
				continue;
			}
			g_projectiles[id] = {gx, gy, gz, from.firework, true};
		}
		for (auto it = g_projectiles.begin(); it != g_projectiles.end();)
			it = it->second.seen ? std::next(it) : g_projectiles.erase(it);
	}

	/// A crew member who fights the player (it keeps at it rather than fleeing), armed with a carbine rifle.
	void army_crew(Ped crew, Ped target)
	{
		natives::SetEntityAsMissionEntity(crew);
		natives::GiveWeaponToPed(crew, 0x83BF0278, 9999, FALSE, TRUE);
		natives::SetPedCombatAttributes(crew, 46, TRUE); // always fight
		natives::SetPedCombatAttributes(crew, 5, TRUE);  // fight armed peds
		natives::SetPedFleeAttributes(crew, 0, FALSE);
		natives::TaskCombatPed(crew, target);
		natives::SetPedKeepTask(crew, TRUE);
		g_army.push_back(crew);
	}

	Hash army_model(const char *name)
	{
		const Hash h = natives::GetHashKey(name);
		natives::RequestModel(h);
		for (int i = 0; i < 300 && !natives::HasModelLoaded(h); ++i)
			WAIT(0);
		return natives::HasModelLoaded(h) ? h : 0;
	}

	/// Tanks on the roads around the player and attack helicopters above, crewed by marines who fight the player.
	void army_spawn(Ped ped, int tanks, int helis, float dist)
	{
		const Hash rhino = army_model("rhino"), buzzard = army_model("buzzard"), marine = army_model("s_m_y_marine_01");
		if (marine == 0)
			return;
		const Vector3 me = natives::GetEntityCoords(ped, TRUE);
		for (int i = 0; i < tanks && rhino != 0; ++i)
		{
			const float a = (0.3f + 6.2832f * i / std::max(1, tanks));
			Vector3 at = {};
			float heading = 0.0f;
			if (!natives::GetClosestVehicleNodeWithHeading(me.x + std::cos(a) * dist, me.y + std::sin(a) * dist, me.z, &at, &heading))
				continue;
			const Vehicle tank = natives::CreateVehicle(rhino, at.x, at.y, at.z + 0.5f, heading);
			natives::SetEntityAsMissionEntity(tank);
			natives::SetVehicleEngineOn(tank, TRUE);
			g_army.push_back(tank);
			army_crew(natives::CreatePedInsideVehicle(tank, marine, -1), ped);
		}
		for (int i = 0; i < helis && buzzard != 0; ++i)
		{
			const float a = (1.2f + 6.2832f * i / std::max(1, helis));
			const Vehicle heli = natives::CreateVehicle(buzzard, me.x + std::cos(a) * dist, me.y + std::sin(a) * dist, me.z + 45.0f, 0.0f);
			natives::SetEntityAsMissionEntity(heli);
			natives::SetVehicleEngineOn(heli, TRUE);
			natives::SetHeliBladesFullSpeed(heli);
			g_army.push_back(heli);
			for (int seat = -1; seat <= 2; ++seat)
				if (seat != 0)
					army_crew(natives::CreatePedInsideVehicle(heli, marine, seat), ped);
		}
		for (const Hash h : {rhino, buzzard, marine})
			if (h != 0)
				natives::SetModelAsNoLongerNeeded(h);
	}

	void army_clear()
	{
		for (Entity e : g_army)
			natives::DeleteEntity(&e);
		g_army.clear();
	}

	bool mobs_active()
	{
		return !g_mobs.empty() && natives::GetGameTimer() - g_mobsSeenAt < 3000;
	}

	void mob_group_init()
	{
		if (g_mobGroup != 0)
			return;
		natives::AddRelationshipGroup("MCMOBS", &g_mobGroup);
		// one-sided: the police and the army hate Minecraft's mobs (the doubles themselves never pick fights)
		natives::SetRelationshipBetweenGroups(5, kCopGroup, g_mobGroup);
		natives::SetRelationshipBetweenGroups(5, kArmyGroup, g_mobGroup);
	}

	void double_visibility(Ped d)
	{
		natives::SetEntityVisible(d, g_doubleVis != 0 ? TRUE : FALSE, FALSE);
		natives::SetEntityAlpha(d, g_doubleVis == 2 ? 0 : 255);
	}

	/// A double for a mob at GTA feet position (x, y, z), or 0 while its model loads.
	Ped double_create(float x, float y, float z)
	{
		static const Hash model = natives::GetHashKey("a_m_y_skater_01");
		if (!natives::HasModelLoaded(model))
		{
			natives::RequestModel(model);
			return 0;
		}
		mob_group_init();
		const Ped d = natives::CreatePed(26, model, x, y, z + 1.0f, 0.0f);
		if (d == 0)
			return 0;
		natives::SetEntityAsMissionEntity(d);
		natives::SetPedRelationshipGroupHash(d, g_mobGroup);
		natives::SetBlockingOfNonTemporaryEvents(d, TRUE);
		natives::SetPedCanRagdoll(d, FALSE);
		natives::SetPedSuffersCriticalHits(d, FALSE);
		natives::SetPedDiesWhenInjured(d, FALSE);
		natives::SetPedArmour(d, 0);
		natives::SetPedMaxHealth(d, kDoubleHealth);
		natives::SetEntityHealth(d, kDoubleHealth);
		// bullets and explosions register; fire, cars and fists don't
		natives::SetEntityProofs(d, FALSE, TRUE, FALSE, TRUE, TRUE);
		natives::FreezeEntityPosition(d, TRUE);
		double_visibility(d);
		g_doublePeds.insert(d);
		return d;
	}

	void double_delete(MobDouble &m)
	{
		if (m.ped != 0)
		{
			g_doublePeds.erase(m.ped);
			if (natives::DoesEntityExist(m.ped))
				natives::DeletePed(&m.ped);
			m.ped = 0;
		}
	}

	bool near_recent_boom(const MobDouble &m)
	{
		const int now = natives::GetGameTimer();
		for (const RecentBoom &b : g_booms)
		{
			const float dx = b.x - m.x, dy = b.y - m.y, dz = b.z - m.z;
			if (b.until > now && dx * dx + dy * dy + dz * dz < 10.0f * 10.0f)
				return true;
		}
		return false;
	}

	/// {"t":"mobs","m":[[id,"zombie",x,y,z],...]} (Minecraft coordinates, feet), every server tick while any fight.
	void mobs_message(const std::string &message)
	{
		const char *p = json_value(message, "m");
		if (p == nullptr || *p != '[')
			return;
		for (auto &[id, m] : g_mobs)
			m.seen = false;
		++p;
		while (*p != '\0')
		{
			while (*p == ' ' || *p == ',')
				++p;
			if (*p != '[')
				break; // the end of the list
			int id = 0;
			char kind[32] = {};
			double x, y, z;
			if (sscanf_s(p, "[%d,\"%31[^\"]\",%lf,%lf,%lf]", &id, kind, unsigned(sizeof(kind)), &x, &y, &z) == 5)
			{
				MobDouble &m = g_mobs[id];
				m.x = float(x);
				m.y = float(-z);
				m.z = float(y) - g_yOffset;
				m.seen = true;
			}
			const char *end = std::strchr(p, ']');
			if (end == nullptr)
				break;
			p = end + 1;
		}
		for (auto it = g_mobs.begin(); it != g_mobs.end();)
		{
			if (it->second.seen)
			{
				++it;
				continue;
			}
			double_delete(it->second);
			it = g_mobs.erase(it);
		}
		if (!g_mobs.empty())
			g_mobsSeenAt = natives::GetGameTimer();
	}

	/// A Minecraft mob hit one of GTA's people: {"t":"mobhit","h":ped,"d":damage,"from":[x,y,z],"k":"zombie"}.
	void mobhit_message(Ped player, const std::string &message)
	{
		const Ped victim = Ped(json_num(message, "h", 0));
		if (victim == 0 || victim == player || !g_pedsSent.count(victim) || g_doublePeds.count(victim) ||
			!natives::DoesEntityExist(victim) || natives::GetEntityType(victim) != 1 || natives::IsPedDeadOrDying(victim))
			return;
		const float damage = float(json_num(message, "d", 2.0)) * g_mobHitScale;
		natives::ApplyDamageToPed(victim, std::max(1, int(damage)));
		++g_statHits;
		const char *from = json_value(message, "from");
		double fx = 0, fy = 0, fz = 0;
		if (from == nullptr || sscanf_s(from, "[%lf ,%lf ,%lf ]", &fx, &fy, &fz) != 3)
			return;
		const std::string kind = json_str(message, "k");
		if (kind == "ghast" || kind == "blaze" || kind == "magma_cube")
			natives::StartEntityFire(victim); // fireballs and molten slime
		if (kind == "skeleton" || kind == "stray" || kind == "bogged" || kind == "pillager" || kind == "blaze" || kind == "ghast")
			return; // arrows and fireballs: the damage is enough, no shove
		const Vector3 v = natives::GetEntityCoords(victim, TRUE);
		float dx = v.x - float(fx), dy = v.y - float(-fz);
		const float len = std::max(0.01f, std::sqrt(dx * dx + dy * dy));
		dx /= len;
		dy /= len;
		const bool heavy = kind == "iron_golem" || kind == "ravager" || kind == "warden";
		natives::SetPedToRagdoll(victim, heavy ? 3000 : 1200);
		natives::ApplyForceToEntity(victim, dx * (heavy ? 14.0f : 5.0f), dy * (heavy ? 14.0f : 5.0f), heavy ? 10.0f : 2.5f);
	}

	Hash squad_model(const char *name)
	{
		const Hash h = natives::GetHashKey(name);
		if (!natives::HasModelLoaded(h))
			natives::RequestModel(h);
		return h;
	}

	/// One police car at a road node `g_copsDist` ahead of the player, siren on, with two armed cops beside it.
	bool squad_spawn_one(Ped player)
	{
		const Hash car = squad_model("police3"), cop = squad_model("s_m_y_cop_01");
		if (!natives::HasModelLoaded(car) || !natives::HasModelLoaded(cop))
			return false;
		mob_group_init();
		const Vector3 me = natives::GetEntityCoords(player, TRUE);
		Vector3 at = {};
		float heading = 0.0f;
		if (g_copsLine)
		{
			// a barricade: cars side by side across the view, `g_copsDist` ahead, cops on the near side
			const float camH = natives::GetGameplayCamRot(2).z, h = camH * 3.14159265f / 180.0f;
			const int k = int(g_squadCars.size());
			const float lateral = (k % 2 == 0 ? 1.0f : -1.0f) * 5.5f * float((k + 1) / 2);
			const float fx = -std::sin(h), fy = std::cos(h), rx = std::cos(h), ry = std::sin(h);
			at.x = me.x + fx * g_copsDist + rx * lateral;
			at.y = me.y + fy * g_copsDist + ry * lateral;
			float g = 0.0f;
			if (!natives::GetGroundZFor3dCoord(at.x, at.y, me.z + 3.0f, &g, FALSE, FALSE) || g == 0.0f)
				return true;
			at.z = g;
			heading = camH + 90.0f + (k % 2 == 0 ? 8.0f : -8.0f);
		}
		else
		{
			const float h = (natives::GetGameplayCamRot(2).z + (float(g_squadCars.size() % 3) - 1.0f) * 35.0f) * 3.14159265f / 180.0f;
			if (!natives::GetClosestVehicleNodeWithHeading(me.x - std::sin(h) * g_copsDist, me.y + std::cos(h) * g_copsDist, me.z, &at, &heading))
				return true; // no road there: give up on this one
		}
		const Vehicle v = natives::CreateVehicle(car, at.x, at.y, at.z + 0.5f, heading);
		if (v != 0)
		{
			natives::SetEntityAsMissionEntity(v);
			natives::SetVehicleSiren(v, TRUE);
			g_squadCars.push_back(v);
		}
		const float hr = heading * 3.14159265f / 180.0f;
		const float rx = std::cos(hr), ry = std::sin(hr), fx = -std::sin(hr), fy = std::cos(hr);
		const Hash guns[] = {0x83BF0278 /* carbine */, 0x1D073A89 /* pump shotgun */, 0x1B06D571 /* pistol */};
		for (int i = 0; i < 2; ++i)
		{
			float px, py;
			float ch = heading;
			if (g_copsLine)
			{
				// the car's right side (rx, ry) faces the horde here: stand 2 m on the other side, spread along the car
				px = at.x - rx * 2.2f + fx * (i == 0 ? -1.4f : 1.4f);
				py = at.y - ry * 2.2f + fy * (i == 0 ? -1.4f : 1.4f);
				ch = heading - 90.0f;
			}
			else
			{
				const float side = i == 0 ? -2.2f : 2.2f;
				px = at.x + rx * side + fx * 0.8f;
				py = at.y + ry * side + fy * 0.8f;
			}
			const Ped c = natives::CreatePed(6, cop, px, py, at.z + 1.0f, ch);
			if (c == 0)
				continue;
			natives::SetEntityAsMissionEntity(c);
			natives::SetPedRelationshipGroupHash(c, kCopGroup);
			natives::GiveWeaponToPed(c, guns[(g_squad.size() + i) % 3], 9999, FALSE, TRUE);
			natives::SetPedAccuracy(c, 35);
			natives::SetPedCombatAbility(c, 2);
			natives::SetPedCombatMovement(c, 2);
			natives::SetPedCombatRange(c, 1);
			natives::SetPedCombatAttributes(c, 5, TRUE);  // always fight
			natives::SetPedCombatAttributes(c, 46, TRUE); // fight armed peds even when not armed
			natives::SetPedCombatAttributes(c, 58, TRUE); // don't flee from combat
			natives::SetPedFleeAttributes(c, 0, FALSE);
			natives::SetPedSeeingRange(c, 120.0f);
			natives::SetPedKeepTask(c, TRUE);
			g_squad.push_back(c);
		}
		return true;
	}

	void squad_clear()
	{
		for (Ped c : g_squad)
			if (natives::DoesEntityExist(c))
				natives::DeletePed(&c);
		for (Vehicle v : g_squadCars)
			if (natives::DoesEntityExist(v))
				natives::DeleteEntity(&v);
		g_squad.clear();
		g_squadCars.clear();
		g_copTarget.clear();
		g_pendingCops = 0;
	}

	void mobwar_clear_all()
	{
		for (auto &[id, m] : g_mobs)
			double_delete(m);
		g_mobs.clear();
		g_doublePeds.clear();
		g_pedsSent.clear();
		squad_clear();
	}

	/// Every frame: doubles follow their mobs and report damage; GTA's people go to Minecraft; cops get targets.
	void mobs_tick(Ped player)
	{
		if (g_pendingCops > 0 && squad_spawn_one(player))
			--g_pendingCops;
		if (g_mobs.empty())
			return;
		const int now = natives::GetGameTimer();
		if (now - g_mobsSeenAt > 3000)
		{
			// Minecraft stopped listing its mobs without an empty list (disconnect, pause): nothing to fight
			for (auto &[id, m] : g_mobs)
				double_delete(m);
			g_mobs.clear();
			return;
		}
		for (auto &[id, m] : g_mobs)
		{
			if (m.ped != 0 && (!natives::DoesEntityExist(m.ped) || natives::IsPedDeadOrDying(m.ped)))
			{
				// killed outright (a big GTA explosion): hurt the mob a lot; either way make a new double
				if (natives::DoesEntityExist(m.ped) && !near_recent_boom(m))
					sendf("{\"t\":\"mobdmg\",\"id\":%d,\"d\":40}", id);
				double_delete(m);
			}
			if (m.ped == 0)
			{
				if (g_doublePeds.size() < kMaxDoubles && (m.ped = double_create(m.x, m.y, m.z)) != 0)
				{
					m.health = natives::GetEntityHealth(m.ped);
					++g_statDoubles;
				}
				continue;
			}
			natives::SetEntityCoordsNoOffset(m.ped, m.x, m.y, m.z + 1.0f);
			if (g_doubleVis == 1)
				natives::SetEntityLocallyInvisible(m.ped);
			const int hp = natives::GetEntityHealth(m.ped);
			if (hp < m.health)
			{
				if (!near_recent_boom(m))
				{
					sendf("{\"t\":\"mobdmg\",\"id\":%d,\"d\":%.2f}", id, float(m.health - hp) * g_mobDmgScale);
					++g_statDmg;
				}
				natives::SetEntityHealth(m.ped, kDoubleHealth);
				m.health = natives::GetEntityHealth(m.ped);
			}
		}

		// GTA's people near the player, 20 times a second (their feet, Minecraft coordinates)
		const Vector3 me = natives::GetEntityCoords(player, TRUE);
		if (now >= g_nextPedsAt)
		{
			g_nextPedsAt = now + 50;
			int handles[256];
			const int n = worldGetAllPeds(handles, 256);
			std::string list;
			g_pedsSent.clear();
			for (int i = 0; i < n && g_pedsSent.size() < 48; ++i)
			{
				const Ped q = handles[i];
				if (q == player || g_doublePeds.count(q) || natives::IsPedDeadOrDying(q))
					continue;
				const int type = natives::GetPedType(q);
				if (type == 28)
					continue; // animals (birds would be hunted in the sky)
				if (g_huntCopsOnly && type != 6 && type != 27 && type != 29)
					continue;
				const Vector3 o = natives::GetEntityCoords(q, TRUE);
				const float dx = o.x - me.x, dy = o.y - me.y;
				if (dx * dx + dy * dy > 60.0f * 60.0f)
					continue;
				char e[96];
				snprintf(e, sizeof(e), "%s[%d,%.3f,%.3f,%.3f]", list.empty() ? "" : ",", q, o.x, o.z - 1.0f + g_yOffset, -o.y);
				list += e;
				g_pedsSent.insert(q);
			}
			g_ws.send("{\"t\":\"peds\",\"p\":[" + list + "]}");
		}

		// police (spawned or not) fight the nearest double; re-tasked only when that one is gone
		if (now >= g_nextCopTaskAt)
		{
			g_nextCopTaskAt = now + 1000;
			std::vector<Ped> cops;
			for (Ped c : g_squad)
				if (natives::DoesEntityExist(c) && !natives::IsPedDeadOrDying(c))
					cops.push_back(c);
			int handles[256];
			const int n = worldGetAllPeds(handles, 256);
			for (int i = 0; i < n; ++i)
			{
				const Ped q = handles[i];
				if (q == player || g_doublePeds.count(q) || natives::IsPedDeadOrDying(q) || std::find(cops.begin(), cops.end(), q) != cops.end())
					continue;
				const int type = natives::GetPedType(q);
				bool fighter = type == 6 || type == 27 || type == 29;
				if (!fighter && g_hell.on)
				{
					// in hell the local gangs fight too
					const Hash group = natives::GetPedRelationshipGroupHash(q);
					static const Hash gangs[] = {natives::GetHashKey("AMBIENT_GANG_FAMILY"), natives::GetHashKey("AMBIENT_GANG_BALLAS"),
						natives::GetHashKey("AMBIENT_GANG_MEXICAN"), natives::GetHashKey("AMBIENT_GANG_LOST")};
					fighter = std::find(std::begin(gangs), std::end(gangs), group) != std::end(gangs);
				}
				if (!fighter)
					continue;
				const Vector3 o = natives::GetEntityCoords(q, TRUE);
				if ((o.x - me.x) * (o.x - me.x) + (o.y - me.y) * (o.y - me.y) < 90.0f * 90.0f)
					cops.push_back(q);
			}
			for (Ped c : cops)
			{
				auto current = g_copTarget.find(c);
				if (current != g_copTarget.end() && g_doublePeds.count(current->second))
					continue;
				const Vector3 o = natives::GetEntityCoords(c, TRUE);
				Ped best = 0;
				float bestD = 90.0f * 90.0f;
				for (const auto &[id, m] : g_mobs)
				{
					const float dx = m.x - o.x, dy = m.y - o.y, dd = dx * dx + dy * dy;
					if (m.ped != 0 && dd < bestD)
					{
						best = m.ped;
						bestD = dd;
					}
				}
				if (best != 0)
				{
					natives::TaskCombatPed(c, best);
					g_copTarget[c] = best;
				}
			}
		}
	}

	/// Small patches of ground under each mob and each spawned cop (beyond the player's own radius), so mobs can
	/// chase people anywhere nearby. Probes start just above the mob or cop, not the player.
	void sample_patches()
	{
		int budget = 120;
		std::string columns;
		auto patch = [&](float gx, float gy, float gz, int radius) {
			const int cx = int(std::floor(gx)), cz = int(std::floor(-gy));
			for (int dx = -radius; dx <= radius && budget > 0; ++dx)
				for (int dz = -radius; dz <= radius && budget > 0; ++dz)
				{
					const int x = cx + dx, z = cz + dz;
					if (g_sampled.count(column_key(x, z)))
						continue;
					--budget;
					float groundZ = 0.0f;
					if (!natives::GetGroundZFor3dCoord(x + 0.5f, -(z + 0.5f), gz + 2.0f, &groundZ, FALSE, FALSE) || groundZ == 0.0f)
						continue;
					g_sampled.insert(column_key(x, z));
					const int top = int(std::floor(groundZ + g_yOffset + 0.5f)) - 1;
					char entry[64];
					snprintf(entry, sizeof(entry), "%s%d,%d,%d,%d", columns.empty() ? "" : ",", x, z, top - kGroundDepth + 1, top);
					columns += entry;
				}
		};
		for (const auto &[id, m] : g_mobs)
			patch(m.x, m.y, m.z, 3);
		for (Ped c : g_squad)
			if (natives::DoesEntityExist(c))
			{
				const Vector3 o = natives::GetEntityCoords(c, TRUE);
				patch(o.x, o.y, o.z - 1.0f, 2);
			}
		if (!columns.empty())
			g_ws.send("{\"t\":\"ground\",\"c\":[" + columns + "]}");
	}

	/// Every frame: GTA's own camera shake off (turned into ours), ours decaying, the portal warp easing in and out.
	void screen_fx_tick()
	{
		const int64_t now = now_nanos();
		const float dt = g_fx.last == 0 ? 0.016f : std::clamp(float(now - g_fx.last) * 1e-9f, 0.0f, 0.1f);
		g_fx.last = now;
		g_fx.t += dt;
		if (natives::IsGameplayCamShaking() || natives::IsCinematicCamShaking())
			++g_fx.gtaShakeFrames;
		// (GTA's explosion shake isn't one these report: Minecraft follows it through re-projection instead)
		if (g_fx.suppress && (natives::IsGameplayCamShaking() || natives::IsCinematicCamShaking()))
		{
			// GTA shook its camera (an explosion of its own, a crash): Minecraft's frame can't follow, so shake the
			// picture instead
			natives::StopGameplayCamShaking(TRUE);
			natives::StopCinematicCamShaking(TRUE);
			const Vector3 c = natives::GetFinalRenderedCamCoord();
			const float strength = natives::IsExplosionInSphere(-1, c.x, c.y, c.z, 18.0f) ? 0.9f
				: natives::IsExplosionInSphere(-1, c.x, c.y, c.z, 50.0f) ? 0.45f : 0.25f;
			if (g_fx.shake < strength)
				shake_impulse(strength - g_fx.shake);
		}
		const int game = natives::GetGameTimer();
		const float floor = game < g_fx.rumbleUntil ? g_fx.rumble : 0.0f;
		g_fx.shake = std::max(floor, g_fx.shake * std::exp(-dt * 3.2f));
		const float a = g_fx.shake, t = g_fx.t;
		const float sx = a * 0.010f * (0.6f * std::sin(t * 57.1f) + 0.4f * std::sin(t * 31.7f + 1.3f));
		const float sy = a * 0.010f * (0.6f * std::sin(t * 49.3f + 0.7f) + 0.4f * std::sin(t * 27.9f + 2.1f));
		const float roll = a * 0.012f * std::sin(t * 41.9f + 0.4f);
		const float target = game < g_fx.warpPulseUntil ? 1.0f : g_fx.warpTarget;
		g_fx.warp += (target - g_fx.warp) * (1.0f - std::exp(-dt * (target > g_fx.warp ? 5.0f : 1.4f)));
		if (g_fx.warp < 0.002f && target == 0.0f)
			g_fx.warp = 0.0f;
		if (g_fx.screenShake)
			compositor::set_screen_fx(sx, sy, roll, g_fx.warp);
		else
			compositor::set_screen_fx(0.0f, 0.0f, 0.0f, g_fx.warp);
	}

	int64_t cell_key(int x, int y, int z)
	{
		return (int64_t(x & 0x1FFFFF) << 42) | (int64_t(y & 0x1FFFFF) << 21) | int64_t(z & 0x1FFFFF);
	}

	int64_t cluster_key(int kind, int x, int z)
	{
		return (int64_t(kind) << 60) | (int64_t((x >> 2) & 0x3FFFFFFF) << 30) | int64_t((z >> 2) & 0x3FFFFFFF);
	}

	/// The integers of a flat JSON array field ("key":[1,2,3,...]).
	void json_ints(const std::string &m, const char *key, std::vector<int> &out)
	{
		out.clear();
		const char *p = json_value(m, key);
		if (p == nullptr || *p != '[')
			return;
		++p;
		while (*p != '\0' && *p != ']')
		{
			char *end = nullptr;
			const long v = std::strtol(p, &end, 10);
			if (end == p)
			{
				++p;
				continue;
			}
			out.push_back(int(v));
			p = end;
		}
	}

	void hot_add(int x, int y, int z, int kind)
	{
		const int64_t k = cell_key(x, y, z);
		if (g_hot.count(k))
			return;
		g_hot[k] = kind;
		HotCluster &c = g_hotClusters[cluster_key(kind, x, z)];
		c.sx += x + 0.5f;
		c.sy += y + 0.5f;
		c.sz += z + 0.5f;
		++c.n;
		c.kind = kind;
	}

	void hot_remove(int x, int y, int z)
	{
		const auto it = g_hot.find(cell_key(x, y, z));
		if (it == g_hot.end())
			return;
		const auto c = g_hotClusters.find(cluster_key(it->second, x, z));
		if (c != g_hotClusters.end())
		{
			c->second.sx -= x + 0.5f;
			c->second.sy -= y + 0.5f;
			c->second.sz -= z + 0.5f;
			if (--c->second.n <= 0)
				g_hotClusters.erase(c);
		}
		g_hot.erase(it);
	}

	/// {"t":"hot","lava":[x,y,z,...],"fire":[...],"soul":[...],"clear":[...]}: Minecraft's hot blocks, as they change.
	void hot_message(const std::string &m)
	{
		std::vector<int> v;
		const char *kinds[3] = {"lava", "fire", "soul"};
		for (int k = 0; k < 3; ++k)
		{
			json_ints(m, kinds[k], v);
			for (size_t i = 0; i + 2 < v.size(); i += 3)
				hot_add(v[i], v[i + 1], v[i + 2], k);
		}
		json_ints(m, "clear", v);
		for (size_t i = 0; i + 2 < v.size(); i += 3)
			hot_remove(v[i], v[i + 1], v[i + 2]);
	}

	bool hot_at(int x, int y, int z)
	{
		return g_hot.count(cell_key(x, y, z)) != 0;
	}

	/// Five times a second: GTA's people and cars on (or in) Minecraft's lava, fire or magma catch fire.
	void hot_tick(Ped player)
	{
		const int now = natives::GetGameTimer();
		if (g_hot.empty() || now < g_nextHotCheckAt)
			return;
		g_nextHotCheckAt = now + 200;
		const Vector3 me = natives::GetEntityCoords(player, TRUE);
		int handles[256];
		const int n = worldGetAllPeds(handles, 256);
		for (int i = 0; i < n; ++i)
		{
			const Ped q = handles[i];
			if (q == player || g_doublePeds.count(q) || natives::IsPedDeadOrDying(q) || natives::IsEntityOnFire(q))
				continue;
			const Vector3 o = natives::GetEntityCoords(q, TRUE);
			if ((o.x - me.x) * (o.x - me.x) + (o.y - me.y) * (o.y - me.y) > 80.0f * 80.0f)
				continue;
			const int cx = int(std::floor(o.x)), cz = int(std::floor(-o.y)), cy = int(std::floor(o.z - 1.0f + g_yOffset + 0.2f));
			if (hot_at(cx, cy, cz) || hot_at(cx, cy - 1, cz))
				natives::StartEntityFire(q);
		}
		const Vehicle mine = natives::IsPedInAnyVehicle(player, FALSE) ? natives::GetVehiclePedIsIn(player, FALSE) : 0;
		const int cars = worldGetAllVehicles(handles, 256);
		for (int i = 0; i < cars; ++i)
		{
			const Vehicle v = handles[i];
			if (v == mine || natives::IsEntityOnFire(v))
				continue;
			const Vector3 o = natives::GetEntityCoords(v, TRUE);
			if ((o.x - me.x) * (o.x - me.x) + (o.y - me.y) * (o.y - me.y) > 80.0f * 80.0f)
				continue;
			const float h = natives::GetEntityHeading(v) * 3.14159265f / 180.0f, fx = -std::sin(h), fy = std::cos(h);
			bool burning = false;
			for (float along = -1.6f; along <= 1.61f && !burning; along += 1.6f)
			{
				const int cx = int(std::floor(o.x + fx * along)), cz = int(std::floor(-(o.y + fy * along)));
				for (int cy = int(std::floor(o.z + g_yOffset - 1.6f)); cy <= int(std::floor(o.z + g_yOffset)) && !burning; ++cy)
					burning = hot_at(cx, cy, cz);
			}
			if (burning)
				natives::StartEntityFire(v);
		}
	}

	/// Every frame: Minecraft's lava and fire light GTA's world around them (the nearest few dozen groups), and the
	/// open portal glows purple.
	void hell_lights(Ped player)
	{
		if (g_hell.on)
			natives::DrawLightWithRange(g_hell.x, g_hell.y, g_hell.z + 1.8f, 150, 50, 255, 11.0f, 7.0f);
		if (g_hotClusters.empty())
			return;
		const Vector3 me = natives::GetEntityCoords(player, TRUE);
		std::vector<std::pair<float, const HotCluster *>> nearby;
		nearby.reserve(g_hotClusters.size());
		for (const auto &[k, c] : g_hotClusters)
		{
			const float x = c.sx / c.n, y = -(c.sz / c.n);
			const float dd = (x - me.x) * (x - me.x) + (y - me.y) * (y - me.y);
			if (dd < 70.0f * 70.0f)
				nearby.emplace_back(dd, &c);
		}
		const size_t count = std::min<size_t>(nearby.size(), 40);
		std::partial_sort(nearby.begin(), nearby.begin() + count, nearby.end(), [](auto &a, auto &b) { return a.first < b.first; });
		for (size_t i = 0; i < count; ++i)
		{
			const HotCluster &c = *nearby[i].second;
			const float x = c.sx / c.n, y = -(c.sz / c.n), z = c.sy / c.n - g_yOffset + 1.0f;
			const float strength = std::min(1.0f, 0.35f + c.n / 10.0f);
			if (c.kind == 2)
				natives::DrawLightWithRange(x, y, z, 70, 170, 255, 5.0f, 3.0f * strength);
			else if (c.kind == 1)
				natives::DrawLightWithRange(x, y, z, 255, 140, 40, 5.0f, 3.5f * strength);
			else
				natives::DrawLightWithRange(x, y, z, 255, 95, 20, 6.5f, 4.5f * strength);
		}
	}

	/// The Nether opened at `pos` (Minecraft coordinates, the portal's bottom centre): hell in GTA's world too.
	void hell_start(const std::string &m)
	{
		const char *pos = json_value(m, "pos");
		double x = 0, y = 0, z = 0;
		if (pos == nullptr || sscanf_s(pos, "[%lf ,%lf ,%lf ]", &x, &y, &z) != 3)
			return;
		if (g_hell.on)
		{
			g_hell.x = float(x); // told again (a resync): same show, keep going
			g_hell.y = float(-z);
			g_hell.z = float(y) - g_yOffset;
			return;
		}
		g_hell.on = true;
		g_hell.t0 = natives::GetGameTimer();
		g_hell.x = float(x);
		g_hell.y = float(-z);
		g_hell.z = float(y) - g_yOffset;
		g_hell.fromMinutes = natives::GetClockHours() * 60 + natives::GetClockMinutes();
		g_hell.locked = false;
		g_hell.nextCopsAt = g_hell.t0 + 7000;
		// the city turns: a red sky and grade fading in while the clock races to midnight, and the ground shakes
		natives::ClearOverrideWeather();
		natives::SetWeatherTypeOvertimePersist("HALLOWEEN", 5.0f);
		natives::SetTransitionTimecycleModifier("damage", 5.0f);
		natives::AnimpostfxPlay("ExplosionJosh3", 0, FALSE);
		natives::ShakeGameplayCam("LARGE_EXPLOSION_SHAKE", 0.25f);
		natives::ShakeGameplayCam("ROAD_VIBRATION_SHAKE", 0.7f);
		g_hell.shaking = true;
		g_fx.warpPulseUntil = g_hell.t0 + 1200;
		// Minecraft's side is lit by its own night now: don't darken it twice; show its ground over GTA's
		compositor::set_look(0.3f, 0.3f, 12.0f);
		// everyone is prey, and the police and the local gangs fight back
		g_huntCopsOnly = false;
		mob_group_init();
		for (const char *gang : {"AMBIENT_GANG_FAMILY", "AMBIENT_GANG_BALLAS", "AMBIENT_GANG_MEXICAN", "AMBIENT_GANG_LOST"})
			natives::SetRelationshipBetweenGroups(5, natives::GetHashKey(gang), g_mobGroup);
		natives::Notify("~r~The Nether is here");
	}

	void hell_stop()
	{
		if (!g_hell.on)
			return;
		g_hell.on = false;
		natives::ClearTimecycleModifier();
		natives::ClearOverrideWeather();
		natives::SetWeatherTypeNowPersist("EXTRASUNNY");
		natives::SetOverrideWeather("EXTRASUNNY");
		natives::SetClockTime(17, 30, 0);
		g_fx.rumbleUntil = 0;
		natives::StopGameplayCamShaking(TRUE);
		compositor::set_look(-1.0f, -1.0f, -1.0f);
		squad_clear();
		g_hot.clear();
		g_hotClusters.clear();
	}

	/// Every frame while hell is on: the clock's race to midnight, the weather locking in, the police arriving.
	void hell_tick(Ped player)
	{
		if (!g_hell.on)
			return;
		const int now = natives::GetGameTimer(), el = now - g_hell.t0;
		if (el <= 5200)
		{
			float k = std::clamp(el / 5000.0f, 0.0f, 1.0f);
			k = k * k * (3.0f - 2.0f * k);
			int to = 23 * 60 + 40;
			if (to < g_hell.fromMinutes)
				to += 24 * 60;
			const int minutes = (g_hell.fromMinutes + int((to - g_hell.fromMinutes) * k)) % (24 * 60);
			natives::SetClockTime(minutes / 60, minutes % 60, 0);
			natives::PauseClock(TRUE);
		}
		else
		{
			if (!g_hell.locked)
			{
				natives::SetOverrideWeather("HALLOWEEN");
				g_hell.locked = true;
			}
			// GTA clears the "damage" grade itself (it's its hurt-player look): keep it on
			natives::SetTimecycleModifier("damage");
			natives::SetTimecycleModifierStrength(1.0f);
		}
		if (g_hell.shaking && el > 8000)
		{
			natives::StopGameplayCamShaking(FALSE);
			g_hell.shaking = false;
		}
		if (now >= g_hell.nextCopsAt && g_squad.size() < 12)
		{
			// the police respond (on the roads around), and keep coming
			g_copsLine = false;
			g_copsDist = 38.0f;
			g_pendingCops += g_squad.empty() ? 3 : 2;
			g_hell.nextCopsAt = now + 40000;
		}
	}

	/// A sword swing in Minecraft: everyone in front of Steve goes flying, and cars within reach get launched.
	void melee(Ped ped)
	{
		const Vector3 me = natives::GetEntityCoords(ped, TRUE);
		const float h = natives::GetGameplayCamRot(2).z * 3.14159265f / 180.0f;
		const float fx = -std::sin(h), fy = std::cos(h);
		int handles[512];
		const int peds = worldGetAllPeds(handles, 512);
		for (int i = 0; i < peds; ++i)
		{
			const Ped q = handles[i];
			if (q == ped || g_doublePeds.count(q))
				continue;
			const Vector3 o = natives::GetEntityCoords(q, TRUE);
			const float dx = o.x - me.x, dy = o.y - me.y, d = std::sqrt(dx * dx + dy * dy);
			if (d > 4.0f || std::fabs(o.z - me.z) > 2.5f || (d > 0.3f && (dx * fx + dy * fy) / d < 0.3f))
				continue;
			natives::SetPedToRagdoll(q, 4000);
			natives::ApplyDamageToPed(q, 400);
			natives::ApplyForceToEntity(q, fx * g_meleePed, fy * g_meleePed, g_meleeUp);
		}
		const int cars = worldGetAllVehicles(handles, 512);
		for (int i = 0; i < cars; ++i)
		{
			const Vehicle v = handles[i];
			const Vector3 o = natives::GetEntityCoords(v, TRUE);
			const float dx = o.x - me.x, dy = o.y - me.y, d = std::sqrt(dx * dx + dy * dy);
			if (d > 5.5f || std::fabs(o.z - me.z) > 3.0f || (d > 0.5f && (dx * fx + dy * fy) / d < 0.2f))
				continue;
			natives::ApplyForceToEntity(v, fx * g_meleeCar, fy * g_meleeCar, g_meleeCarUp);
		}
	}

	/// Whether an armed player has gone off an edge. Scripted: dropped below where it was armed. The player's own:
	/// really falling, with a long way down (not a kerb or a stair).
	bool drive_should_launch(Ped ped)
	{
		const Vector3 p = natives::GetEntityCoords(ped, TRUE);
		if (!g_drive.user)
			return p.z < g_drive.armZ - g_drive.armDrop;
		const int now = natives::GetGameTimer();
		const bool falling = natives::GetEntityVelocity(ped).z < -3.0f &&
			(natives::IsPedFalling(ped) || natives::IsPedInParachuteFreeFall(ped) || natives::GetEntityVelocity(ped).z < -6.0f);
		if (!falling || now < g_drive.armAfter)
		{
			g_drive.fallingSince = 0;
			return false;
		}
		if (g_drive.fallingSince == 0)
			g_drive.fallingSince = now;
		if (now - g_drive.fallingSince < 250)
			return false;
		// a long way down below (a failed probe is no evidence either way: then only a fast fall counts)
		float g = 0.0f;
		if (natives::GetGroundZFor3dCoord(p.x, p.y, p.z + 0.5f, &g, FALSE, FALSE) && g > -100.0f && g != 0.0f)
			return p.z - g > 10.0f;
		return natives::GetEntityVelocity(ped).z < -12.0f;
	}

	/// Touched down: back to walking where Minecraft's player landed, facing the way it flew; armed again for the next jump.
	void drive_land(Ped ped, float x, float y, float groundZ)
	{
		drive_set(ped, false, true);
		natives::SetEntityCoordsNoOffset(ped, x, y, groundZ + 1.0f);
		natives::SetEntityHeading(ped, g_drive.sHeading);
		natives::SetGameplayCamRelativeHeading(0.0f);
		sendf("{\"t\":\"glide\",\"on\":false}");
		g_drive.armZ = groundZ + 1.0f;
		g_drive.armed = true;
		g_drive.armAfter = natives::GetGameTimer() + 250;
		g_drive.fallingSince = 0;
	}

	block_collision::Hit block_sweep(block_collision::Point a, block_collision::Point b, float height, bool walking = false)
	{
		block_collision::Hit best;
		if (g_props.solid.empty()) return best;
		const int x0=int(std::floor(std::min(a.x,b.x)-0.3f)), x1=int(std::floor(std::max(a.x,b.x)+0.3f));
		const int y0=int(std::floor(std::min(a.y,b.y)-0.3f)), y1=int(std::floor(std::max(a.y,b.y)+0.3f));
		const int z0=int(std::floor(std::min(a.z,b.z)+g_yOffset)), z1=int(std::floor(std::max(a.z,b.z)+g_yOffset+height));
		for(int x=x0;x<=x1;++x) for(int y=y0;y<=y1;++y) for(int z=z0;z<=z1;++z) {
			if(!g_props.solid.count(std::make_tuple(x,z,-y-1))) continue;
			const block_collision::Point block{float(x),float(y),z-g_yOffset};
			const auto hit=walking ? block_collision::walk_sweep(a,b,block,height,g_props.live.count(std::make_tuple(x,z,-y-1))!=0) : block_collision::sweep(a,b,block,height);
			if(hit.time<best.time) best=hit;
		}
		return best;
	}

	// GTA still moves the player. This safety sweep fills the gaps between the
	// invisible collision props and also covers blocks beyond GTA's prop budget.
	void walk_blocks(Ped ped, Vector3 &p)
	{
		if(g_drive.on || natives::IsPedInAnyVehicle(ped,FALSE) || natives::IsPedClimbing(ped) ||
			natives::IsPedVaulting(ped) || natives::IsPedJumping(ped)) { g_haveWalk=false; return; }
		const block_collision::Point wanted{p.x,p.y,p.z-1.0f};
		block_collision::Point at{g_lastWalk.x,g_lastWalk.y,g_lastWalk.z};
		const float dx=wanted.x-at.x,dy=wanted.y-at.y,dz=wanted.z-at.z;
		if(g_haveWalk && dx*dx+dy*dy+dz*dz<400.0f) {
			auto target=wanted;
			bool blocked=false;
			for(int pass=0;pass<3;++pass) {
				const auto hit=block_sweep(at,target,1.65f,true);
				if(hit.axis<0) { at=target; break; }
				blocked=true;
				const float t=std::max(0.0f,hit.time-0.001f);
				at={at.x+(target.x-at.x)*t,at.y+(target.y-at.y)*t,at.z+(target.z-at.z)*t};
				if(hit.axis==0) target.x=at.x;
				else if(hit.axis==1) target.y=at.y;
				else target.z=at.z;
			}
			if(blocked) {
				p.x=at.x; p.y=at.y; p.z=at.z+1.0f;
				natives::CorrectPedCoords(ped,p.x,p.y,p.z);
			}
		} else at=wanted;
		g_lastWalk={at.x,0,at.y,0,at.z,0}; g_haveWalk=true;
	}

	// Sweep the gliding player's body, including its leading edge. These probes are
	// synchronous, so a pending result can never allow a frame to cross a GTA wall.
	bool flight_blocked(Ped ped, float ax, float ay, float az, float bx, float by, float bz)
	{
		const float dx=bx-ax, dy=by-ay, dz=bz-az;
		const float length=std::sqrt(dx*dx+dy*dy+dz*dz);
		if(length < 0.001f) return false;
		if(block_sweep({ax,ay,az},{bx,by,bz},0.65f).axis>=0) return true;
		const float horizontal=std::sqrt(dx*dx+dy*dy);
		const float sideX=horizontal>0.001f ? -dy/horizontal : 1.0f;
		const float sideY=horizontal>0.001f ? dx/horizontal : 0.0f;
		const float lead=0.35f/length;
		for(int side=-1;side<=1;++side) for(int height=0;height<3;++height) {
			const float ox=side*0.30f*sideX, oy=side*0.30f*sideY, oz=0.05f+height*0.30f;
			BOOL hit=FALSE; Vector3 end{}, normal{}; Entity entity=0;
			const int probe=natives::StartShapeTestLosProbe(ax+ox,ay+oy,az+oz,
				bx+ox+dx*lead,by+oy+dy*lead,bz+oz+dz*lead,1|2|16,ped);
			if(natives::GetShapeTestResult(probe,&hit,&end,&normal,&entity)!=2 || hit) return true;
		}
		return false;
	}

	void drive_collision(Ped ped)
	{
		const float x=g_drive.steveX,y=g_drive.steveY,z=g_drive.steveZ;
		drive_set(ped,false,true);
		natives::SetEntityCoordsNoOffset(ped,x,y,z+1.0f);
		sendf("{\"t\":\"glide\",\"on\":false}");
		sendf("{\"t\":\"gtainfo\",\"event\":\"flight_collision\",\"pos\":[%.3f,%.3f,%.3f]}",x,y,z);
		g_drive.armed=true;
		g_drive.armAfter=natives::GetGameTimer()+500;
		g_drive.fallingSince=0;
	}

	/// Scripted shots, sent by a director script through Minecraft's link: {"t":"gta","op":...} in GTA coordinates.
	void handle_director(const std::string &m)
	{
		const Ped ped = natives::PlayerPedId();
		const std::string op = json_str(m, "op");
		const float x = float(json_num(m, "x", 0)), y = float(json_num(m, "y", 0)), z = float(json_num(m, "z", 0));
		if (op == "teleport")
		{
			natives::NewLoadSceneStartSphere(x, y, z, 80.0f);
			for (int i = 0; i < 300 && !natives::IsNewLoadSceneLoaded(); ++i)
				WAIT(0);
			natives::NewLoadSceneStop();
			natives::RequestCollisionAtCoord(x, y, z);
			natives::SetEntityCoordsNoOffset(ped, x, y, z);
			natives::SetEntityHeading(ped, float(json_num(m, "h", 0)));
			natives::SetGameplayCamRelativeHeading(0.0f);
			natives::SetGameplayCamRelativePitch(float(json_num(m, "pitch", 0)), 1.0f);
			g_haveOffset = false; // re-level the Minecraft ground here
		}
		else if (op == "walk")
			natives::TaskGoStraightToCoord(ped, x, y, z, float(json_num(m, "speed", 1.0)), int(json_num(m, "timeout", 20000)),
				float(json_num(m, "h", 40000.0)), 0.1f);
		else if (op == "stop")
			natives::ClearPedTasks(ped);
		else if (op == "face")
			natives::SetEntityHeading(ped, float(json_num(m, "h", 0)));
		else if (op == "view")
			natives::SetFollowPedCamViewMode(int(json_num(m, "mode", 1)));
		else if (op == "look")
		{
			natives::SetGameplayCamRelativeHeading(float(json_num(m, "heading", 0)));
			natives::SetGameplayCamRelativePitch(float(json_num(m, "pitch", 0)), 1.0f);
		}
		else if (op == "time")
		{
			natives::SetClockTime(int(json_num(m, "h", 12)), int(json_num(m, "m", 0)), 0);
			natives::PauseClock(TRUE);
		}
		else if (op == "weather")
		{
			const std::string w = json_str(m, "w");
			natives::SetWeatherTypeNowPersist(w.c_str());
			natives::SetOverrideWeather(w.c_str());
		}
		else if (op == "explode")
			natives::AddExplosion(x, y, z, int(json_num(m, "type", 2)), float(json_num(m, "scale", 1.0)), TRUE, FALSE, 1.0f, FALSE);
		else if (op == "ped" || op == "car")
		{
			const std::string model = json_str(m, "model");
			const Hash hash = natives::GetHashKey(model.c_str());
			natives::RequestModel(hash);
			for (int i = 0; i < 200 && !natives::HasModelLoaded(hash); ++i)
				WAIT(0);
			if (natives::HasModelLoaded(hash))
			{
				if (op == "ped")
				{
					const Ped p = natives::CreatePed(26, hash, x, y, z, float(json_num(m, "h", 0)));
					const std::string scenario = json_str(m, "scenario");
					const std::string dict = json_str(m, "anim_dict"), anim = json_str(m, "anim");
					if (!dict.empty() && !anim.empty())
					{
						natives::RequestAnimDict(dict.c_str());
						for (int i = 0; i < 200 && !natives::HasAnimDictLoaded(dict.c_str()); ++i)
							WAIT(0);
						natives::TaskPlayAnimLoop(p, dict.c_str(), anim.c_str());
					}
					else if (!scenario.empty())
						natives::TaskStartScenarioInPlace(p, scenario.c_str());
				}
				else
					natives::SetVehicleOnGroundProperly(natives::CreateVehicle(hash, x, y, z, float(json_num(m, "h", 0))));
				natives::SetModelAsNoLongerNeeded(hash);
			}
		}
		else if (op == "safe")
			make_safe(ped);
		else if (op == "relevel")
			g_haveOffset = false;
		else if (op == "fadein")
			natives::DoScreenFadeIn(500);
		else if (op == "drive")
		{
			g_drive.dist = float(json_num(m, "dist", g_drive.dist));
			g_drive.height = float(json_num(m, "height", g_drive.height));
			drive_set(ped, json_num(m, "on", 1.0) != 0.0);
		}
		else if (op == "gun")
			gun_set(ped, int(json_num(m, "w", -1)));
		else if (op == "police")
		{
			// a wanted level that stays: cops come (the player is invincible regardless)
			const Player player = natives::PlayerId();
			const int stars = int(json_num(m, "stars", 3));
			g_police = stars > 0;
			natives::SetMaxWantedLevel(g_police ? 5 : 0);
			natives::SetPoliceIgnorePlayer(player, g_police ? FALSE : TRUE);
			natives::SetDispatchCopsForPlayer(player, g_police ? TRUE : FALSE);
			if (g_police)
			{
				natives::SetPlayerWantedLevel(player, stars);
				natives::SetPlayerWantedLevelNow(player);
			}
			else
				natives::ClearPlayerWantedLevel(player);
		}
		else if (op == "army")
		{
			if (json_num(m, "clear", 0) != 0)
				army_clear();
			else
				army_spawn(ped, int(json_num(m, "tanks", 2)), int(json_num(m, "helis", 2)), float(json_num(m, "dist", 70)));
		}
		else if (op == "cops")
		{
			// police vs Minecraft's mobs: no wanted level (they leave the player alone), a squad arriving
			g_police = false;
			make_safe(ped);
			mob_group_init();
			g_pendingCops += int(json_num(m, "cars", 3));
			g_copsDist = float(json_num(m, "dist", 40));
			g_copsLine = json_num(m, "line", 0) != 0;
		}
		else if (op == "copsclear")
			squad_clear();
		else if (op == "mobfit")
		{
			g_doubleVis = int(json_num(m, "vis", g_doubleVis));
			g_huntCopsOnly = json_num(m, "copsonly", g_huntCopsOnly ? 1 : 0) != 0;
			g_mobHitScale = float(json_num(m, "hit", g_mobHitScale));
			g_mobDmgScale = float(json_num(m, "dmg", g_mobDmgScale));
			for (auto &[id, d] : g_mobs)
				if (d.ped != 0)
					double_visibility(d.ped);
		}
		else if (op == "poselag")
			compositor::set_pose_lag(int(json_num(m, "n", 1)));
		else if (op == "fx")
		{
			g_fx.screenShake = json_num(m, "shake", g_fx.screenShake ? 1 : 0) != 0;
			g_fx.suppress = json_num(m, "suppress", g_fx.suppress ? 1 : 0) != 0;
			sendf("{\"t\":\"gtainfo\",\"fx\":{\"shake\":%d,\"suppress\":%d,\"gtaShakeFrames\":%d}}",
				g_fx.screenShake ? 1 : 0, g_fx.suppress ? 1 : 0, g_fx.gtaShakeFrames);
			g_fx.gtaShakeFrames = 0;
		}
		else if (op == "hell")
		{
			// testing: hell on at the player (or at x, y, z), or off
			if (json_num(m, "on", 1) != 0)
			{
				const Vector3 me = natives::GetEntityCoords(ped, TRUE);
				char msg[160];
				snprintf(msg, sizeof(msg), "{\"pos\":[%.2f,%.2f,%.2f]}", me.x, me.z - 1.0f + g_yOffset, -me.y);
				hell_start(msg);
			}
			else
				hell_stop();
		}
		else if (op == "tcmod")
		{
			// debug/look-dev: a timecycle modifier (a GTA colour grade) by name, at a strength; "" clears it
			const std::string name = json_str(m, "name");
			if (name.empty())
				natives::ClearTimecycleModifier();
			else
			{
				natives::SetTimecycleModifier(name.c_str());
				natives::SetTimecycleModifierStrength(float(json_num(m, "s", 1.0)));
			}
		}
		else if (op == "postfx")
		{
			const std::string name = json_str(m, "name");
			if (json_num(m, "stop", 0) != 0)
				natives::AnimpostfxStop(name.c_str());
			else
				natives::AnimpostfxPlay(name.c_str(), int(json_num(m, "ms", 0)), json_num(m, "loop", 0) != 0);
		}
		else if (op == "mobwarclear")
			mobwar_clear_all();
		else if (op == "mobinfo")
		{
			// debug: doubles, their health, and what the cops are doing
			std::string info;
			for (const auto &[id, d] : g_mobs)
			{
				char e[128];
				snprintf(e, sizeof(e), "%s[%d,%d,%d,%.1f,%.1f,%.1f]", info.empty() ? "" : ",", id, d.ped, d.ped ? natives::GetEntityHealth(d.ped) : -1, d.x, d.y, d.z);
				info += e;
			}
			sendf("{\"t\":\"gtainfo\",\"mobs\":[%s],\"squad\":%d,\"targets\":%d,\"peds\":%d,\"hits\":%d,\"dmg\":%d,\"doubles\":%d}", info.c_str(),
				int(g_squad.size()), int(g_copTarget.size()), int(g_pedsSent.size()), g_statHits, g_statDmg, g_statDoubles);
		}
		else if (op == "meleefit")
		{
			g_meleePed = float(json_num(m, "ped", g_meleePed));
			g_meleeUp = float(json_num(m, "up", g_meleeUp));
			g_meleeCar = float(json_num(m, "car", g_meleeCar));
			g_meleeCarUp = float(json_num(m, "carup", g_meleeCarUp));
		}
		else if (op == "gunfit")
		{
			g_gunFit.fwd = float(json_num(m, "fwd", g_gunFit.fwd));
			g_gunFit.right = float(json_num(m, "right", g_gunFit.right));
			g_gunFit.up = float(json_num(m, "up", g_gunFit.up));
			g_gunFit.yaw = float(json_num(m, "yaw", g_gunFit.yaw));
			g_gunFit.pitch = float(json_num(m, "pitch", g_gunFit.pitch));
			g_gunFit.roll = float(json_num(m, "roll", g_gunFit.roll));
			g_gunFit.tilt = float(json_num(m, "tilt", g_gunFit.tilt));
		}
		else if (op == "armdrive")
		{
			g_drive.dist = float(json_num(m, "dist", g_drive.dist));
			g_drive.height = float(json_num(m, "height", g_drive.height));
			g_drive.armHeading = float(json_num(m, "h", natives::GetEntityHeading(ped)));
			g_drive.armPitch = float(json_num(m, "p", -20.0));
			g_drive.armSpeed = float(json_num(m, "speed", 1.0));
			g_drive.armDrop = float(json_num(m, "drop", 0.6));
			g_drive.armHold = int(json_num(m, "hold", 3000));
			g_drive.user = json_num(m, "user", 0.0) != 0.0;
			g_drive.armZ = natives::GetEntityCoords(ped, TRUE).z;
			g_drive.armed = json_num(m, "on", 1.0) != 0.0;
		}
		else if (op == "probe")
		{
			// ground heights along a line: from (x, y) towards heading h, n points `step` metres apart, probing down from z
			const float h = float(json_num(m, "h", 0)) * 3.14159265f / 180.0f, step = float(json_num(m, "step", 1.0));
			const int n = int(json_num(m, "n", 20));
			std::string zs;
			for (int i = 0; i < n; ++i)
			{
				float g = -1000.0f;
				natives::GetGroundZFor3dCoord(x - std::sin(h) * step * i, y + std::cos(h) * step * i, z, &g, FALSE, FALSE);
				char e[24];
				snprintf(e, sizeof(e), "%s%.2f", zs.empty() ? "" : ",", g);
				zs += e;
			}
			sendf("{\"t\":\"gtainfo\",\"probe\":[%s]}", zs.c_str());
		}
		else if (op == "flylook")
		{
			g_drive.heading = float(json_num(m, "h", g_drive.heading));
			g_drive.pitch = float(json_num(m, "p", g_drive.pitch));
			g_drive.lookUntil = natives::GetGameTimer() + 3000;
		}
		else if (op == "blockprops")
		{
			props_clear_all();
			const std::string model = json_str(m, "model");
			if (!model.empty())
				g_props.model = model;
			g_props.visible = json_num(m, "visible", 1.0) != 0.0;
			g_props.alpha = int(json_num(m, "alpha", 255));
			g_props.hash = 0;
			g_props.mn = g_props.mx = {};
			g_ws.send("{\"t\":\"blocksync\",\"r\":64}");
		}
		else if (op == "dims")
		{
			const std::string model = json_str(m, "model");
			const Hash h = natives::GetHashKey(model.c_str());
			Vector3 mn = {}, mx = {};
			const BOOL valid = natives::IsModelValid(h);
			if (valid)
			{
				natives::RequestModel(h);
				for (int i = 0; i < 100 && !natives::HasModelLoaded(h); ++i)
					WAIT(0);
				natives::GetModelDimensions(h, &mn, &mx);
			}
			sendf("{\"t\":\"gtainfo\",\"model\":\"%s\",\"valid\":%d,\"min\":[%.3f,%.3f,%.3f],\"max\":[%.3f,%.3f,%.3f]}",
				model.c_str(), valid ? 1 : 0, mn.x, mn.y, mn.z, mx.x, mx.y, mx.z);
		}
		send_state(ped);
	}

	bool props_model_ready()
	{
		if (g_props.hash == 0)
			g_props.hash = natives::GetHashKey(g_props.model.c_str());
		if (!natives::IsModelValid(g_props.hash))
			return false;
		natives::RequestModel(g_props.hash);
		if (!natives::HasModelLoaded(g_props.hash))
			return false;
		if (g_props.mx.x == g_props.mn.x)
			natives::GetModelDimensions(g_props.hash, &g_props.mn, &g_props.mx);
		return true;
	}

	void props_clear_all()
	{
		for (auto &[key, obj] : g_props.live)
			if (natives::DoesEntityExist(obj))
				natives::DeleteObject(&obj);
		g_props.live.clear();
		g_props.solid.clear();
		g_props.pending.clear();
		g_haveWalk=false;
		g_nextBlockSync=natives::GetGameTimer()+500;
	}

	/// Spawn a few queued props per frame (the model has to be streamed in first).
	void props_tick()
	{
		if (g_props.pending.empty() || !props_model_ready())
			return;
		// the model's box centred on the block horizontally and sitting on its floor
		// (Minecraft block (x,y,z) spans GTA x..x+1, -z-1..-z, y-yOffset..+1)
		const float cx = (g_props.mn.x + g_props.mx.x) * 0.5f, cy = (g_props.mn.y + g_props.mx.y) * 0.5f;
		int n = 0;
		while (!g_props.pending.empty() && n < 30 && g_props.live.size() < BlockProps::kMax)
		{
			const auto key = g_props.pending.back();
			g_props.pending.pop_back();
			if (g_props.live.count(key) || !g_props.solid.count(key))
				continue;
			const auto [bx, by, bz] = key;
			const float gx = bx + 0.5f, gy = -(bz + 0.5f), floor = by - g_yOffset;
			const Object o = natives::CreateObjectNoOffset(g_props.hash, gx - cx, gy - cy, floor - g_props.mn.z + 0.01f);
			if (o == 0)
				break; // no room: try again later
			natives::SetEntityRotation(o, 0, 0, 0);
			natives::FreezeEntityPosition(o, TRUE);
			natives::SetEntityCanBeDamaged(o, FALSE);
			natives::SetDisableFragDamage(o, TRUE);
			natives::SetEntityCollision(o, TRUE, TRUE);
			natives::SetEntityLodDist(o, 200);
			if (!g_props.visible)
				natives::SetEntityVisible(o, FALSE, FALSE);
			else if (g_props.alpha < 255)
				natives::SetEntityAlpha(o, g_props.alpha);
			g_props.live[key] = o;
			++n;
		}
	}

	void props_message(const std::string &m)
	{
		auto parse = [&](const char *field, auto &&each) {
			const char *p = json_value(m, field);
			if (p == nullptr || *p != '[')
				return;
			++p;
			int v[3], k = 0;
			while (*p && *p != ']')
			{
				char *end = nullptr;
				const long x = std::strtol(p, &end, 10);
				if (end == p)
				{
					++p;
					continue;
				}
				v[k++] = int(x);
				p = end;
				if (k == 3)
				{
					each(std::make_tuple(v[0], v[1], v[2]));
					k = 0;
				}
			}
		};
		parse("clear", [&](auto key) {
			g_props.solid.erase(key);
			auto it = g_props.live.find(key);
			if (it != g_props.live.end())
			{
				if (natives::DoesEntityExist(it->second))
					natives::DeleteObject(&it->second);
				g_props.live.erase(it);
			}
		});
		parse("set", [&](auto key) { if(g_props.solid.emplace(key,true).second) g_props.pending.push_back(key); });
	}

	/// Messages from Minecraft: explosions become GTA explosions at the same spot; director commands.
	void handle_event(const std::string &message)
	{
		const std::string type = json_str(message, "t");
		if (type == "mcui") {
			g_mcUiOpen = json_value(message, "open") && std::strncmp(json_value(message, "open"), "true", 4) == 0;
			return;
		}
		if (type == "gta")
		{
			handle_director(message);
			return;
		}
		if (type == "blocks")
		{
			props_message(message);
			return;
		}
		if (type == "mcpos")
		{
			g_drive.eye=float(json_num(message,"eye",1.62));
			const char *pos = json_value(message, "pos");
			const char *vel = json_value(message, "vel");
			double x, y, z, vx = 0, vy = 0, vz = 0;
			if (pos && sscanf_s(pos, "[%lf ,%lf ,%lf ]", &x, &y, &z) == 3)
			{
				if (vel)
					sscanf_s(vel, "[%lf ,%lf ,%lf ]", &vx, &vy, &vz);
				g_drive.pos = {float(x), 0, float(-z), 0, float(y - g_yOffset), 0};
				g_drive.vel = {float(vx), 0, float(-vz), 0, float(vy), 0};
				const double tn = json_num(message, "tn", 0.0);
				g_drive.posNanos = tn > 0.0 ? int64_t(tn) : now_nanos();
				g_drive.havePos = true;
			}
			return;
		}
		if (type == "proj")
		{
			projectiles_message(natives::PlayerPedId(), message);
			return;
		}
		if (type == "melee")
		{
			melee(natives::PlayerPedId());
			return;
		}
		if (type == "mobs")
		{
			mobs_message(message);
			return;
		}
		if (type == "hot")
		{
			hot_message(message);
			return;
		}
		if (type == "inportal")
		{
			const char *on = json_value(message, "on");
			g_fx.warpTarget = on != nullptr && std::strncmp(on, "true", 4) == 0 ? 0.85f : 0.0f;
			return;
		}
		if (type == "nether")
		{
			if (json_value(message, "on") != nullptr && std::strncmp(json_value(message, "on"), "true", 4) == 0)
				hell_start(message);
			else
				hell_stop();
			return;
		}
		if (type == "mobhit")
		{
			mobhit_message(natives::PlayerPedId(), message);
			return;
		}
		if (type == "pteleport")
		{
			// Minecraft moved the player (ender pearl): move GTA's player there, keeping the height mapping
			const char *pos = json_value(message, "pos");
			double x, y, z;
			if (pos && sscanf_s(pos, "[%lf ,%lf ,%lf ]", &x, &y, &z) == 3)
				natives::SetEntityCoordsNoOffset(natives::PlayerPedId(), float(x), float(-z), float(y - g_yOffset + 1.0));
			return;
		}
		if (type != "explosion")
			return;
		const char *pos = json_value(message, "pos");
		double x, y, z;
		if (pos == nullptr || sscanf_s(pos, "[%lf ,%lf ,%lf ]", &x, &y, &z) != 3)
			return;
		const double radius = json_num(message, "r", 4.0);
		g_booms[g_boomNext++ % 8] = {float(x), float(-z), float(y - g_yOffset), natives::GetGameTimer() + 500};
		const std::string src = json_str(message, "src");
		if (src == "fireball" || src == "wither_skull" || src == "dragon_fireball")
		{
			// a ghast's fireball: a rocket's blast and burning fuel
			natives::AddExplosion(float(x), float(-z), float(y - g_yOffset), 4, 1.0f, TRUE, FALSE, 0.6f, FALSE);
			natives::AddExplosion(float(x), float(-z), float(y - g_yOffset), 3, 1.0f, TRUE, FALSE, 0.0f, FALSE);
			shake_from(float(x), float(-z), float(y - g_yOffset), 0.8f);
			return;
		}
		// TNT (radius 4) as a sticky bomb, smaller blasts (creepers are 3) as grenades.
		natives::AddExplosion(float(x), float(-z), float(y - g_yOffset), radius >= 3.5 ? 2 : 0, 1.0f, TRUE, FALSE, 1.0f, FALSE);
		shake_from(float(x), float(-z), float(y - g_yOffset), radius >= 3.5 ? 1.1f : 0.8f);
	}

	/// Minecraft-driven flight: follow Minecraft's player with a smoothed chase camera; GTA's (invisible) player rides
	/// along so the world streams in around it. Steering = where Steve looks: the director's flylook, or the mouse.
	void drive_tick(Ped ped)
	{
		const int64_t now = now_nanos();
		const float frameDt = std::clamp(float(now - g_drive.lastTick) * 1e-9f, 0.0f, 0.1f);
		g_drive.lastTick = now;
		if (natives::GetGameTimer() > g_drive.lookUntil)
		{
			if (g_drive.user && !g_mcUiOpen && !g_wheelOpen && !natives::IsPauseMenuActive())
			{
				// the mouse steers, read directly (GTA's own camera isn't the one rendering)
				g_drive.heading -= natives::GetDisabledControlNormal(0, 1) * 6.0f;
				g_drive.pitch = std::clamp(g_drive.pitch - natives::GetDisabledControlNormal(0, 2) * 6.0f, -80.0f, 70.0f);
			}
			else if (!g_drive.user)
			{
				const Vector3 r = natives::GetGameplayCamRot(2);
				g_drive.heading = r.z;
				g_drive.pitch = std::clamp(r.x, -70.0f, 70.0f);
			}
		}
		const float look = 1.0f - std::exp(-frameDt * (g_drive.user ? 16.0f : 8.0f));
		g_drive.sHeading += (std::fmod(g_drive.heading - g_drive.sHeading + 540.0f, 360.0f) - 180.0f) * look;
		g_drive.sPitch += (g_drive.pitch - g_drive.sPitch) * look;
		if (!g_drive.havePos)
			return;
		// where Minecraft's player is now: its last sample, carried forward by its velocity to this moment
		const float dt = std::clamp(float(now - g_drive.posNanos) * 1e-9f, 0.0f, 0.1f);
		const float tx = g_drive.pos.x + g_drive.vel.x * dt, ty = g_drive.pos.y + g_drive.vel.y * dt, tz = g_drive.pos.z + g_drive.vel.z * dt;
		if(flight_blocked(ped,g_drive.steveX,g_drive.steveY,g_drive.steveZ,tx,ty,tz)) {
			drive_collision(ped);
			return;
		}
		if (g_drive.user && !g_drive.walk && g_drive.vel.z < 0.0f && natives::GetGameTimer() - g_drive.launchedAt > 1000)
		{
			// Minecraft's player goes through GTA's world (it isn't in Minecraft): land on whatever is below instead
			float g = 0.0f;
			if (natives::GetGroundZFor3dCoord(tx, ty, tz + 1.0f, &g, FALSE, FALSE) && tz + g_drive.vel.z * 0.05f < g + 0.4f)
			{
				drive_land(ped, tx, ty, g);
				return;
			}
		}
		natives::SetEntityCoordsNoOffset(ped, tx, ty, tz + 1.0f);
		natives::SetEntityHeading(ped, g_drive.sHeading);
		const float d2r = 3.14159265f / 180.0f;
		const float h = g_drive.sHeading * d2r, pt = g_drive.sPitch * d2r;
		const float fx = -std::sin(h) * std::cos(pt), fy = std::cos(h) * std::cos(pt), fz = std::sin(pt);
		const float cx = tx - fx * g_drive.dist, cy = ty - fy * g_drive.dist, cz = tz + 1.2f + g_drive.height - fz * g_drive.dist;
		if (g_drive.walk && natives::GetFollowPedCamViewMode() == 4) {
			// Eyes and rotation follow Minecraft directly in first person.
			const float eye = tz + g_drive.eye;
			natives::SetCamCoord(g_drive.cam, tx, ty, eye);
			natives::SetCamRot(g_drive.cam, g_drive.sPitch, 0.0f, g_drive.sHeading);
			natives::SetCamFov(g_drive.cam, 60.0f);
			g_drive.outX=tx; g_drive.outY=ty; g_drive.outZ=eye;
			g_drive.outPitch=g_drive.sPitch; g_drive.outHeading=g_drive.sHeading; g_drive.outFov=60.0f;
			g_drive.steveX=tx; g_drive.steveY=ty; g_drive.steveZ=tz;
			g_drive.haveOut=true;
			g_drive.camInit=false;
			return;
		}
		if (!g_drive.camInit)
		{
			g_drive.camX = cx; g_drive.camY = cy; g_drive.camZ = cz;
			g_drive.camInit = true;
		}
		// frame-rate independent smoothing (21/s is 0.3 a frame at 60 fps), ramping up after the switch from GTA's camera
		g_drive.follow = std::min(21.0f, g_drive.follow + frameDt * 30.0f);
		const float k = 1.0f - std::exp(-frameDt * g_drive.follow);
		g_drive.camX += (cx - g_drive.camX) * k;
		g_drive.camY += (cy - g_drive.camY) * k;
		g_drive.camZ += (cz - g_drive.camZ) * k;
		// look at a point ahead of Steve so he sits a little below the centre of the frame
		const float ax = tx + fx * 6.0f - g_drive.camX, ay = ty + fy * 6.0f - g_drive.camY, az = tz + 1.0f + fz * 6.0f - g_drive.camZ;
		const float camHeading = std::atan2(-ax, ay) / d2r, camPitch = std::atan2(az, std::sqrt(ax * ax + ay * ay)) / d2r;
		natives::SetCamCoord(g_drive.cam, g_drive.camX, g_drive.camY, g_drive.camZ);
		natives::SetCamRot(g_drive.cam, camPitch, 0.0f, camHeading);
		natives::SetCamFov(g_drive.cam, 60.0f);
		g_drive.outX = g_drive.camX; g_drive.outY = g_drive.camY; g_drive.outZ = g_drive.camZ;
		g_drive.outPitch = camPitch; g_drive.outHeading = camHeading; g_drive.outFov = 60.0f;
		g_drive.steveX = tx; g_drive.steveY = ty; g_drive.steveZ = tz;
		g_drive.haveOut = true;
	}

	/// GTA's ground is at fractional heights but blocks sit on whole ones: when nothing Minecraft is built nearby,
	/// re-level the mapping so the ground here is whole again (built things keep their place).
	void maybe_relevel(const Vector3 &player)
	{
		if (mobs_active())
			return; // a relevel clears all of Minecraft's ground: the mobs would fall through the world
		static int next = 0;
		const int now = natives::GetGameTimer();
		if (now < next)
			return;
		next = now + 1500;
		float groundZ = 0.0f;
		if (!natives::GetGroundZFor3dCoord(player.x, player.y, player.z + 1.0f, &groundZ, FALSE, FALSE))
			return;
		const float mc = groundZ + g_yOffset;
		if (std::fabs(mc - std::round(mc)) < 0.08f)
			return;
		const int px = int(std::floor(player.x)), pz = int(std::floor(-player.y));
		for (const auto &[key, solid] : g_props.solid)
		{
			const int dx = std::get<0>(key) - px, dz = std::get<2>(key) - pz;
			if (dx * dx + dz * dz < 24 * 24)
				return; // blocks nearby: keep them where they are
		}
		g_haveOffset = false; // the tick re-levels to the ground here
	}

	void forward_button(int control, const char *key)
	{
		if (natives::IsDisabledControlJustPressed(0, control))
			sendf("{\"t\":\"key\",\"k\":\"%s\",\"down\":true}", key);
		if (natives::IsDisabledControlJustReleased(0, control))
			sendf("{\"t\":\"key\",\"k\":\"%s\",\"down\":false}", key);
	}

	void steve_mode(Ped ped, bool enabled)
	{
		g_jumpPresses = 0;
		g_firstJumpAt = -1;
		g_pendingJumpUntil = 0;
		g_controllerJumpDown = false;
		if (g_drive.on) drive_set(ped, false, true);
		g_ws.send("{\"t\":\"glide\",\"on\":false}");
		g_ws.send("{\"t\":\"key\",\"k\":\"escape\",\"down\":true}");
		g_mcUiOpen = false;
		g_enabled = enabled;
		if(!enabled) g_ws.send("{\"t\":\"detach\"}");
		gun_set(ped, -1);
		g_drive.user = enabled;
		g_drive.walk = false;
		g_drive.armed = enabled;
		g_drive.armZ = natives::GetEntityCoords(ped, TRUE).z;
		g_drive.armAfter = natives::GetGameTimer() + 500;
		if (enabled) make_safe(ped);
	}

	void movement_input(bool active)
	{
		static int previous = 0, nextRefresh = 0;
		const char *keys[] = {"forward","back","left","right","jump","sprint","sneak"};
		int mask = 0;
		HWND window = GetForegroundWindow(); DWORD process = 0;
		if(window) GetWindowThreadProcessId(window,&process);
		if(active && process == GetCurrentProcessId()) {
			const int vk[] = {'W','S','A','D',VK_SPACE,VK_CONTROL,VK_SHIFT};
			if(natives::IsUsingKeyboardAndMouse(0)) {
				for(int i=0;i<7;++i) if(GetAsyncKeyState(vk[i])&0x8000) mask |= 1<<i;
			} else {
				const float x=natives::GetDisabledControlNormal(0,30), y=natives::GetDisabledControlNormal(0,31);
				if(y < -0.2f) mask|=1; if(y > 0.2f) mask|=2;
				if(x < -0.2f) mask|=4; if(x > 0.2f) mask|=8;
				if(natives::IsDisabledControlPressed(0,22)) mask|=16;
				if(natives::IsDisabledControlPressed(0,21)) mask|=32;
				if(natives::IsDisabledControlPressed(0,36)) mask|=64;
			}
		}
		const int now=natives::GetGameTimer();
		if(mask!=previous || now>=nextRefresh) {
			for(int i=0;i<7;++i) sendf("{\"t\":\"key\",\"k\":\"%s\",\"down\":%s}",keys[i],mask&(1<<i)?"true":"false");
			previous=mask; nextRefresh=now+100;
		}
	}

	// Four Story Mode choices on the game's usual character-wheel control.
	// Original protagonists use GTA's own selection controls and saved locations.
	int g_wheelMovie = 0, g_wheelCurrent = 0;
	bool g_wheelConfigured = false;
	void draw_character_wheel()
	{
		if (!g_wheelMovie || !natives::HasScaleformMovieLoaded(g_wheelMovie)) return;
		// Use Rockstar's original PLAYER_SWITCH artwork, not a recreation of the wheel.
		const int slots[] = {0,3,1,2}, characters[] = {0,2,1,3};
		if (!g_wheelConfigured) {
			for(int choice=0;choice<4;++choice) {
				natives::BeginScaleformMovieMethod(g_wheelMovie,"SET_SWITCH_SLOT");
				natives::ScaleformInt(slots[choice]); natives::ScaleformInt(1);
				natives::ScaleformInt(characters[choice]); natives::ScaleformBool(choice==g_wheelCurrent);
				natives::EndScaleformMovieMethod();
			}
			natives::BeginScaleformMovieMethod(g_wheelMovie,"SET_MP_LABEL");
			natives::ScaleformString("STEVE"); natives::EndScaleformMovieMethod();
			natives::BeginScaleformMovieMethod(g_wheelMovie,"SET_SWITCH_VISIBLE");
			natives::ScaleformInt(1); natives::EndScaleformMovieMethod();
			g_wheelConfigured=true;
		}
		natives::BeginScaleformMovieMethod(g_wheelMovie,"SET_PLAYER_SELECTED");
		natives::ScaleformInt(slots[g_wheelChoice]); natives::EndScaleformMovieMethod();
		// The same dimensions and safe-zone alignment used by the game's selector script.
		const float width=0.098f*1.3333f, height=0.175f;
		natives::SetWidescreenFormat(3);
		natives::SetScriptGfxAlign(82,66);
		natives::SetScriptGfxAlignParams(0,0,0,0);
		float cx=0, cy=0;
		natives::GetScriptGfxAlignPosition(width*0.5f,0.087f,&cx,&cy);
		natives::DrawScaleformMovie(g_wheelMovie,width*0.5f,0.087f,width,height);
		natives::ResetScriptGfxAlign();
		// Minecraft's real eight-pixel Steve face, over the wheel's bottom portrait.
		constexpr unsigned face[8][8] = {
			{0x332411,0x332411,0x3f2a15,0x3f2a15,0x3f2a15,0x3f2a15,0x332411,0x2b1e0d},
			{0x241808,0x332411,0x332411,0x3f2a15,0x3f2a15,0x332411,0x3f2a15,0x332411},
			{0x2b1e0d,0x9b6349,0xb3795e,0xb7836b,0xb3795e,0xaa7259,0x9b6349,0x342512},
			{0x9b6349,0xaa7259,0xb3795e,0xb3795e,0xaa7259,0xaa7259,0xaa7259,0x9b6349},
			{0xaa7259,0xffffff,0x523d89,0xaa7259,0x9b6349,0x523d89,0xffffff,0xaa7259},
			{0x9b6349,0xaa7259,0xaa7259,0x6a4030,0x6a4030,0xaa7259,0xaa7259,0x9b6349},
			{0x90593f,0x8f5e3e,0x492510,0x774235,0x774235,0x421d0a,0x8f5e3e,0x815339},
			{0x94603e,0x815339,0x421d0a,0x492510,0x421d0a,0x492510,0x815339,0x8f5e3e}
		};
		float aspect=std::max(natives::GetAspectRatio(),1.0f), pixel=0.0052f;
		for(int row=0;row<8;++row) for(int col=0;col<8;++col) {
			unsigned color=face[row][col];
			natives::DrawRect(cx+(col-3.5f)*pixel/aspect,cy+0.044f+(row-3.5f)*pixel,pixel/aspect,pixel,
				(color>>16)&255,(color>>8)&255,color&255,255);
		}
	}
	void character_wheel(Ped ped)
	{
		if(!g_wheelMovie) g_wheelMovie=natives::RequestScaleformMovieInstance("PLAYER_SWITCH");
		if(g_switchControl) {
			// Story's selector needs a held selection for >300 ms, then a release.
			// Six frames could expire before it entered the selection state.
			const int now=natives::GetGameTimer();
			natives::EnableControlAction(0,g_switchControl,TRUE);
			natives::SetControlValueNextFrame(0,g_switchControl,now<g_switchUntil ? 1.0f : 0.0f);
			if(now>=g_switchUntil+200 || natives::IsPlayerSwitchInProgress()) g_switchControl=0;
			return; // leave control handling to GTA while handing back the selected protagonist
		}
		natives::DisableControlAction(0, 19, TRUE);
		natives::DisableControlAction(0, 169, TRUE); // Story wheel's fourth choice is Steve.
		natives::DisableControlAction(0, 168, TRUE); // F7 is our compatibility toggle.
		bool held = natives::IsDisabledControlPressed(0, 19) != FALSE;
		if (natives::IsPauseMenuActive() || natives::IsCutsceneActive() || natives::IsPlayerSwitchInProgress() || g_mcUiOpen) {
			g_wheelOpen = false;
			return;
		}
		if (held && !g_wheelOpen) {
			g_wheelOpen = true;
			g_wheelX = g_wheelY = 0;
			Hash model = natives::GetEntityModel(ped);
			g_wheelChoice = g_enabled ? 3 : model == natives::GetHashKey("player_zero") ? 0 : model == natives::GetHashKey("player_one") ? 1 : 2;
			g_wheelCurrent=g_wheelChoice;
			g_wheelConfigured=false;
			g_ws.send("{\"t\":\"key\",\"k\":\"attack\",\"down\":false}");
			g_ws.send("{\"t\":\"key\",\"k\":\"use\",\"down\":false}");
		}
		if (!held && g_wheelOpen) {
			g_wheelOpen = false;
			const Hash model = natives::GetEntityModel(ped);
			const int current = model == natives::GetHashKey("player_zero") ? 0 : model == natives::GetHashKey("player_one") ? 1 : 2;
			steve_mode(ped, g_wheelChoice == 3);
			if (g_wheelChoice != 3 && g_wheelChoice != current) {
				g_switchControl = 166 + g_wheelChoice;
				g_switchUntil = natives::GetGameTimer()+900;
			} else if (g_wheelChoice == 3) natives::Notify(g_ws.connected() ? "Steve selected" : "Steve selected: waiting for Minecraft launcher");
			return;
		}
		if (!g_wheelOpen) return;
		for (int control : {1,2,24,25,30,31,32,33,34,35,166,167,168,187,188,189,190}) natives::DisableControlAction(0, control, TRUE);
		g_wheelX = std::clamp(g_wheelX + natives::GetDisabledControlNormal(0, 1)*0.10f, -1.0f, 1.0f);
		g_wheelY = std::clamp(g_wheelY + natives::GetDisabledControlNormal(0, 2)*0.10f, -1.0f, 1.0f);
		if (std::max(std::abs(g_wheelX), std::abs(g_wheelY)) > 0.035f)
			g_wheelChoice = std::abs(g_wheelX) > std::abs(g_wheelY) ? (g_wheelX > 0 ? 2 : 1) : (g_wheelY > 0 ? 3 : 0);
		if (natives::IsDisabledControlJustPressed(0,188)) g_wheelChoice=0;
		if (natives::IsDisabledControlJustPressed(0,190)) g_wheelChoice=2;
		if (natives::IsDisabledControlJustPressed(0,189)) g_wheelChoice=1;
		if (natives::IsDisabledControlJustPressed(0,187)) g_wheelChoice=3;
		draw_character_wheel();
	}

	int ui_modifiers() {
		return (GetAsyncKeyState(VK_SHIFT)&0x8000?1:0) | (GetAsyncKeyState(VK_CONTROL)&0x8000?2:0) | (GetAsyncKeyState(VK_MENU)&0x8000?4:0);
	}
	int g_uiButtons = 0;

	void inventory_input()
	{
		for(int control : {1,2,24,25,30,31,32,33,34,35,51,46,199,200,14,15,16,17,157,158,159,160,161,162,163,164,165}) natives::DisableControlAction(0,control,TRUE);
		natives::SetMouseCursorThisFrame();
		POINT cursor{}; RECT bounds{};
		HWND window=GetForegroundWindow(); DWORD process=0;
		GetWindowThreadProcessId(window,&process);
		if(process!=GetCurrentProcessId() || !GetCursorPos(&cursor) || !ScreenToClient(window,&cursor) || !GetClientRect(window,&bounds) || bounds.right<=0 || bounds.bottom<=0) {
			for(int button=0;button<2;++button) if(g_uiButtons&(1<<button)) sendf("{\"t\":\"ui_mouse\",\"x\":0.5,\"y\":0.5,\"button\":%d,\"down\":false}",button);
			g_uiButtons=0;
			return;
		}
		float x=std::clamp(float(cursor.x)/bounds.right,0.0f,1.0f), y=std::clamp(float(cursor.y)/bounds.bottom,0.0f,1.0f);
		// GTA's visible frontend cursor can move independently of the OS pointer in raw-input mode.
		float frontendX=natives::GetDisabledControlNormal(2,239), frontendY=natives::GetDisabledControlNormal(2,240);
		if(frontendX>=0 && frontendX<=1 && frontendY>=0 && frontendY<=1 && (frontendX>0 || frontendY>0)) { x=frontendX; y=frontendY; }
		sendf("{\"t\":\"ui_mouse\",\"x\":%.5f,\"y\":%.5f,\"mods\":%d}",x,y,ui_modifiers());
		// With GTA's cursor active, firing controls no longer reliably report mouse buttons.
		// Poll physical buttons only while our own game window has focus; preserve release edges for dragging.
		int buttons=(GetAsyncKeyState(VK_LBUTTON)&0x8000?1:0) | (GetAsyncKeyState(VK_RBUTTON)&0x8000?2:0);
		for(int button=0;button<2;++button) if((buttons^g_uiButtons)&(1<<button))
			sendf("{\"t\":\"ui_mouse\",\"x\":%.5f,\"y\":%.5f,\"button\":%d,\"down\":%s,\"mods\":%d}",x,y,button,buttons&(1<<button)?"true":"false",ui_modifiers());
		g_uiButtons=buttons;
		if(natives::IsDisabledControlJustPressed(0,14)) sendf("{\"t\":\"ui_mouse\",\"x\":%.5f,\"y\":%.5f,\"scroll\":-1}",x,y);
		if(natives::IsDisabledControlJustPressed(0,15)) sendf("{\"t\":\"ui_mouse\",\"x\":%.5f,\"y\":%.5f,\"scroll\":1}",x,y);
	}

	void tick()
	{
		const Ped ped = natives::PlayerPedId();
		character_wheel(ped);
		if (g_toggle.exchange(false))
		{
			steve_mode(ped, !g_enabled);
			natives::Notify(g_enabled ? "Minecraft passthrough ~g~on" : "Minecraft passthrough ~r~off");
		}
		const bool on = g_enabled && g_ws.connected();
		const bool cycleGun = g_cycleGun.exchange(false);
		bool inventoryToggle = g_inventoryToggle.exchange(false), inventoryClose = g_inventoryClose.exchange(false);
		const int keyboardJumpPresses = g_jumpPresses.exchange(0);
		// Prepared cutscenes and leftover character-switch state can outlive the
		// transition. Hide only while a menu/fade or an actual cutscene is visible.
		const bool hidden = natives::IsPauseMenuActive() || natives::IsScreenFadedOut() || natives::IsCutscenePlaying();
		compositor::set_pause_menu(natives::IsPauseMenuActive());
		compositor::set_active(on && !hidden);
		if (!on)
		{
			movement_input(false);
			if(g_drive.on) drive_set(ped,false,true);
			g_drive.walk=false;
			g_mcUiOpen = false;
			if (g_hiddenPlayer)
			{
				show_player(ped, true);
				natives::SetPedCurrentWeaponVisible(ped, TRUE);
			}
			if (!g_props.live.empty())
				props_clear_all();
			if (!g_mobs.empty() || !g_squad.empty())
				mobwar_clear_all();
			compositor::set_screen_fx(0.0f, 0.0f, 0.0f, 0.0f);
			std::string ignored;
			while (g_ws.poll(ignored))
			{
			}
			return;
		}

		if (g_ws.generation() != g_generation)
		{
			// (re)connected: size Minecraft's window to GTA's picture and start the ground over
			g_generation = g_ws.generation();
			g_haveOffset = false;
			if(g_drive.on) drive_set(ped,false,true);
			g_drive.walk=false;
			g_drive.armAfter=natives::GetGameTimer()+500;
			g_viewSent = 0;
			natives::Notify("Minecraft passthrough ~g~connected");
			make_safe(ped);
			mobwar_clear_all();
			g_hot.clear();
			g_hotClusters.clear();
			g_ws.send("{\"t\":\"nethersync\"}");
		}
		if (inventoryToggle && !hidden && !g_wheelOpen) {
			gun_set(ped,-1);
			g_ws.send("{\"t\":\"key\",\"k\":\"inventory\",\"down\":true}");
		}
		if (inventoryClose) g_ws.send("{\"t\":\"key\",\"k\":\"escape\",\"down\":true}");
		if (natives::GetFrameCount() % 30 == 0)
		{
			if (!g_police)
				natives::ClearPlayerWantedLevel(natives::PlayerId());
			send_state(ped);
		}

		// Minecraft's window = GTA's picture (its real backbuffer, not the monitor), at up to ~1080p worth of
		// pixels (the effect scales it up; at 5120x1440 a full-size copy would be ~90 MB of readback per frame).
		int bw = 0, bh = 0;
		compositor::backbuffer_size(bw, bh);
		if (bw > 0 && bh > 0 && (bw * 65536 + bh) != g_viewSent)
		{
			g_viewSent = bw * 65536 + bh;
			const double scale = std::min(1.0, std::sqrt(kMaxMinecraftPixels / (double(bw) * bh)));
			sendf("{\"t\":\"view\",\"w\":%d,\"h\":%d}", int(bw * scale + 0.5), int(bh * scale + 0.5));
		}

		Vector3 p = natives::GetEntityCoords(ped, TRUE);
		if (!g_haveOffset || g_relevel.exchange(false))
		{
			float groundZ = 0.0f;
			if (natives::GetGroundZFor3dCoord(p.x, p.y, p.z + 1.0f, &groundZ, FALSE, FALSE))
			{
				g_yOffset = std::round(groundZ) - groundZ;
				g_haveOffset = true;
				g_sampled.clear();
				g_ws.send("{\"t\":\"clear\"}");
				props_clear_all();
				g_ws.send("{\"t\":\"blocksync\",\"r\":64}");
			}
		}
		if(g_haveOffset) walk_blocks(ped,p);
		if(g_haveOffset && !hidden && natives::GetGameTimer()>=g_nextBlockSync) {
			g_ws.send("{\"t\":\"blocksync\",\"r\":24}");
			g_nextBlockSync=natives::GetGameTimer()+2000;
		}

		// GTA owns walking/running and its normal camera; Minecraft only drives elytra flight.
		movement_input(false);
		if(g_drive.user && !g_drive.on && !g_drive.armed) {
			g_drive.armed=true; g_drive.armZ=p.z;
		}
		// Count real key-down events, so quick taps aren't lost between GTA frames.
		// Auto-repeat/holding Space never counts as a second jump.
		const bool airborne = natives::IsEntityInAir(ped) != FALSE;
		const bool controllerDown = natives::IsDisabledControlPressed(0,22) != FALSE;
		const int jumpPresses = std::max(keyboardJumpPresses, controllerDown && !g_controllerJumpDown ? 1 : 0);
		g_controllerJumpDown = controllerDown;
		const int jumpNow = natives::GetGameTimer();
		const bool jumpReady = g_drive.user && g_drive.armed && !g_drive.on && !hidden && !g_mcUiOpen && !g_wheelOpen && !natives::IsPedInAnyVehicle(ped,FALSE);
		bool doubleJump = false;
		if (!jumpReady) { g_firstJumpAt = -1; g_pendingJumpUntil=0; }
		else if (jumpPresses > 0) {
			doubleJump = jumpPresses >= 2 || (g_firstJumpAt >= 0 && jumpNow - g_firstJumpAt <= 1200);
			g_firstJumpAt = doubleJump ? -1 : jumpNow;
			if(doubleJump) g_pendingJumpUntil=jumpNow+600;
		}
		if(g_drive.armed && airborne && !g_mcUiOpen && !g_wheelOpen) natives::DisableControlAction(0,22,TRUE);
		const bool takeOff = jumpReady && g_pendingJumpUntil>jumpNow && jumpNow>=g_drive.armAfter;
		if (g_drive.armed && !g_drive.on && !natives::IsPedInAnyVehicle(ped,FALSE) && (takeOff || (!g_drive.user && drive_should_launch(ped))))
		{
			// GTA's jump animation may not report airborne yet on a fast double tap.
			const float lift = takeOff ? (airborne ? 0.8f : 2.0f) : 0.0f;
			if(lift>0 && flight_blocked(ped,p.x,p.y,p.z-1.0f,p.x,p.y,p.z-1.0f+lift)) return;
			// off the edge: Minecraft takes over (elytra, launched along the arm heading/pitch, or where the player looks)
			const Vector3 look = natives::GetGameplayCamRot(2);
			g_pendingJumpUntil=0;
			drive_set(ped, true);
			g_drive.heading = g_drive.sHeading = g_drive.user ? look.z : g_drive.armHeading;
			g_drive.pitch = g_drive.sPitch = g_drive.user ? std::clamp(look.x, -80.0f, 70.0f) : g_drive.armPitch;
			g_drive.lookUntil = natives::GetGameTimer() + (g_drive.user ? 0 : g_drive.armHold);
			g_drive.launchedAt = natives::GetGameTimer();
			sendf("{\"t\":\"glide\",\"on\":true,\"speed\":%.3f,\"lift\":%.2f}", g_drive.user ? 1.2f : g_drive.armSpeed,lift);
			sendf("{\"t\":\"gtainfo\",\"event\":\"jump\"}");
		}
		if (g_drive.on)
			drive_tick(ped);

		// The camera GTA rendered with, and where the player stands (Minecraft draws them in third person).
		const Vector3 c = natives::GetFinalRenderedCamCoord();
		const Vector3 r = natives::GetFinalRenderedCamRot(2);
		const float fov = natives::GetFinalRenderedCamFov();
		compositor::set_host_planes(natives::GetFinalRenderedCamNearClip(), natives::GetFinalRenderedCamFarClip());
		const bool inVehicle = natives::IsPedInAnyVehicle(ped, FALSE) != FALSE;
		const bool firstPerson = (!g_drive.on || g_drive.walk) && (inVehicle ? natives::GetFollowVehicleCamViewMode() : natives::GetFollowPedCamViewMode()) == 4;
		const float mcYaw = wrap_degrees(180.0f - r.z), mcPitch = -r.x, mcRoll = r.y;
		compositor::set_host_pose(mcYaw, mcPitch, mcRoll, fov, c.x, c.z + g_yOffset, -c.y);
		compositor::set_camera_locked(g_drive.on);
		if (g_drive.on && g_drive.haveOut)
		{
			// the chase cam as set this frame, and the Steve position it framed: Minecraft draws him exactly there
			const struct { float yaw, pitch; } o = {wrap_degrees(180.0f - g_drive.outHeading), -g_drive.outPitch};
			compositor::set_host_pose(o.yaw, o.pitch, 0.0f, g_drive.outFov, g_drive.outX, g_drive.outZ + g_yOffset, -g_drive.outY);
			sendf("{\"t\":\"cam\",\"f\":%d,\"p\":[%.4f,%.4f,%.4f],\"r\":[%.3f,%.3f,0],\"fov\":%.3f,\"fp\":%s,\"drive\":true,\"walk\":%s,"
				  "\"pl\":[%.4f,%.4f,%.4f],\"look\":[%.3f,%.3f]}",
				natives::GetFrameCount(), g_drive.outX, g_drive.outZ + g_yOffset, -g_drive.outY, o.yaw, o.pitch, g_drive.outFov,firstPerson?"true":"false",g_drive.walk?"true":"false",
				g_drive.steveX, g_drive.steveZ + g_yOffset, -g_drive.steveY, wrap_degrees(180.0f - g_drive.sHeading), -g_drive.sPitch);
		}
		else if (g_drive.on)
			// no position from Minecraft yet: the player is where GTA's is (Minecraft's camera then stays at GTA's)
			sendf("{\"t\":\"cam\",\"f\":%d,\"p\":[%.4f,%.4f,%.4f],\"r\":[%.3f,%.3f,%.3f],\"fov\":%.3f,\"fp\":%s,\"drive\":true,\"walk\":%s,"
				  "\"pl\":[%.4f,%.4f,%.4f],\"look\":[%.3f,%.3f]}",
				natives::GetFrameCount(), c.x, c.z + g_yOffset, -c.y, mcYaw, mcPitch, mcRoll, fov,firstPerson?"true":"false",g_drive.walk?"true":"false",
				p.x, p.z - 1.0f + g_yOffset, -p.y, wrap_degrees(180.0f - g_drive.sHeading), -g_drive.sPitch);
		else
			sendf("{\"t\":\"cam\",\"f\":%d,\"p\":[%.4f,%.4f,%.4f],\"r\":[%.3f,%.3f,%.3f],\"fov\":%.3f,\"fp\":%s,\"pl\":[%.4f,%.4f,%.4f],\"h\":%.3f,\"gun\":%s}",
				natives::GetFrameCount(), c.x, c.z + g_yOffset, -c.y, mcYaw, mcPitch, mcRoll, fov,
				firstPerson ? "true" : "false", p.x, p.z - 1.0f + g_yOffset, -p.y, wrap_degrees(180.0f - natives::GetEntityHeading(ped)),
				g_gun >= 0 ? "true" : "false");

		// no camera motion blur (explosions smear GTA's picture, Minecraft's stays sharp: the two look apart)
		natives::SetGameplayCamMotionBlurScalingThisUpdate(0.0f);
		natives::SetGameplayCamMaxMotionBlurStrengthThisUpdate(0.0f);
		// Keep GTA's minimap/status HUD alongside Minecraft's hotbar and inventory.
		// GTA's idle cinematic camera (30 s without input) never cuts in.
		natives::InvalidateIdleCam();
		natives::InvalidateCinematicVehicleIdleMode();
		if (g_hiddenPlayer != ped)
			natives::SetCurrentPedWeapon(ped, kWeaponUnarmed, TRUE);
		// Story scripts may restore visibility or replace the player during a
		// character switch. Hide the current ped every frame and restore the old one.
		show_player(ped, false);

		// Only an explicit F9 press can hand Minecraft's mouse controls to GTA.
		if (cycleGun && !g_drive.on && !hidden && !g_mcUiOpen && !g_wheelOpen)
			gun_set(ped, g_gun + 1 < int(std::size(kGuns)) ? g_gun + 1 : -1);
		const bool gun = g_gun >= 0 && !g_drive.on;
		for (int control : kDisabledControls)
			if (!gun || std::find(std::begin(kGunControls), std::end(kGunControls), control) == std::end(kGunControls))
				natives::DisableControlAction(0, control, TRUE);
		if (gun)
		{
			// GTA aims and fires; its reticle shows, and its weapon stays visible in Steve's hands
			// GTA's own weapon fires (flash, tracers); the copy in Steve's hands is what shows; the crosshair is ours
			natives::SetPedCurrentWeaponVisible(ped, TRUE);
			if (const Entity w = natives::GetCurrentPedWeaponEntityIndex(ped))
				natives::SetEntityVisible(w, TRUE, FALSE);
			reticle_tick(ped);
		}
		else
		{
			// Story scripts can re-equip GTA weapons; Minecraft owns these clicks.
			natives::SetCurrentPedWeapon(ped, kWeaponUnarmed, TRUE);
			natives::SetPedCurrentWeaponVisible(ped, FALSE);
			if(!g_mcUiOpen && !g_wheelOpen) {
				forward_button(24, "attack");
				forward_button(25, "use");
			}
		}
		aim_cam_tick(gun && natives::IsPlayerFreeAiming(natives::PlayerId()));
		gun_model_tick(ped, gun && !natives::IsPauseMenuActive());
		if(g_mcUiOpen) {
			inventory_input();
			std::string message;
			while(g_ws.poll(message)) handle_event(message);
			return;
		}
		if (natives::IsDisabledControlJustPressed(0, 14) || natives::IsDisabledControlJustPressed(0, 16))
			g_ws.send("{\"t\":\"scroll\",\"d\":-1}");
		if (natives::IsDisabledControlJustPressed(0, 15) || natives::IsDisabledControlJustPressed(0, 17))
			g_ws.send("{\"t\":\"scroll\",\"d\":1}");
		for (int i = 0; i < 9; ++i)
			if (natives::IsDisabledControlJustPressed(0, kHotbarControls[i]))
				sendf("{\"t\":\"slot\",\"n\":%d}", i);

		screen_fx_tick();
		mobs_tick(ped);
		hell_tick(ped);
		hot_tick(ped);
		hell_lights(ped);
		if (g_haveOffset)
		{
			if (!g_drive.on)
				maybe_relevel(p);
			sample_ground(p);
			if (!g_mobs.empty())
				sample_patches();
			props_tick();
		}

		std::string message;
		while (g_ws.poll(message))
			handle_event(message);
	}

	void script_main()
	{
		bool onlineWarning = false;
		while (true)
		{
			// Read-only mode checks precede sockets, events, input and world edits.
			// ScriptHookV itself also rejects Online; this is an additional guard.
			if (natives::NetworkIsSessionStarted() || natives::NetworkIsGameInProgress())
			{
				compositor::set_active(false);
				g_enabled = false;
				g_toggle = false;
				if (g_started)
				{
					g_ws.stop();
					g_started = false;
				}
				if (!onlineWarning)
					natives::Notify("MINECRAFT PASSTHROUGH DISABLED IN GTA ONLINE");
				onlineWarning = true;
				WAIT(0);
				continue;
			}
			onlineWarning = false;
			if (!g_started)
			{
				g_started = true;
				g_ws.start("127.0.0.1", kPort);
			}
			compositor::try_register(g_module);
			tick();
			WAIT(0);
		}
	}

	void on_keyboard(DWORD key, WORD scan, BYTE, BOOL, BOOL, BOOL wasDownBefore, BOOL isUpNow)
	{
		if (isUpNow)
			return;
		if (key == VK_ESCAPE && !wasDownBefore && !g_mcUiOpen)
			compositor::request_pause_menu();
		if(g_mcUiOpen) {
			if(key==VK_ESCAPE) { g_inventoryClose=true; return; }
			if(key!=VK_F7 && key!=VK_F8 && key!=VK_F9) {
				BYTE state[256]{}; WCHAR chars[4]{};
				GetKeyboardState(state);
				int count=ToUnicodeEx(key,scan,state,chars,4,0,GetKeyboardLayout(0));
				sendf("{\"t\":\"ui_key\",\"vk\":%u,\"mods\":%d,\"char\":%u}",key,ui_modifiers(),count>0?unsigned(chars[0]):0);
				return;
			}
		}
		if(wasDownBefore) return;
		if(key==VK_SPACE) { g_jumpPresses.fetch_add(1); return; }
		if(key=='E') { g_inventoryToggle=true; return; }
		if (key == VK_F7)
			g_toggle = true;
		else if (key == VK_F8)
			g_relevel = true;
		else if (key == VK_F9)
			g_cycleGun = true;
	}
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
	switch (reason)
	{
	case DLL_PROCESS_ATTACH:
		g_module = module;
		scriptRegister(module, script_main);
		keyboardHandlerRegister(on_keyboard);
		break;
	case DLL_PROCESS_DETACH:
		compositor::unregister(module);
		scriptUnregister(module);
		keyboardHandlerUnregister(on_keyboard);
		break;
	}
	return TRUE;
}
