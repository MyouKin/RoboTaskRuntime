#include "tasks/spr_vision_26/spr_vision_fake_test_task.hpp"

#include <Eigen/Geometry>
#include <chrono>
#include <cmath>
#include <list>
#include <memory>
#include <sstream>
#include <string>

#include <opencv2/opencv.hpp>

#include "debug/param_tuner.hpp"
#include "io/command.hpp"
#include "rtr/core/runtime.hpp"
#include "spr_vision_image_bridge.hpp"
#include "tasks/auto_aim/aimer.hpp"
#include "tasks/auto_aim/armor.hpp"
#include "tasks/auto_aim/solver.hpp"
#include "tasks/auto_aim/target.hpp"
#include "tasks/auto_aim/tracker.hpp"
#include "tools/math_tools.hpp"

namespace tasks::spr_vision_26 {
namespace {

constexpr double kRadToDeg = 57.29577951308232;

cv::Rect boundingBox(const std::vector<cv::Point2f>& points) {
  std::vector<cv::Point> int_points;
  int_points.reserve(points.size());
  for (const auto& p : points) {
    int_points.emplace_back(static_cast<int>(std::round(p.x)),
                            static_cast<int>(std::round(p.y)));
  }
  return cv::boundingRect(int_points);
}

std::string commandString(const io::Command& command) {
  std::ostringstream ss;
  ss << "control=" << (command.control ? "true" : "false")
     << " shoot=" << (command.shoot ? "true" : "false")
     << " yaw=" << command.yaw * kRadToDeg
     << "deg pitch=" << command.pitch * kRadToDeg << "deg";
  return ss.str();
}

}  // namespace

struct SprVisionFakeTestTask::Impl {
  std::string config_path;
  std::unique_ptr<auto_aim::Solver> solver;
  std::unique_ptr<auto_aim::Tracker> tracker;
  std::unique_ptr<auto_aim::Aimer> aimer;
  int64_t frame_index = 0;
  std::chrono::steady_clock::time_point t0;
  io::Command last_command{false, false, 0.0, 0.0};

  auto_aim::Armor makeFakeArmor(double sim_time) {
    const double x = 3.2 + 0.25 * std::sin(sim_time * 0.7);
    const double y = 0.35 * std::sin(sim_time * 1.3);
    const double z = 0.08 * std::cos(sim_time * 0.9);
    const double yaw = tools::limit_rad(0.18 * std::sin(sim_time * 0.6));

    auto points = solver->reproject_armor({x, y, z}, yaw, auto_aim::ArmorType::small,
                                          auto_aim::ArmorName::three);

    auto_aim::Armor armor(
        0, 4, 0.95F, boundingBox(points), points);  // blue, number three, small armor
    armor.center_norm = {armor.center.x / 1440.0F, armor.center.y / 1080.0F};
    return armor;
  }

