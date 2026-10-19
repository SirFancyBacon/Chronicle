#pragma once

class CEditGround;

// Whether video.shadow_distance reaches past the map's shadow_far, where retail's fainter far band
// ends and a part stops casting. Shadows are then extended: every part out to shadow_distance
// (forever at 0) casts in one shade whatever its distance, and the fog fades them.
bool EdGroundShadowsExtended(float shadow_far);

// The shadows (and shades) of the parts whose distance from the eye lies between near_distance and
// far_distance, cut off at video.shadow_distance: extended shadows.
void EdGroundDrawShadowBetween(CEditGround *ground, float near_distance, float far_distance);
