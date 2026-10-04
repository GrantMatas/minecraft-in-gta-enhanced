package dev.rehan.passthrough.client.mixin;

import com.mojang.blaze3d.pipeline.RenderTarget;
import com.mojang.blaze3d.resource.GraphicsResourceAllocator;
import dev.rehan.passthrough.client.AchromaEffects;
import dev.rehan.passthrough.client.HostState;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Pseudo;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Pseudo
@Mixin(targets = "com.agoni.achroma.client.fx.PostFx", remap = false)
abstract class AchromaPostFxMixin {
	@Inject(method = "apply", at = @At("HEAD"), cancellable = true, remap = false)
	private static void passthrough$worldOnly(final RenderTarget target,
		final GraphicsResourceAllocator allocator, final CallbackInfo ci) {
		if (HostState.frame() != null && !AchromaEffects.worldPass()) ci.cancel();
	}
}
