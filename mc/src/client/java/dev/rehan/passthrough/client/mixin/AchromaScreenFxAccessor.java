package dev.rehan.passthrough.client.mixin;

import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Pseudo;
import org.spongepowered.asm.mixin.gen.Invoker;

@Pseudo
@Mixin(targets = "com.agoni.achroma.client.fx.ScreenFx", remap = false)
public interface AchromaScreenFxAccessor {
	@Invoker(value = "drain", remap = false)
	static float passthrough$drain() { throw new AssertionError("Mixin invoker not applied"); }
}
