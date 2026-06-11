#pragma once

#include <cstdint>
#include <random>
#include <string>

#include "rtr/core/data.hpp"
#include "rtr/io/data_source.hpp"

namespace rtr {

struct SimTarget {
  bool valid = false;
  double x = 0.0;
  double y = 0.0;
};

struct SimFrame {
  Header header;
  int width = 640;
  int height = 480;
  SimTarget target;
};

class SimCameraSource : public DataSource {
 public:
  bool configure(Rtr& rtr, const std::string& prefix) override;
  bool open() override;
  bool start() override;
  void stop() override;
  void close() override;

  void setMotion(double speed_px_per_sec, double noise_px,
                 double loss_period_sec);
  bool next(SimFrame& frame, double dt_sec);

 private:
  int width_ = 640;
  int height_ = 480;
  bool opened_ = false;
  bool started_ = false;
  uint64_t sequence_ = 0;
  double time_sec_ = 0.0;
  double speed_px_per_sec_ = 80.0;
  double noise_px_ = 2.0;
  double loss_period_sec_ = 5.0;
  std::mt19937 rng_{7};
};

}  // namespace rtr

