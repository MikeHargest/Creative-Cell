#include "camera.h"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace {
constexpr float kOrbitSpeed = 0.006f;
constexpr float kPanSpeed = 0.0015f;
constexpr float kDollyOutScale = 0.45f;
// No upper bound on distance — models can be arbitrarily large. Floor is
// just large enough to keep dolly from crossing zero and flipping the view
// through the target.
constexpr float kMinDistance = 0.01f;
// Straight up / down is allowed (exact top and bottom views, which an
// orthographic view needs — any less and a side face shows). The camera's
// up comes from its own orientation, so nothing flips at the poles.
constexpr float kPitchLimit = 1.5707963f;

glm::vec3 orbitDirection(float yaw, float pitch) {
  return glm::vec3(std::cos(pitch) * std::sin(yaw), std::sin(pitch), std::cos(pitch) * std::cos(yaw));
}
}  // namespace

void Camera::orbit(float dxPixels, float dyPixels) {
  animating_ = false;
  if (free_) {
    // About the view's own up and right axes: same directions as the
    // turntable, but nothing stops at the poles.
    glm::vec3 upAxis = q_ * glm::vec3(0, 1, 0), right = q_ * glm::vec3(1, 0, 0);
    q_ = glm::normalize(glm::angleAxis(-dxPixels * kOrbitSpeed, upAxis) *
                        glm::angleAxis(-dyPixels * kOrbitSpeed, right) * q_);
    return;
  }
  yaw_ -= dxPixels * kOrbitSpeed;
  pitch_ = std::clamp(pitch_ + dyPixels * kOrbitSpeed, -kPitchLimit, kPitchLimit);
}

void Camera::pan(float dxPixels, float dyPixels) {
  if (free_) {
    target_ += (-dxPixels * (q_ * glm::vec3(1, 0, 0)) + dyPixels * (q_ * glm::vec3(0, 1, 0))) * kPanSpeed * distance_;
    return;
  }
  const glm::quat q = turntable();
  const glm::vec3 right = q * glm::vec3(1, 0, 0), up = q * glm::vec3(0, 1, 0);
  target_ += (-dxPixels * right + dyPixels * up) * kPanSpeed * distance_;
}

void Camera::dolly(float delta) {
  // Moving away is damped a little compared to moving in.
  if (delta > 0.0f) delta *= kDollyOutScale;
  distance_ = std::max(distance_ * std::exp(delta), kMinDistance);
}

void Camera::refocus(float depth) {
  const glm::vec3 eye = position();
  const glm::vec3 forward = glm::normalize(target_ - eye);
  distance_ = std::max(depth, kMinDistance);
  target_ = eye + forward * distance_;
}

void Camera::frame(const glm::vec3& center, float radius) {
  target_ = center;
  distance_ = std::max(radius * 2.2f, kMinDistance);
}

