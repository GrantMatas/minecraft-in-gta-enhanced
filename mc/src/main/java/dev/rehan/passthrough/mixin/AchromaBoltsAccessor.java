package dev.rehan.passthrough.mixin;

import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Pseudo;
import org.spongepowered.asm.mixin.gen.Invoker;

@Pseudo
@Mixin(targets = "com.agoni.achroma.strike.StrikeBolts", remap = false)
public interface AchromaBoltsAccessor {
	@Invoker(value = "fireTick", remap = false)
	static int passthrough$fireTick(int index) { throw new AssertionError(); }
	@Invoker(value = "hitX", remap = false)
	static float passthrough$hitX(int seed, int index, float radius) { throw new AssertionError(); }
	@Invoker(value = "hitZ", remap = false)
	static float passthrough$hitZ(int seed, int index, float radius) { throw new AssertionError(); }
}
