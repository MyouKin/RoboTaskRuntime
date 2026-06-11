#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <variant>
#include <vector>

#include "rtr/core/data.hpp"

namespace rtr {

enum class DebugValueType {
  Scalar,
  Int,
  Bool,
  Text,
};

using DebugVariant = std::variant<double, int64_t, bool, std::string>;

struct HistoryPoint {
  int64_t timestamp_ms = 0;
  double value = 0.0;
};

struct DebugValueEntry {
  std::string key;
  DebugValueType type = DebugValueType::Text;
  DebugVariant value;
  int64_t timestamp_ms = 0;
};

struct DebugImageEntry {
  std::string key;
  ImageFrame image;
  int64_t timestamp_ms = 0;
};

struct DebugList {
  std::vector<std::string> images;
  std::vector<std::string> scalars;
  std::vector<std::string> integers;
  std::vector<std::string> booleans;
  std::vector<std::string> texts;
  std::vector<DebugValueEntry> values;
};

class DebugService {
 public:
  explicit DebugService(size_t history_limit = 300);

  void setGlobalEnabled(bool enabled);
  void setEnabled(const std::string& key, bool enabled);
  bool enabled(const std::string& key) const;

  void setImageRateLimitHz(double hz);
  bool image(const std::string& key, const ImageFrame& image);

#ifdef RTR_USE_OPENCV
  bool image(const std::string& key, const cv::Mat& image);
#endif

  void scalar(const std::string& key, double value);
  void integer(const std::string& key, int64_t value);
  void boolean(const std::string& key, bool value);
  void text(const std::string& key, const std::string& value);

  std::optional<DebugImageEntry> latestImage(const std::string& key) const;
  DebugList list() const;
  std::vector<DebugValueEntry> values() const;
  std::map<std::string, std::vector<HistoryPoint>> histories() const;

 private:
  void rememberHistoryLocked(const std::string& key, double value,
                             int64_t timestamp_ms);

  std::atomic<bool> global_enabled_{true};
  double image_min_interval_ms_ = 66.0;
  size_t history_limit_ = 300;
  mutable std::mutex mutex_;
  std::set<std::string> disabled_keys_;
  std::map<std::string, DebugImageEntry> images_;
  std::map<std::string, int64_t> last_image_ms_;
  std::map<std::string, DebugValueEntry> values_;
  std::map<std::string, std::deque<HistoryPoint>> histories_;
};

}  // namespace rtr