void Camera::lookAlongAxis(const glm::vec3& axis, bool animate) {
  glm::vec3 dir = glm::normalize(axis);
  if (free_) {
    // Level where it can be: world up, or, for views from straight above /
    // below, the current up squared to the nearest world axis (±X / ±Z) —
    // otherwise the top view keeps whatever roll the orbit came in with and
    // the model sits at an angle, as the turntable branch already avoids.
    glm::vec3 upAxis = glm::vec3(0, 1, 0);
    if (std::abs(dir.y) >= 0.999f) {
      glm::vec3 current = q_ * glm::vec3(0, 1, 0);
      current -= dir * glm::dot(current, dir);
      if (glm::length(current) < 1e-4f) current = glm::vec3(0, 0, -1);
      upAxis = std::abs(current.x) > std::abs(current.z) ? glm::vec3(current.x > 0.0f ? 1.0f : -1.0f, 0, 0)
                                                         : glm::vec3(0, 0, current.z > 0.0f ? 1.0f : -1.0f);
    }
    upAxis = glm::normalize(upAxis - dir * glm::dot(upAxis, dir));
    glm::vec3 right = glm::cross(upAxis, dir);
    goalQ_ = glm::normalize(glm::quat_cast(glm::mat3(right, upAxis, dir)));
    if (glm::dot(goalQ_, q_) < 0.0f) goalQ_ = -goalQ_;  // the short way round
    animating_ = animate;
    if (!animate) q_ = goalQ_;
    return;
  }
  goalPitch_ = std::clamp(std::asin(std::clamp(dir.y, -1.0f, 1.0f)), -kPitchLimit, kPitchLimit);
  // Straight up/down axes leave yaw undefined (horizLen ~ 0) — keep the
  // current heading, squared to the nearest axis so the top view lines up
  // with the grid.
  float horizLen = std::sqrt(dir.x * dir.x + dir.z * dir.z);
  const float quarter = glm::half_pi<float>();
  goalYaw_ = horizLen > 1e-5f ? std::atan2(dir.x, dir.z) : std::round(yaw_ / quarter) * quarter;
  // Turn the short way round.
  const float twoPi = glm::two_pi<float>();
  goalYaw_ = yaw_ + std::remainder(goalYaw_ - yaw_, twoPi);
  animating_ = animate;
  if (!animate) {
    yaw_ = goalYaw_;
    pitch_ = goalPitch_;
  }
}

void Camera::resetToIsometricView() {
  float yaw = glm::radians(45.0f), pitch = glm::radians(35.264f);
  lookAlongAxis(glm::vec3(std::cos(pitch) * std::sin(yaw), std::sin(pitch), std::cos(pitch) * std::cos(yaw)));
}

void Camera::update(float dt) {
  if (!animating_) return;
  // Exponential ease: fast start, soft landing, frame-rate independent.
  float k = 1.0f - std::exp(-dt * 14.0f);
  if (free_) {
    q_ = glm::normalize(glm::slerp(q_, goalQ_, k));
    if (std::abs(glm::dot(q_, goalQ_)) > 1.0f - 1e-7f) {
      q_ = goalQ_;
      animating_ = false;
    }
    return;
  }
  yaw_ += (goalYaw_ - yaw_) * k;
  pitch_ += (goalPitch_ - pitch_) * k;
  if (std::abs(goalYaw_ - yaw_) < 1e-4f && std::abs(goalPitch_ - pitch_) < 1e-4f) {
    yaw_ = goalYaw_;
    pitch_ = goalPitch_;
    animating_ = false;
  }
}

glm::quat Camera::turntable() const {
  return glm::angleAxis(yaw_, glm::vec3(0, 1, 0)) * glm::angleAxis(-pitch_, glm::vec3(1, 0, 0));
}

// Turntable: the view's own up (the same as world up for lookAt anywhere
// but at the poles, where world up is along the view and lookAt breaks).
glm::vec3 Camera::up() const { return (free_ ? q_ : turntable()) * glm::vec3(0, 1, 0); }

void Camera::setFreeOrbit(bool free) {
  if (free == free_) return;
  animating_ = false;
  if (free) {
    q_ = goalQ_ = turntable();
  } else {
    // Back to turntable: same direction, level again.
    glm::vec3 d = q_ * glm::vec3(0, 0, 1);
    pitch_ = std::clamp(std::asin(std::clamp(d.y, -1.0f, 1.0f)), -kPitchLimit, kPitchLimit);
    if (d.x * d.x + d.z * d.z > 1e-8f) yaw_ = std::atan2(d.x, d.z);
  }
  free_ = free;
}

glm::vec3 Camera::position() const {
  glm::vec3 d = free_ ? q_ * glm::vec3(0, 0, 1) : orbitDirection(yaw_, pitch_);
  return target_ + distance_ * d;
}

glm::mat4 Camera::viewMatrix() const {
  return glm::lookAt(position(), target_, up());
}
