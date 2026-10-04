#pragma once
#include <algorithm>
#include <cmath>

namespace block_collision {
struct Point { float x, y, z; };
struct Hit { float time = 1.0f; int axis = -1; };

// Sweep feet against a unit block expanded by the body dimensions. Open
// boundaries allow walking on the floor and sliding along a wall.
inline Hit sweep(Point from, Point to, Point block, float height, float blockHeight = 1.0f) {
    constexpr float width = 0.30f, gap = 0.002f;
    const float start[] = {from.x, from.y, from.z};
    const float delta[] = {to.x-from.x, to.y-from.y, to.z-from.z};
    const float low[] = {block.x-width+gap, block.y-width+gap, block.z-height+gap};
    const float high[] = {block.x+1+width-gap, block.y+1+width-gap, block.z+blockHeight-gap};
    float enter = -1.0f, leave = 1.0f;
    int axis = -1;
    for (int i=0; i<3; ++i) {
        if (std::fabs(delta[i]) < 0.000001f) {
            if (start[i] <= low[i] || start[i] >= high[i]) return {};
            continue;
        }
        float a = (low[i]-start[i])/delta[i], b = (high[i]-start[i])/delta[i];
        if (a>b) std::swap(a,b);
        if (a>enter) { enter=a; axis=i; }
        leave=std::min(leave,b);
        if (enter>=leave) return {};
    }
    // An existing overlap, such as a newly placed block, must not trap the player.
    if (enter<0 || enter>=1 || axis<0) return {};
    return {enter,axis};
}

// Match the GTA block prop's 0.80 m top, so crossing seams remains possible and
// falling onto a block still finds a solid floor even without a physical prop.
inline Hit walk_sweep(Point from, Point to, Point block, float height, bool nativeCollision = false) {
    // GTA owns contact with an existing prop, including its actual foot offset
    // and climb tasks. A second approximate capsule must not fight that contact.
    if (nativeCollision) return {};
    return sweep(from,to,block,height,0.80f);
}
}
