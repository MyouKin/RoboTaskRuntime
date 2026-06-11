#include "tasks/spr_vision_26/spr_vision_task.hpp"

#include <atomic>
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <thread>

#include "rtr/core/runtime.hpp"

#if RTR_ENABLE_SPR_VISION_26_REAL

#include <Eigen/Geometry>
#include <opencv2/opencv.hpp>

#include "io/camera.hpp"
#include "io/gimbal/gimbal.hpp"
#include "spr_vision_image_bridge.hpp"
#include "tasks/auto_aim/planner/planner.hpp"
#include "tasks/auto_aim/solver.hpp"
#include "tasks/auto_aim/target.hpp"
#include "tasks/auto_aim/tracker.hpp"
#include "tasks/auto_aim/yolo.hpp"
#include "tasks/auto_buff/buff_aimer.hpp"
#include "tasks/auto_buff/buff_detector.hpp"
#include "tasks/auto_buff/buff_solver.hpp"
#include "tasks/auto_buff/buff_target.hpp"
#include "tasks/auto_buff/buff_type.hpp"
#include "tools/logger.hpp"
#include "tools/recorder.hpp"
#include "tools/thread_safe_queue.hpp"

#endif

namespace tasks::spr_vision_26 {

struct SprVisionTask::Impl {
  std::string config_path;

#if RTR_ENABLE_SPR_VISION_26_REAL
  std::unique_ptr<io::Gimbal> gimbal;
  std::unique_ptr<io::Camera> camera;
  std::unique_ptr<auto_aim::YOLO> yolo;
  std::unique_ptr<auto_aim::Solver> solver;
  std::unique_ptr<auto_aim::Tracker> tracker;
  std::unique_ptr<auto_aim::Planner> planner;

  std::unique_ptr<auto_buff::Buff_Detector> buff_detector;
  std::unique_ptr<auto_buff::Solver> buff_solver;
  auto_buff::SmallTarget buff_small_target;
  auto_buff::BigTarget buff_big_target;
  std::unique_ptr<auto_buff::Aimer> buff_aimer;

  tools::ThreadSafeQueue<std::optional<auto_aim::Target>, true> target_queue{1};
  std::thread plan_thread;
  std::atomic<bool> quit{false};
  std::atomic<bool> paused{false};
  std::atomic<io::GimbalMode> mode{io::GimbalMode::IDLE};
  io::GimbalMode last_mode{io::GimbalMode::IDLE};
  int64_t frame_index = 0;

