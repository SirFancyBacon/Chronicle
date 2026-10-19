#pragma once

class CEditGround;

// Whether video.shadow_distance reaches past the map's shadow_far, where retail's fainter far band
// ends and a part stops casting. Shadows are then extended: EditMainDraw casts them by the parts'
// distance from the eye rather than their depth, so that turning the camera does not move a part
// from one band to another, in a shade that fades from the near band's to the far band's and keeps
// it out to shadow_distance (forever at 0), and the fog fades them.
bool EdGroundShadowsExtended(float shadow_far);

// The shadows of the parts whose distance from the eye lies between near_distance and far_distance,
// cut off at video.shadow_distance: the bands of extended shadows. Returns how many parts it took,
// whether they drew a shadow, a shade or both.
int EdGroundDrawShadowBetween(CEditGround *ground, float near_distance, float far_distance);
