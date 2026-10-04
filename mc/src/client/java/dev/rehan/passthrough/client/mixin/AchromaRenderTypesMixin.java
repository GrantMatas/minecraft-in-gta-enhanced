package dev.rehan.passthrough.client.mixin;

import com.mojang.renderpearl.api.pipeline.CompareOp;
import com.mojang.renderpearl.api.pipeline.DepthStencilState;
import net.minecraft.client.renderer.oit.OitPipelineSet;
import net.minecraft.client.renderer.rendertype.RenderSetup;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Pseudo;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.ModifyArg;
import org.spongepowered.asm.mixin.injection.Redirect;

/** The host compositor needs the strike's surface depth, including its additive glow passes. */
@Pseudo
@Mixin(targets = "com.agoni.achroma.client.render.AchromaRenderTypes", remap = false)
abstract class AchromaRenderTypesMixin {
	// OIT's separate accumulation pipelines leave no surface in the exported
	// depth buffer. Use the regular glow pipelines, which write host-visible depth.
	@Redirect(method = {"lambda$static$0", "lambda$static$2", "lambda$static$4"},
		at = @At(value = "INVOKE", target = "Lnet/minecraft/client/renderer/rendertype/RenderSetup$RenderSetupBuilder;setOitPipelines(Lnet/minecraft/client/renderer/oit/OitPipelineSet;)Lnet/minecraft/client/renderer/rendertype/RenderSetup$RenderSetupBuilder;"), remap = false)
	private static RenderSetup.RenderSetupBuilder passthrough$exportableGlow(RenderSetup.RenderSetupBuilder builder, OitPipelineSet pipelines) {
		return builder;
	}
	@ModifyArg(method = "<clinit>", at = @At(value = "INVOKE",
		target = "Lcom/mojang/renderpearl/api/pipeline/RenderPipeline$Builder;withDepthStencilState(Lcom/mojang/renderpearl/api/pipeline/DepthStencilState;)Lcom/mojang/renderpearl/api/pipeline/RenderPipeline$Builder;"),
		index = 0, remap = false)
	private static DepthStencilState passthrough$strikeDepth(final DepthStencilState state) {
		if (state.writeDepth()) return state;
		// Include glow depth in the exported frame, and keep the x-ray glow behind nearer geometry.
		return new DepthStencilState(CompareOp.GREATER_THAN_OR_EQUAL, true,
			state.depthBiasScaleFactor(), state.depthBiasConstant());
	}
}