  cv::Mat drawOverlay(const std::list<auto_aim::Armor>& armors,
                      const std::list<auto_aim::Target>& targets,
                      const io::Command& command,
                      const std::string& tracker_state) {
    cv::Mat image(720, 960, CV_8UC3, cv::Scalar(8, 10, 14));
    const cv::Point center(image.cols / 2, image.rows / 2);
    cv::line(image, {center.x - 20, center.y}, {center.x + 20, center.y},
             {90, 90, 90}, 1);
    cv::line(image, {center.x, center.y - 20}, {center.x, center.y + 20},
             {90, 90, 90}, 1);

    for (const auto& armor : armors) {
      std::vector<cv::Point> poly;
      for (const auto& p : armor.points) {
        poly.emplace_back(static_cast<int>(std::round(p.x * image.cols / 1440.0)),
                          static_cast<int>(std::round(p.y * image.rows / 1080.0)));
      }
      if (poly.size() == 4) {
        cv::polylines(image, poly, true, {0, 220, 120}, 2);
        cv::circle(image, poly[0], 4, {0, 255, 255}, -1);
      }
    }

    if (!targets.empty() && aimer->debug_aim_point.valid) {
      const auto& target = targets.front();
      const Eigen::Vector4d xyza = aimer->debug_aim_point.xyza;
      const auto aim_points = solver->reproject_armor(
          xyza.head(3), xyza[3], target.armor_type, target.name);
      std::vector<cv::Point> aim_poly;
      for (const auto& p : aim_points) {
        aim_poly.emplace_back(static_cast<int>(std::round(p.x * image.cols / 1440.0)),
                              static_cast<int>(std::round(p.y * image.rows / 1080.0)));
      }
      if (aim_poly.size() == 4) {
        cv::polylines(image, aim_poly, true, {0, 0, 255}, 2);
      }
    }

    cv::putText(image, "SPR fake solve -> track -> aim",
                {18, 32}, cv::FONT_HERSHEY_SIMPLEX, 0.75, {230, 230, 230}, 2);
    cv::putText(image, "tracker: " + tracker_state,
                {18, 66}, cv::FONT_HERSHEY_SIMPLEX, 0.65, {130, 180, 255}, 2);
    cv::putText(image, commandString(command),
                {18, image.rows - 28}, cv::FONT_HERSHEY_SIMPLEX, 0.6,
                {0, 220, 220}, 2);
    return image;
  }
};

SprVisionFakeTestTask::SprVisionFakeTestTask() : impl_(std::make_unique<Impl>()) {}

SprVisionFakeTestTask::~SprVisionFakeTestTask() = default;

bool SprVisionFakeTestTask::onInit(rtr::Rtr& rtr) {
  const std::string default_config =
#ifdef RTR_SPR_VISION_26_DIR
      std::string(RTR_SPR_VISION_26_DIR) + "/vendor/configs/spr_fake_demo.yaml";
#else
      "src/tasks/spr_vision_26/vendor/configs/spr_fake_demo.yaml";
#endif

  rtr.param().declareString("spr.config_path", default_config,
                            "SPR Vision YAML config used by the original algorithms");
  rtr.param().declareDouble("spr.fake.bullet_speed", 27.0, 10.0, 40.0,
                            "Fake bullet speed passed to Aimer");
  rtr.param().declareBool("spr.fake.enable_overlay", true,
                          "Publish fake SPR overlay through rtr.debug");
  rtr.debug().text("spr.lifecycle", "Init");
  rtr.log().info("SprVisionFakeTestTask onInit");
  return true;
}

bool SprVisionFakeTestTask::onStart(rtr::Rtr& rtr) {
  impl_->config_path = rtr.param().getString("spr.config_path");
  try {
    debug::ParamTuner::instance().load_from_yaml(impl_->config_path);
    impl_->solver = std::make_unique<auto_aim::Solver>(impl_->config_path);
    impl_->tracker = std::make_unique<auto_aim::Tracker>(impl_->config_path, *impl_->solver);
    impl_->aimer = std::make_unique<auto_aim::Aimer>(impl_->config_path);
  } catch (const std::exception& e) {
    rtr.log().error(std::string("SprVisionFakeTestTask start failed: ") + e.what());
    return false;
  }

  impl_->frame_index = 0;
  impl_->t0 = std::chrono::steady_clock::now();
  impl_->last_command = {false, false, 0.0, 0.0};
  rtr.debug().text("spr.lifecycle", "Running");
  rtr.log().info("SprVisionFakeTestTask onStart");
  return true;
}

void SprVisionFakeTestTask::onUpdate(rtr::Rtr& rtr) {
  const double rate = rtr.param().getDouble("runtime.update_rate_hz", 30.0);
  const double dt = 1.0 / std::max(rate, 1.0);
  const double sim_time = static_cast<double>(impl_->frame_index) * dt;
  const auto timestamp =
      impl_->t0 + std::chrono::microseconds(static_cast<int64_t>(sim_time * 1e6));

  impl_->solver->set_R_gimbal2world(Eigen::Quaterniond::Identity());

  auto armor = impl_->makeFakeArmor(sim_time);
  auto solved_armor = armor;
  impl_->solver->solve(solved_armor);

  std::list<auto_aim::Armor> armors{armor};
  auto targets = impl_->tracker->track(armors, timestamp);

  const double bullet_speed = rtr.param().getDouble("spr.fake.bullet_speed", 27.0);
  auto command = impl_->aimer->aim(targets, timestamp, bullet_speed, false);
  impl_->last_command = command;

  rtr.debug().integer("spr.frame.index", impl_->frame_index);
  rtr.debug().text("spr.tracker.state", impl_->tracker->state());
  rtr.debug().integer("spr.armor.count", static_cast<int64_t>(armors.size()));
  rtr.debug().boolean("spr.command.control", command.control);
  rtr.debug().boolean("spr.command.shoot", command.shoot);
  rtr.debug().scalar("spr.command.yaw_deg", command.yaw * kRadToDeg);
  rtr.debug().scalar("spr.command.pitch_deg", command.pitch * kRadToDeg);
  rtr.debug().scalar("spr.solved.x", solved_armor.xyz_in_world.x());
  rtr.debug().scalar("spr.solved.y", solved_armor.xyz_in_world.y());
  rtr.debug().scalar("spr.solved.z", solved_armor.xyz_in_world.z());

  if (!targets.empty()) {
    const auto x = targets.front().ekf_x();
    if (x.size() >= 8) {
      rtr.debug().scalar("spr.target.x", x[0]);
      rtr.debug().scalar("spr.target.y", x[2]);
      rtr.debug().scalar("spr.target.z", x[4]);
      rtr.debug().scalar("spr.target.w", x[7]);
    }
  }

  if (rtr.param().getBool("spr.fake.enable_overlay", true) &&
      rtr.debug().enabled("spr.fake.overlay")) {
    rtr.debug().image("spr.fake.overlay",
                      toImageFrame(impl_->drawOverlay(armors, targets, command,
                                                      impl_->tracker->state()),
                                   "spr_fake"));
  }

  ++impl_->frame_index;
}

void SprVisionFakeTestTask::onPause(rtr::Rtr& rtr) {
  rtr.debug().text("spr.lifecycle", "Paused");
  rtr.log().info("SprVisionFakeTestTask onPause");
}

void SprVisionFakeTestTask::onResume(rtr::Rtr& rtr) {
  rtr.debug().text("spr.lifecycle", "Running");
  rtr.log().info("SprVisionFakeTestTask onResume");
}

void SprVisionFakeTestTask::onStop(rtr::Rtr& rtr) {
  impl_->aimer.reset();
  impl_->tracker.reset();
  impl_->solver.reset();
  rtr.debug().text("spr.lifecycle", "Stopped");
  rtr.log().info("SprVisionFakeTestTask onStop");
}

}  // namespace tasks::spr_vision_26
