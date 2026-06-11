#pragma once

#include <cstdint>

#include "rtr/core/task.hpp"

namespace rtr::tasks {

class MinimalTask : public Task {
 public:
  bool onInit(Rtr& rtr) override;
  bool onStart(Rtr& rtr) override;
  void onUpdate(Rtr& rtr) override;
  void onPause(Rtr& rtr) override;
  void onResume(Rtr& rtr) override;
  void onStop(Rtr& rtr) override;

 private:
  int64_t update_count_ = 0;
  double phase_ = 0.0;
};

}  // namespace rtr::tasks

