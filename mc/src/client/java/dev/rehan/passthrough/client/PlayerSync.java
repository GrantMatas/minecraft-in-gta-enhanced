package dev.rehan.passthrough.client;

import net.minecraft.client.CameraType;
import net.minecraft.client.Minecraft;
import dev.rehan.passthrough.Passthrough;
import dev.rehan.passthrough.WorldBridge;
import java.util.Locale;
import net.minecraft.client.player.LocalPlayer;
import net.minecraft.world.entity.player.Abilities;
import net.minecraft.world.phys.Vec3;
import net.minecraft.util.Mth;

/** Keeps the Minecraft player on the host's player: it stands where they stand and looks where the host camera looks. */
public final class PlayerSync {
	private static final double TELEPORT_SQ = 64.0 * 64.0;
	/** How far the host's player moved over the last client tick (drives the walk animation). */
	private static float tickDistance;
	private static double lastX = Double.NaN, lastZ;
	private static boolean walking;
	private static long headFrame;
	private static float headYaw, headPitch;

	private PlayerSync() {
	}

	public static float tickDistance() {
		return tickDistance;
	}

	public static float headYaw(final float bodyYaw) {
		return Mth.clamp(Mth.wrapDegrees(headYaw - bodyYaw), -45.0F, 45.0F);
	}

	public static float headPitch() {
		return headPitch;
	}

	private static void smoothHead(final HostState.Pose p) {
		long now = System.nanoTime();
		float targetYaw = p.bodyYaw() + Mth.clamp(Mth.wrapDegrees(p.yaw() - p.bodyYaw()) * 0.35F, -35.0F, 35.0F);
		float targetPitch = Mth.clamp(p.pitch() * 0.5F, -25.0F, 25.0F);
		if (headFrame == 0 || now - headFrame > 500_000_000L) {
			headYaw = targetYaw;
			headPitch = targetPitch;
		} else {
			float alpha = (float)(1.0 - Math.exp(-Math.min((now - headFrame) * 1.0e-9, 0.1) * 7.0));
			headYaw += Mth.wrapDegrees(targetYaw - headYaw) * alpha;
			headPitch += (targetPitch - headPitch) * alpha;
		}
		headFrame = now;
	}

	/** Every frame, before the camera update: position, rotation, and first/third person to match the host. */
	public static void frame(final float partialTick) {
		HostState.Pose p = HostState.frame();
		Minecraft minecraft = Minecraft.getInstance();
		LocalPlayer player = minecraft.player;
		if (p == null || player == null) {
			headFrame = 0;
			return;
		}
		if (!p.drive() && !p.firstPerson() && !p.gun()) smoothHead(p);
		else headFrame = 0;

		if (p.drive()) {
			// Minecraft owns movement; the host supplies the look direction and follows the result.
			if (p.walk() && !walking) return; // wait for the tick to initialize the new location
			player.setYRot(p.lookYaw());
			player.setXRot(p.lookPitch());
			player.yRotO = p.lookYaw();
			player.xRotO = p.lookPitch();
			player.yHeadRot = player.yHeadRotO = p.lookYaw();
			CameraType cameraType = p.firstPerson() ? CameraType.FIRST_PERSON : CameraType.THIRD_PERSON_BACK;
			if (minecraft.options.getCameraType() != cameraType) {
				minecraft.options.setCameraType(cameraType);
			}
			// airborne: with no collision onGround never updates, and the server cancels a grounded player's glide
			if (!p.walk()) player.setOnGround(false);

			// where the player is drawn this frame, how fast that moves (the slope of the tick interpolation, per
			// second) and when (System.nanoTime is the host's QueryPerformanceCounter clock): the host carries it forward
			Vec3 at = player.getPosition(partialTick);
			double vx = (player.getX() - player.xo) * 20.0, vy = (player.getY() - player.yo) * 20.0, vz = (player.getZ() - player.zo) * 20.0;
			Passthrough.events.accept(String.format(Locale.ROOT, "{\"t\":\"mcpos\",\"pos\":[%.4f,%.4f,%.4f],\"vel\":[%.3f,%.3f,%.3f],\"tn\":%d,\"fly\":%b,\"eye\":%.3f}",
				at.x, at.y, at.z, vx, vy, vz, System.nanoTime(), player.isFallFlying(), player.getEyeHeight()));
			return;
		}

		player.setYRot(p.yaw());
		player.setXRot(p.pitch());
		player.yRotO = p.yaw();
		player.xRotO = p.pitch();
		player.yHeadRot = player.yHeadRotO = p.yaw();
		player.yBodyRot = player.yBodyRotO = p.firstPerson() ? p.yaw() : p.bodyYaw();
		// the model stands exactly where the host's player is this frame (not a tick behind, interpolating)
		double x = p.firstPerson() ? p.x() : p.px();
		double y = p.firstPerson() ? p.y() - player.getEyeHeight() : p.py();
		double z = p.firstPerson() ? p.z() : p.pz();
		player.setPos(x, y, z);
		player.xo = player.xOld = x;
		player.yo = player.yOld = y;
		player.zo = player.zOld = z;
		CameraType cameraType = p.firstPerson() ? CameraType.FIRST_PERSON : CameraType.THIRD_PERSON_BACK;
		if (minecraft.options.getCameraType() != cameraType) {
			minecraft.options.setCameraType(cameraType);
		}
	}

