#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

// Maya-style orbit camera: Alt+LMB orbit, Alt+MMB pan, Alt+RMB drag or wheel
// to dolly. Caller is responsible for gating input on ImGui::WantCaptureMouse
// and on the Alt modifier before forwarding deltas here.
class Camera {
 public:
  void orbit(float dxPixels, float dyPixels);
  void pan(float dxPixels, float dyPixels);
  void dolly(float delta);

  // Auto depth (Blender's): the target moved along the view to `depth`
  // from the eye — the view stays as it is, but orbit turns about that
  // depth and zoom / pan go at its pace.
  void refocus(float depth);

  // Re-centers on `center` and picks a distance that fits a sphere of
  // `radius` in view. Call after loading a new mesh.
  void frame(const glm::vec3& center, float radius);

  // Turns the camera so it sits in direction `axis` from the target and
  // looks back at it, at the current target/distance (orbits in place,
  // doesn't reframe) — the click-to-snap of the navigation cube. `axis`
  // need not be normalized. Animated unless `animate` is false; any orbit
  // cancels the animation.
  void lookAlongAxis(const glm::vec3& axis, bool animate = true);

  // Same in-place orbit, to a fixed 3/4 angle (yaw 45deg, pitch ~35.264deg,
  // the standard "isometric" angle) — the navigation cube's home button.
  void resetToIsometricView();

  // Advances the snap animation; call once per frame.
  void update(float dt);

  glm::mat4 viewMatrix() const;
  glm::vec3 position() const;
  glm::vec3 target() const { return target_; }

  // Free orbit: the view turns over the top and rolls, like a trackball,
  // instead of keeping the floor level (turntable, the default). Turning
  // it off levels the view again.
  void setFreeOrbit(bool free);
  bool freeOrbit() const { return free_; }
  bool animating() const { return animating_; }

 private:
  glm::quat turntable() const;  // yaw / pitch as a rotation
  glm::vec3 up() const;

  glm::vec3 target_{0.0f, 0.0f, 0.0f};
  float yaw_ = -0.6f;    // radians
  float pitch_ = 0.4f;   // radians
  float distance_ = 3.0f;

  bool animating_ = false;
  float goalYaw_ = 0.0f, goalPitch_ = 0.0f;

  // Free orbit: camera -> world rotation; the camera sits along its +Z
  // from the target, with its +Y up.
  bool free_ = false;
  glm::quat q_{1.0f, 0.0f, 0.0f, 0.0f};
  glm::quat goalQ_{1.0f, 0.0f, 0.0f, 0.0f};
};
