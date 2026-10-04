package dev.rehan.passthrough.client;

import com.mojang.blaze3d.pipeline.RenderTarget;
import com.mojang.blaze3d.resource.GraphicsResourceAllocator;
import dev.rehan.passthrough.client.mixin.AchromaPostFxAccessor;
import dev.rehan.passthrough.client.mixin.AchromaScreenFxAccessor;
import dev.rehan.passthrough.Passthrough;
import java.util.Locale;
import net.fabricmc.loader.api.FabricLoader;

/** Keep Achroma's opaque screen shader in the depth-tested world layer, never in the HUD overlay. */
public final class AchromaEffects {
	private static boolean worldPass;
	private static long nextGrade;

	private AchromaEffects() { }

	public static boolean worldPass() {
		return worldPass;
	}

	public static void applyWorld(final RenderTarget target, final GraphicsResourceAllocator allocator) {
		if (HostState.frame() == null || !FabricLoader.getInstance().isModLoaded("achroma")) return;
		long now = System.nanoTime();
		if (now >= nextGrade) {
			nextGrade = now + 50_000_000L;
			Passthrough.events.accept(String.format(Locale.ROOT, "{\"t\":\"achromafx\",\"drain\":%.4f}", AchromaScreenFxAccessor.passthrough$drain()));
		}
		worldPass = true;
		try {
			AchromaPostFxAccessor.passthrough$apply(target, allocator);
		} finally {
			worldPass = false;
		}
	}
}
