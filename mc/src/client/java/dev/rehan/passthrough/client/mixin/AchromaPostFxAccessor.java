package dev.rehan.passthrough.client.mixin;

import com.mojang.blaze3d.pipeline.RenderTarget;
import com.mojang.blaze3d.resource.GraphicsResourceAllocator;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Pseudo;
import org.spongepowered.asm.mixin.gen.Invoker;

@Pseudo
@Mixin(targets = "com.agoni.achroma.client.fx.PostFx", remap = false)
public interface AchromaPostFxAccessor {
	@Invoker(value = "apply", remap = false)
	static void passthrough$apply(final RenderTarget target, final GraphicsResourceAllocator allocator) {
		throw new AssertionError("Mixin invoker not applied");
	}
}
