package dev.rehan.passthrough.client;

import com.google.gson.JsonObject;
import dev.rehan.passthrough.Passthrough;
import dev.rehan.passthrough.client.mixin.KeyMappingAccessor;
import net.minecraft.client.KeyMapping;
import net.minecraft.client.Minecraft;
import net.minecraft.client.player.LocalPlayer;
import net.minecraft.client.gui.screens.inventory.InventoryScreen;
import net.minecraft.client.input.MouseButtonInfo;
import net.minecraft.client.input.MouseButtonEvent;
import net.minecraft.client.input.KeyEvent;
import net.minecraft.client.input.CharacterEvent;
import net.minecraft.core.registries.BuiltInRegistries;
import net.minecraft.world.entity.player.Inventory;
import org.lwjgl.sdl.SDLVideo;

/** Host input, applied on the client thread: the host window has the focus, so Minecraft never sees these itself. */
final class ClientInput {
	private static int dragButton = -1;
	/** Host modifiers use Shift=1/Ctrl=2/Alt=4; Minecraft 26.3 uses SDL's modifier bits. */
	private static int sdlModifiers(final int modifiers) {
		return ((modifiers & 1) != 0 ? 0x0003 : 0) | ((modifiers & 2) != 0 ? 0x00c0 : 0)
			| ((modifiers & 4) != 0 ? 0x0300 : 0);
	}
	private ClientInput() {
	}

