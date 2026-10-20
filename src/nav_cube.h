#pragma once

#include <imgui.h>

#include <glm/glm.hpp>

class Camera;

// Navigation cube (as Expanse's, which is Maya's ViewCube): each face is
// split 3x3 — a click on the middle looks along that face, on an edge band
// along that edge, on a corner along the diagonal. Dragging it orbits.
// Drawn into the current window at `topLeft`, `size` px square. Returns
// true while the mouse is on it (the view below shouldn't take the click).
bool navCube(const char* id, ImVec2 topLeft, float size, const glm::mat4& view, Camera& camera);
