#include "../src/pause_overlay.h"
#include <cassert>
#include <cstdio>

int main() {
	PauseOverlay gate;
	assert(gate.visible());
	gate.request(1000);
	assert(!gate.visible()); // Even if the script never gets its next tick.
	gate.update(false,1100);
	assert(!gate.visible()); // GTA has not finished opening the menu.
	gate.update(true,1200);
	assert(!gate.visible());
	assert(!gate.visible()); // Submenus stay hidden without script updates.
	gate.request(6000); // Escape to resume.
	gate.update(false,6050);
	assert(!gate.visible());
	gate.update(false,6500);
	assert(gate.visible());
	gate.request(7000);
	gate.update(true,7500);
	gate.update(false,9000); // Resume by menu click, without another Escape.
	assert(gate.visible());
	std::puts("Pause overlay entry, held menu and resume passed.");
}
