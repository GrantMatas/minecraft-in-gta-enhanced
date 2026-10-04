#pragma once
#include <atomic>
#include <cstdint>

// ScriptHook can stop ticking in GTA menus. The keyboard callback hides the
// render layer immediately; a resumed script confirms when gameplay is back.
class PauseOverlay {
	std::atomic<bool> hidden{false};
	std::atomic<uint64_t> requestedAt{0};
public:
	void request(uint64_t now) { requestedAt = now; hidden = true; }
	void update(bool paused, uint64_t now) {
		if (paused) hidden = true;
		else if (now - requestedAt.load() >= 500) hidden = false;
	}
	bool visible() const { return !hidden.load(); }
};
