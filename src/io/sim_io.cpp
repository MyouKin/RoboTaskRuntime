#include "rtr/io/sim_camera_source.hpp"

#include <algorithm>
#include <cmath>

#include "rtr/core/runtime.hpp"

namespace rtr {

bool SimCameraSource::configure(Rtr& rtr, const std::string& prefix) {
  rtr.param().declareInt(prefix + ".width", width_, 160, 3840,
                         "Sim camera image width");
  rtr.param().declareInt(prefix + ".height", height_, 120, 2160,
                         "Sim camera image height");
  width_ = static_cast<int>(rtr.param().getInt(prefix + ".width", width_));
  height_ = static_cast<int>(rtr.param().getInt(prefix + ".height", height_));
  rtr.log().info("SimCameraSource configured");
  return true;
}

bool SimCameraSource::open() {
  opened_ = true;
  return true;
}

bool SimCameraSource::start() {
  if (!opened_) {
    return false;
  }
  started_ = true;
  return true;
}

void SimCameraSource::stop() {
  started_ = false;
}

void SimCameraSource::close() {
  opened_ = false;
  started_ = false;
}

void SimCameraSource::setMotion(double speed_px_per_sec, double noise_px,
                                double loss_period_sec) {
  speed_px_per_sec_ = std::max(0.0, speed_px_per_sec);
  noise_px_ = std::max(0.0, noise_px);
  loss_period_sec_ = std::max(0.1, loss_period_sec);
}

bool SimCameraSource::next(SimFrame& frame, double dt_sec) {
  if (!started_) {
    return false;
  }
  dt_sec = std::max(dt_sec, 1e-4);
  time_sec_ += dt_sec;

  std::normal_distribution<double> noise(0.0, noise_px_);
  const double radius_x = width_ * 0.32;
  const double radius_y = height_ * 0.25;
  const double phase = time_sec_ * speed_px_per_sec_ * 0.018;
  const double period_pos = std::fmod(time_sec_, loss_period_sec_);
  const bool valid = period_pos < loss_period_sec_ * 0.78;

  frame.header.stamp = TimeStamp::now();
  frame.header.frame_id = "sim_camera";
  frame.header.sequence = sequence_++;
  frame.width = width_;
  frame.height = height_;
  frame.target.valid = valid;
  frame.target.x =
      std::clamp(width_ * 0.5 + std::cos(phase) * radius_x + noise(rng_), 0.0,
                 static_cast<double>(width_ - 1));
  frame.target.y =
      std::clamp(height_ * 0.5 + std::sin(phase * 0.73) * radius_y +
                     noise(rng_),
                 0.0, static_cast<double>(height_ - 1));
  return true;
}

}  // namespace rtr

