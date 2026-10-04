package dev.rehan.passthrough.mixin;

import dev.rehan.passthrough.Passthrough;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Pseudo;
import org.spongepowered.asm.mixin.injection.Constant;
import org.spongepowered.asm.mixin.injection.ModifyConstant;

@Pseudo
@Mixin(targets = "com.agoni.achroma.strike.StrikeManager", remap = false)
abstract class AchromaCountdownMixin {
	@ModifyConstant(method = "countdown", constant = @Constant(floatValue = 20.0f), remap = false)
	private static float passthrough$countdownSeconds(float original) {
		return Passthrough.active ? 14.0f : original;
	}
}
