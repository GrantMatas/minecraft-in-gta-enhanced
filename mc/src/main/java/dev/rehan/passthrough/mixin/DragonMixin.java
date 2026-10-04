package dev.rehan.passthrough.mixin;

import net.minecraft.core.BlockPos;
import net.minecraft.world.entity.boss.enderdragon.EnderDragon;
import net.minecraft.world.level.levelgen.Heightmap;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.ModifyArgs;
import org.spongepowered.asm.mixin.injection.invoke.arg.Args;

/** Keep the vanilla dragon's flight paths around its GTA spawn point. */
@Mixin(EnderDragon.class)
abstract class DragonMixin {
	@ModifyArgs(method = "findClosestNode()I", at = @At(value = "INVOKE",
		target = "Lnet/minecraft/world/level/pathfinder/Node;<init>(III)V"))
	private void passthrough$localFlightNodes(final Args args) {
		EnderDragon dragon = (EnderDragon) (Object) this;
		if (!dragon.entityTags().contains("gta_dragon")) return;
		BlockPos origin = dragon.getFightOrigin();
		int x = origin.getX() + (int) args.get(0), z = origin.getZ() + (int) args.get(2);
		int ground = dragon.level().getHeightmapPos(Heightmap.Types.MOTION_BLOCKING_NO_LEAVES, new BlockPos(x, 0, z)).getY();
		args.set(0, x);
		args.set(1, Math.max(origin.getY() + 35, ground + 10));
		args.set(2, z);
	}
}
