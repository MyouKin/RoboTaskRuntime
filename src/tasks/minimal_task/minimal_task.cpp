#include "tasks/minimal_task/minimal_task.hpp"

#include <cmath>
#include <sstream>

#include "rtr/core/runtime.hpp"

namespace rtr::tasks {

bool MinimalTask::onInit(Rtr& rtr) {
  rtr.param().declareString("minimal.message", "hello runtime",
                            "Message exposed by MinimalTask");
  rtr.param().declareDouble("minimal.amplitude", 1.0, 0.0, 10.0,
                            "Amplitude for the minimal scalar signal");
  rtr.debug().text("minimal.lifecycle", "Init");
  rtr.log().info("MinimalTask onInit");
  return true;
}

bool MinimalTask::onStart(Rtr& rtr) {
  update_count_ = 0;
  phase_ = 0.0;
  rtr.debug().text("minimal.lifecycle", "Running");
  rtr.log().info("MinimalTask onStart");
  return true;
}

void MinimalTask::onUpdate(Rtr& rtr) {
  const double amplitude = rtr.param().getDouble("minimal.amplitude", 1.0);
  phase_ += 0.04;
  ++update_count_;

  rtr.debug().integer("minimal.update_count", update_count_);
  rtr.debug().scalar("minimal.signal", std::sin(phase_) * amplitude);
  rtr.debug().text("minimal.message",
                   rtr.param().getString("minimal.message", ""));

  if (update_count_ % 100 == 0) {
    std::ostringstream ss;
    ss << "MinimalTask heartbeat update=" << update_count_;
    rtr.log().info(ss.str());
  }
}

void MinimalTask::onPause(Rtr& rtr) {
  rtr.debug().text("minimal.lifecycle", "Paused");
  rtr.log().info("MinimalTask onPause");
}

void MinimalTask::onResume(Rtr& rtr) {
  rtr.debug().text("minimal.lifecycle", "Running");
  rtr.log().info("MinimalTask onResume");
}

void MinimalTask::onStop(Rtr& rtr) {
  rtr.debug().text("minimal.lifecycle", "Stopped");
  rtr.log().info("MinimalTask onStop");
}

}  // namespace rtr::tasks