	/**
	 * Every client tick, at the start of the player's tick (the old position is already saved, so the model
	 * interpolates and walks): in first person the player's eyes are at the host camera, in third person
	 * their feet are at the host player's.
	 */
	public static void tick(final LocalPlayer player) {
		HostState.Pose p = HostState.live();
		boolean walk = p != null && p.drive() && p.walk();
		if (walk != walking) {
			walking = walk;
			ClientInput.releaseMovement(Minecraft.getInstance());
			if (walk) {
				player.setPos(p.px(), p.py(), p.pz());
				player.xo = player.xOld = p.px();
				player.yo = player.yOld = p.py();
				player.zo = player.zOld = p.pz();
				player.setDeltaMovement(Vec3.ZERO);
				player.noPhysics = false;
			}
			WorldBridge.movement(walk, player.getX(), player.getY(), player.getZ());
		}
		if (walk) {
			player.getAbilities().mayfly = false;
			player.getAbilities().flying = false;
			player.setYRot(p.lookYaw());
			player.setXRot(p.lookPitch());
			if (Minecraft.getInstance().gui.screen() != null) ClientInput.releaseMovement(Minecraft.getInstance());
		}
		if (p == null || p.drive()) {
			if (p == null) ClientInput.releaseMovement(Minecraft.getInstance());
			if (p != null && !p.walk() && player.isFallFlying()) {
				// A modest cruise assist, preserving vanilla steering and faster firework boosts.
				Vec3 velocity = player.getDeltaMovement();
				double speed = velocity.horizontalDistance();
				if (speed > 0.05 && speed < 1.2) {
					double scale = (speed + (1.2 - speed) * 0.08) / speed;
					player.setDeltaMovement(velocity.x * scale, velocity.y, velocity.z * scale);
				}
			}
			return;
		}

		double x = p.firstPerson() ? p.x() : p.px();
		double y = p.firstPerson() ? p.y() - player.getEyeHeight() : p.py();
		double z = p.firstPerson() ? p.z() : p.pz();
		tickDistance = Double.isNaN(lastX) ? 0.0F : (float) Math.min(Math.hypot(x - lastX, z - lastZ), 1.0);
		lastX = x;
		lastZ = z;
		boolean teleport = player.distanceToSqr(x, y, z) > TELEPORT_SQ;
		player.setPos(x, y, z);
		if (teleport) {
			player.xo = player.xOld = x;
			player.yo = player.yOld = y;
			player.zo = player.zOld = z;
		}

		player.setDeltaMovement(Vec3.ZERO);
		Abilities abilities = player.getAbilities();
		if (abilities.mayfly && !abilities.flying) {
			abilities.flying = true;
			player.onUpdateAbilities();
		}
	}
}