  void startPlanThread(rtr::Rtr& rtr) {
    plan_thread = std::thread([this, &rtr]() {
      while (!quit) {
        if (paused) {
          std::this_thread::sleep_for(std::chrono::milliseconds(20));
          continue;
        }

        if (!target_queue.empty() && mode == io::GimbalMode::AUTO_AIM) {
          auto target = target_queue.front();
          auto gs = gimbal->state();
          auto plan = planner->plan(target, gs.bullet_speed);

          gimbal->send(
            plan.control, plan.fire, plan.yaw, plan.yaw_vel, plan.yaw_acc, plan.pitch,
            plan.pitch_vel, plan.pitch_acc);

          rtr.debug().boolean("spr.plan.control", plan.control);
          rtr.debug().boolean("spr.plan.fire", plan.fire);
          rtr.debug().scalar("spr.plan.yaw", plan.yaw);
          rtr.debug().scalar("spr.plan.pitch", plan.pitch);

          std::this_thread::sleep_for(std::chrono::milliseconds(10));
        } else {
          std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
      }
    });
  }

  void stopPlanThread() {
    quit = true;
    if (plan_thread.joinable()) {
      plan_thread.join();
    }
  }
#endif
};

SprVisionTask::SprVisionTask() : impl_(std::make_unique<Impl>()) {}

SprVisionTask::~SprVisionTask() {
#if RTR_ENABLE_SPR_VISION_26_REAL
  if (impl_) {
    impl_->stopPlanThread();
  }
#endif
}

bool SprVisionTask::onInit(rtr::Rtr& rtr) {
  const std::string default_config =
#ifdef RTR_SPR_VISION_26_DIR
      std::string(RTR_SPR_VISION_26_DIR) + "/vendor/configs/spr_standard4.yaml";
#else
      "src/task/spr_vision_26/vendor/configs/spr_standard4.yaml";
#endif

  rtr.param().declareString("spr.config_path", default_config,
                            "SPR Vision standard_mpc YAML config");
  rtr.param().declareBool("spr.debug.enable_overlay", true,
                          "Publish latest SPR camera frame through rtr.debug");
  rtr.debug().text("spr.lifecycle", "Init");
  rtr.log().info("SprVisionTask onInit");
  return true;
}

bool SprVisionTask::onStart(rtr::Rtr& rtr) {
#if RTR_ENABLE_SPR_VISION_26_REAL
  impl_->config_path = rtr.param().getString("spr.config_path");
  try {
    impl_->gimbal = std::make_unique<io::Gimbal>(impl_->config_path);
    impl_->camera = std::make_unique<io::Camera>(impl_->config_path);

    impl_->yolo = std::make_unique<auto_aim::YOLO>(impl_->config_path, true);
    impl_->solver = std::make_unique<auto_aim::Solver>(impl_->config_path);
    impl_->tracker = std::make_unique<auto_aim::Tracker>(impl_->config_path, *impl_->solver);
    impl_->planner = std::make_unique<auto_aim::Planner>(impl_->config_path);

    impl_->target_queue.push(std::nullopt);

    impl_->buff_detector = std::make_unique<auto_buff::Buff_Detector>(impl_->config_path);
    impl_->buff_solver = std::make_unique<auto_buff::Solver>(impl_->config_path);
    impl_->buff_aimer = std::make_unique<auto_buff::Aimer>(impl_->config_path);

    impl_->quit = false;
    impl_->paused = false;
    impl_->mode = io::GimbalMode::IDLE;
    impl_->last_mode = io::GimbalMode::IDLE;
    impl_->frame_index = 0;
    impl_->startPlanThread(rtr);
  } catch (const std::exception& e) {
    rtr.log().error(std::string("SprVisionTask start failed: ") + e.what());
    return false;
  }

  rtr.debug().text("spr.lifecycle", "Running");
  rtr.log().info("SprVisionTask onStart");
  return true;
#else
  (void)rtr;
  return false;
#endif
}

void SprVisionTask::onUpdate(rtr::Rtr& rtr) {
#if RTR_ENABLE_SPR_VISION_26_REAL
  if (impl_->paused) {
    return;
  }

  impl_->mode = impl_->gimbal->mode();

  if (impl_->last_mode != impl_->mode) {
    rtr.log().info("SPR mode switch to " + impl_->gimbal->str(impl_->mode));
    impl_->last_mode = impl_->mode.load();
  }

  cv::Mat img;
  std::chrono::steady_clock::time_point t;
  impl_->camera->read(img, t);
  auto q = impl_->gimbal->q(t);
  auto gs = impl_->gimbal->state();
  impl_->solver->set_R_gimbal2world(q);

  if (impl_->mode.load() == io::GimbalMode::AUTO_AIM) {
    auto armors = impl_->yolo->detect(img);
    auto targets = impl_->tracker->track(armors, t);
    if (!targets.empty()) {
      impl_->target_queue.push(targets.front());
    } else {
      impl_->target_queue.push(std::nullopt);
    }

    rtr.debug().integer("spr.armor.count", static_cast<int64_t>(armors.size()));
    rtr.debug().text("spr.tracker.state", impl_->tracker->state());
    if (!targets.empty()) {
      const auto x = targets.front().ekf_x();
      if (x.size() >= 8) {
        rtr.debug().scalar("spr.target.x", x[0]);
        rtr.debug().scalar("spr.target.y", x[2]);
        rtr.debug().scalar("spr.target.z", x[4]);
        rtr.debug().scalar("spr.target.w", x[7]);
      }
    }
  }

  else if (
    impl_->mode.load() == io::GimbalMode::SMALL_BUFF ||
    impl_->mode.load() == io::GimbalMode::BIG_BUFF) {
    impl_->buff_solver->set_R_gimbal2world(q);

    auto power_runes = impl_->buff_detector->detect(img);

    impl_->buff_solver->solve(power_runes);

    auto_aim::Plan buff_plan;
    if (impl_->mode.load() == io::GimbalMode::SMALL_BUFF) {
      impl_->buff_small_target.get_target(power_runes, t);
      auto target_copy = impl_->buff_small_target;
      buff_plan = impl_->buff_aimer->mpc_aim(target_copy, t, gs, true);
    } else if (impl_->mode.load() == io::GimbalMode::BIG_BUFF) {
      impl_->buff_big_target.get_target(power_runes, t);
      auto target_copy = impl_->buff_big_target;
      buff_plan = impl_->buff_aimer->mpc_aim(target_copy, t, gs, true);
    }
    impl_->gimbal->send(
      buff_plan.control, buff_plan.fire, buff_plan.yaw, buff_plan.yaw_vel,
      buff_plan.yaw_acc, buff_plan.pitch, buff_plan.pitch_vel, buff_plan.pitch_acc);
  } else {
    impl_->gimbal->send(false, false, 0, 0, 0, 0, 0, 0);
  }

  rtr.debug().integer("spr.frame.index", impl_->frame_index);
  rtr.debug().text("spr.gimbal.mode", impl_->gimbal->str(impl_->mode));
  rtr.debug().scalar("spr.gimbal.bullet_speed", gs.bullet_speed);
  rtr.debug().scalar("spr.gimbal.yaw", gs.yaw);
  rtr.debug().scalar("spr.gimbal.pitch", gs.pitch);

  if (rtr.param().getBool("spr.debug.enable_overlay", true) &&
      rtr.debug().enabled("spr.overlay") && !img.empty()) {
    rtr.debug().image("spr.overlay", toImageFrame(img, "spr_camera"));
  }

  ++impl_->frame_index;
#else
  (void)rtr;
#endif
}

void SprVisionTask::onPause(rtr::Rtr& rtr) {
#if RTR_ENABLE_SPR_VISION_26_REAL
  impl_->paused = true;
#endif
  rtr.debug().text("spr.lifecycle", "Paused");
  rtr.log().info("SprVisionTask onPause");
}

void SprVisionTask::onResume(rtr::Rtr& rtr) {
#if RTR_ENABLE_SPR_VISION_26_REAL
  impl_->paused = false;
#endif
  rtr.debug().text("spr.lifecycle", "Running");
  rtr.log().info("SprVisionTask onResume");
}

void SprVisionTask::onStop(rtr::Rtr& rtr) {
#if RTR_ENABLE_SPR_VISION_26_REAL
  impl_->stopPlanThread();
  if (impl_->gimbal) {
    impl_->gimbal->send(false, false, 0, 0, 0, 0, 0, 0);
  }
  impl_->buff_aimer.reset();
  impl_->buff_detector.reset();
  impl_->buff_solver.reset();
  impl_->planner.reset();
  impl_->tracker.reset();
  impl_->solver.reset();
  impl_->yolo.reset();
  impl_->camera.reset();
  impl_->gimbal.reset();
#endif
  rtr.debug().text("spr.lifecycle", "Stopped");
  rtr.log().info("SprVisionTask onStop");
}

}  // namespace tasks::spr_vision_26