	static void handle(final Minecraft minecraft, final JsonObject m) {
		LocalPlayer player = minecraft.player;
		switch (m.get("t").getAsString()) {
			case "ui_mouse" -> {
				if (minecraft.gui.screen() == null) return;
				var window = minecraft.getWindow();
				// MouseHandler scales logical window coordinates, not framebuffer pixels (which differ with DPI).
				double x = Math.clamp(m.get("x").getAsDouble(), 0.0, 1.0) * window.getScreenWidth();
				double y = Math.clamp(m.get("y").getAsDouble(), 0.0, 1.0) * window.getScreenHeight();
				double oldX = minecraft.mouseHandler.xpos(), oldY = minecraft.mouseHandler.ypos();
				minecraft.mouseHandler.onMove(window.handle(), x, y, x-oldX, y-oldY);
				int modifiers = sdlModifiers(m.has("mods") ? m.get("mods").getAsInt() : 0);
				if (dragButton >= 0 && !m.has("button")) {
					double sx = minecraft.mouseHandler.getScaledXPos(window);
					double sy = minecraft.mouseHandler.getScaledYPos(window);
					minecraft.gui.screen().mouseDragged(new MouseButtonEvent(sx, sy, new MouseButtonInfo(dragButton, modifiers)),
						(x-oldX) * window.getGuiScaledWidth()/window.getScreenWidth(), (y-oldY) * window.getGuiScaledHeight()/window.getScreenHeight());
				}
				if (m.has("button")) {
					// The bridge uses 0=left/1=right/2=middle. SDL uses 1=left/3=right/2=middle.
					int button = switch (m.get("button").getAsInt()) { case 0 -> 1; case 1 -> 3; case 2 -> 2; default -> -1; };
					if (button < 0) return;
					boolean down = m.get("down").getAsBoolean();
					minecraft.mouseHandler.onButton(window.handle(), new MouseButtonInfo(button, modifiers), down ? 1 : 0);
					dragButton = down ? button : -1;
				}
				if (m.has("scroll")) minecraft.mouseHandler.onScroll(window.handle(), 0, m.get("scroll").getAsDouble());
			}
			case "ui_key" -> {
				if (minecraft.gui.screen() == null) return;
				int vk = m.get("vk").getAsInt();
				int keyCode = switch (vk) {
					case 37 -> 0x40000050; case 38 -> 0x40000052; case 39 -> 0x4000004f; case 40 -> 0x40000051;
					case 46 -> 127; case 36 -> 0x4000004a; case 35 -> 0x4000004d;
					default -> vk >= 65 && vk <= 90 ? vk + 32 : vk;
				};
				minecraft.gui.screen().keyPressed(new KeyEvent(keyCode, 0, sdlModifiers(m.get("mods").getAsInt())));
				if (m.has("char") && m.get("char").getAsInt() > 0 && minecraft.gui.screen() != null) minecraft.gui.screen().charTyped(new CharacterEvent(m.get("char").getAsInt()));
			}
			case "key" -> {
				String k = m.get("k").getAsString();
				boolean down = !m.has("down") || m.get("down").getAsBoolean();
				if (k.equals("inventory") && down && player != null) {
					minecraft.options.keyAttack.setDown(false);
					minecraft.options.keyUse.setDown(false);
					dragButton = -1;
					if (minecraft.gui.screen() == null) minecraft.gui.setScreen(new InventoryScreen(player));
					else minecraft.gui.screen().onClose();
					return;
				}
				if (k.equals("escape")) {
					dragButton = -1;
					if (down && minecraft.gui.screen() != null) {
						minecraft.gui.screen().onClose();
					}

					return;
				}

				KeyMapping key = switch (k) {
					case "forward" -> minecraft.options.keyUp;
					case "back" -> minecraft.options.keyDown;
					case "left" -> minecraft.options.keyLeft;
					case "right" -> minecraft.options.keyRight;
					case "jump" -> minecraft.options.keyJump;
					case "sprint" -> minecraft.options.keySprint;
					case "sneak" -> minecraft.options.keyShift;
					case "use" -> minecraft.options.keyUse;
					case "attack" -> minecraft.options.keyAttack;
					case "pick" -> minecraft.options.keyPickItem;
					case "inventory" -> minecraft.options.keyInventory;
					case "drop" -> minecraft.options.keyDrop;
					case "swap" -> minecraft.options.keySwapOffhand;
					default -> null;
				};
				if (k.equals("attack") && down && player != null
					&& BuiltInRegistries.ITEM.getKey(player.getMainHandItem().getItem()).getPath().endsWith("_sword")) {
					// a sword swing: the host hits what's in front of Steve in its own world
					Passthrough.events.accept("{\"t\":\"melee\"}");
				}

				if (key != null) {
					if (down && !key.isDown()) {
						KeyMappingAccessor access = (KeyMappingAccessor)key;
						access.passthrough$setClickCount(access.passthrough$getClickCount() + 1);
					}

					key.setDown(down);
				}
			}
			case "slot" -> {
				if (player != null) {
					player.getInventory().setSelectedSlot(Math.clamp(m.get("n").getAsInt(), 0, Inventory.getSelectionSize() - 1));
				}
			}
			case "scroll" -> {
				if (player != null) {
					Inventory inventory = player.getInventory();
					int size = Inventory.getSelectionSize();
					inventory.setSelectedSlot(Math.floorMod(inventory.getSelectedSlot() - m.get("d").getAsInt(), size));
				}
			}
			case "hud" -> {
				if (minecraft.gui.hud.isHidden() != m.get("hidden").getAsBoolean()) {
					minecraft.gui.hud.toggle();
				}
			}
			case "view" -> {
				// match the host's picture exactly: un-minimize/un-maximize first (resizing a maximized window is ignored)
				int w = m.get("w").getAsInt(), h = m.get("h").getAsInt();
				long handle = minecraft.getWindow().handle();
				SDLVideo.SDL_RestoreWindow(handle);
				minecraft.getWindow().setWindowed(w, h);
				SDLVideo.SDL_SetWindowSize(handle, w, h);
				SDLVideo.SDL_SyncWindow(handle);
			}
			default -> {
			}
		}
	}

	static void releaseMovement(final Minecraft minecraft) {
		for (KeyMapping key : new KeyMapping[] {minecraft.options.keyUp, minecraft.options.keyDown,
			minecraft.options.keyLeft, minecraft.options.keyRight, minecraft.options.keyJump,
			minecraft.options.keySprint, minecraft.options.keyShift}) key.setDown(false);
	}
}
