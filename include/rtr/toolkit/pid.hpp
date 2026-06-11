#pragma once

#include <algorithm>
#include <cmath>

namespace rtr {

class Pid {
 public:
  void setGains(double kp, double ki, double kd) {
    kp_ = kp;
    ki_ = ki;
    kd_ = kd;
  }

  void setOutputLimit(double limit) { output_limit_ = std::abs(limit); }

  void reset() {
    integral_ = 0.0;
    previous_error_ = 0.0;
    has_previous_ = false;
  }

  double update(double error, double dt_sec) {
    if (dt_sec <= 1e-6) {
      dt_sec = 1e-6;
    }
    integral_ += error * dt_sec;
    const double derivative =
        has_previous_ ? (error - previous_error_) / dt_sec : 0.0;
    previous_error_ = error;
    has_previous_ = true;

    double output = kp_ * error + ki_ * integral_ + kd_ * derivative;
    if (output_limit_ > 0.0) {
      output = std::clamp(output, -output_limit_, output_limit_);
    }
    return output;
  }

 private:
  double kp_ = 0.0;
  double ki_ = 0.0;
  double kd_ = 0.0;
  double output_limit_ = 0.0;
  double integral_ = 0.0;
  double previous_error_ = 0.0;
  bool has_previous_ = false;
};

}  // namespace rtr

