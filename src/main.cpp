#include <exception>
#include <iostream>
#include <memory>
#include <string>

#include "rtr/core/runtime.hpp"
#include "tasks/minimal_task/minimal_task.hpp"
#ifdef RTR_HAS_SPR_VISION_26_FAKE_TEST
#include "tasks/spr_vision_26/spr_vision_fake_test_task.hpp"
#endif
#if RTR_ENABLE_SPR_VISION_26_REAL
#include "tasks/spr_vision_26/spr_vision_task.hpp"
#endif

namespace {

std::unique_ptr<rtr::Task> createTask(const std::string& name) {
  if (name == "minimal") {
    return std::make_unique<rtr::tasks::MinimalTask>();
  }
#ifdef RTR_HAS_SPR_VISION_26_FAKE_TEST
  if (name == "spr_vision_fake_test") {
    return std::make_unique<tasks::spr_vision_26::SprVisionFakeTestTask>();
  }
#endif
#if RTR_ENABLE_SPR_VISION_26_REAL
  if (name == "spr_vision_task") {
    return std::make_unique<tasks::spr_vision_26::SprVisionTask>();
  }
#endif
  return nullptr;
}

std::string defaultConfigForTask(const std::string& name) {
  if (name == "spr_vision_fake_test") {
    return "configs/spr_vision_26_fake_test.yaml";
  }
  if (name == "spr_vision_task") {
    return "configs/spr_vision_26_standard4.yaml";
  }
  return "configs/minimal_task.yaml";
}

}  // namespace

int main(int argc, char** argv) {
  try {
    rtr::RuntimeOptions options = rtr::parseRuntimeOptions(argc, argv);
    if (options.config_path.empty()) {
      options.config_path = defaultConfigForTask(options.task_name);
    }

    std::unique_ptr<rtr::Task> task = createTask(options.task_name);
    if (!task) {
      std::cerr << "Unknown task: " << options.task_name << "\n";
      rtr::printUsage();
      return 2;
    }

    rtr::Runtime runtime;
    return runtime.run(std::move(task), options);
  } catch (const std::exception& e) {
    std::cerr << "robotaskrun error: " << e.what() << "\n";
    rtr::printUsage();
    return 2;
  }
}
