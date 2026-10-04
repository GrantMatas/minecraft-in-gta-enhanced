package dev.rehan.passthrough.mixin;

import dev.rehan.passthrough.Passthrough;
import dev.rehan.passthrough.WorldBridge;
import dev.rehan.passthrough.AchromaTiming;
import java.util.Locale;
import net.minecraft.server.level.ServerLevel;
import net.minecraft.world.entity.Entity;
import net.minecraft.network.syncher.EntityDataAccessor;
import org.spongepowered.asm.mixin.Final;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Pseudo;
import org.spongepowered.asm.mixin.Shadow;
import org.spongepowered.asm.mixin.Unique;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.Redirect;
import org.spongepowered.asm.mixin.injection.Coerce;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

/** Achroma carves/damages directly, so its impact needs the same host event as vanilla explosions. */
@Pseudo
@Mixin(targets = "com.agoni.achroma.strike.StrikeEntity", remap = false)
abstract class AchromaStrikeMixin {
	@Shadow @Final private static EntityDataAccessor<Long> START_TICK;
	@Shadow public abstract long getStartTick();
	@Shadow public abstract float getRadius();
	@Shadow public abstract int getSeed();
	@Shadow public abstract boolean sideBolts();
	@Shadow public abstract boolean blastWave();
	@Unique private boolean passthrough$impactSent;
	@Unique private int passthrough$lastAge = -1;

	// Bolt geometry must keep a fixed seed while the buildup clock is stretched.
	@Redirect(method = "getSeed", at = @At(value = "INVOKE", target = "Lcom/agoni/achroma/strike/StrikeEntity;getStartTick()J"), remap = false)
	private long passthrough$stableSeed(@Coerce Object strike) {
		return ((Entity)strike).getEntityData().get(START_TICK);
	}

	@Inject(method = "getStartTick", at = @At("RETURN"), cancellable = true, remap = false)
	private void passthrough$fiveSeconds(CallbackInfoReturnable<Long> ci) {
		if (!Passthrough.active) return;
		Entity strike = (Entity)(Object)this;
		long now = strike.level().getGameTime();
		ci.setReturnValue(now - AchromaTiming.phaseAge(now - ci.getReturnValue()));
	}

	@Inject(method = "tick", at = @At("TAIL"), remap = false)
	private void passthrough$impact(final CallbackInfo ci) {
		Entity strike = (Entity)(Object)this;
		if (!(strike.level() instanceof ServerLevel) || !Passthrough.active) return;
		int age = (int)(strike.level().getGameTime() - getStartTick());
		// Achroma 1.1.0's beam hits at tick 70 of its server timeline.
		if (!passthrough$impactSent && age >= 70) {
			passthrough$impactSent = true;
			WorldBridge.onExplosion(strike.position(), getRadius(), "achroma");
			Passthrough.LOG.info("Achroma strike bridged at {}", strike.position());
		}
		for (int tick = Math.max(0, passthrough$lastAge + 1); tick <= Math.min(age, 164); ++tick) {
			if (sideBolts()) for (int i = 0; i < 26; ++i) {
				if (tick != AchromaBoltsAccessor.passthrough$fireTick(i)) continue;
				WorldBridge.onExplosion(strike.position().add(AchromaBoltsAccessor.passthrough$hitX(getSeed(), i, getRadius()), 0,
					AchromaBoltsAccessor.passthrough$hitZ(getSeed(), i, getRadius())), Math.max(3.5f, Math.min(7.0f, getRadius() * 0.24f)), "achroma_bolt");
			}
			if (blastWave() && tick >= 70 && tick <= 100 && tick % 2 == 0) {
				float progress = (tick - 70) / 30.0f;
				passthrough$wave(strike, getRadius() * 2.2f * (1 - (1 - progress) * (1 - progress)), "blast");
			}
			if (tick >= 150 && tick <= 164 && tick % 2 == 0) {
				float progress = (tick - 150) / 14.0f;
				passthrough$wave(strike, getRadius() * 2.9f * (1 - (float)Math.pow(1 - progress, 3)), "nova");
			}
		}
		passthrough$lastAge = age;
	}

	@Unique private void passthrough$wave(Entity strike, float radius, String wave) {
		Passthrough.events.accept(String.format(Locale.ROOT,
			"{\"t\":\"achromawave\",\"id\":%d,\"wave\":\"%s\",\"pos\":[%.3f,%.3f,%.3f],\"r\":%.3f}",
			strike.getId(), wave, strike.getX(), strike.getY(), strike.getZ(), radius));
	}
}
