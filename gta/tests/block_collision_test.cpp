#include "../src/block_collision.h"
#include <cassert>
#include <cstdio>
int main() {
    using namespace block_collision;
    const Point wall{0,0,0};
    auto hit=sweep({-2,.5f,0},{2,.5f,0},wall,1.65f);
    assert(hit.axis==0 && hit.time>.4f && hit.time<.45f); // Running across a wall.
    assert(sweep({-2,.5f,1},{2,.5f,1},wall,1.65f).axis<0); // Walk on top.
    assert(sweep({-.31f,-2,0},{-.31f,2,0},wall,1.65f).axis<0); // Slide alongside.
    assert(sweep({-2,.5f,1.1f},{2,.5f,1.1f},wall,1.65f).axis<0); // Clear a block while jumping.
    assert(sweep({.5f,.5f,3},{.5f,.5f,-2},wall,1.65f).axis==2); // Land on it.
    assert(sweep({-20,.5f,.2f},{20,.5f,.2f},wall,.65f).axis==0); // Fast elytra cannot tunnel.
    assert(sweep({.5f,.5f,.2f},{2,.5f,.2f},wall,1.65f).axis<0); // Escape a newly placed block.
    assert(sweep({-2,.5f,-1},{2,.5f,-1},wall,1.65f).axis==0); // Head strikes an overhang.
    assert(sweep({-2,2,0},{2,2,0},wall,1.65f).axis<0); // Unrelated blocks do not stop movement.
    assert(walk_sweep({-2,.5f,.81f},{2,.5f,.81f},wall,1.65f).axis<0); // GTA prop top: cross a block seam.
    assert(walk_sweep({-2,.5f,0},{2,.5f,0},wall,1.65f).axis==0); // Still stop at the side while walking.
    assert(walk_sweep({.5f,.5f,1.5f},{.5f,.5f,.2f},wall,1.65f).axis==2); // Falling still collides with the top.
    assert(walk_sweep({.5f,.5f,1.5f},{.5f,.5f,.75f},wall,1.65f).axis==2); // A short fall cannot slip inside the lid.
    assert(walk_sweep({-2,.5f,.7f},{2,.5f,.7f},wall,1.65f).axis==0); // Below the prop top, the wall remains solid.
    assert(walk_sweep({-.31f,.5f,.79f},{.4f,.5f,.77f},wall,1.65f,true).axis<0); // Native prop contact must not trigger a second correction.
    assert(walk_sweep({-2,.5f,0},{2,.5f,0},wall,1.65f,false).axis==0); // Unspawned blocks retain fallback collision.
    std::puts("Block collision sweeps passed.");
}
