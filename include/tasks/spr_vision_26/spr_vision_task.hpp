#pragma once

#include <memory>

#include "rtr/core/task.hpp"

namespace tasks::spr_vision_26 {

class SprVisionTask : public rtr::Task {
 public:
  SprVisionTask();
  ~SprVisionTask() override;

  bool onInit(rtr::Rtr& rtr) override;
  bool onStart(rtr::Rtr& rtr) override;
  void onUpdate(rtr::Rtr& rtr) override;
  void onPause(rtr::Rtr& rtr) override;
  void onResume(rtr::Rtr& rtr) override;
  void onStop(rtr::Rtr& rtr) override;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace tasks::spr_vision_26

