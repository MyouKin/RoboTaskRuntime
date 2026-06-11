#include "rtr/core/runtime.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cctype>
#include <cstdlib>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <thread>

#include "rtr/core/webui_server.hpp"

#ifdef RTR_HAS_SPDLOG
#include <spdlog/spdlog.h>
#endif

#ifdef RTR_HAS_YAML_CPP
#include <yaml-cpp/yaml.h>
#endif

#ifdef RTR_USE_OPENCV
#include <opencv2/imgproc.hpp>
#endif

namespace {

std::atomic<bool> g_signal_shutdown{false};

void handleSignal(int) {
  g_signal_shutdown = true;
}

std::string trim(const std::string& input) {
  const auto begin = std::find_if_not(input.begin(), input.end(), [](int ch) {
    return std::isspace(ch) != 0;
  });
  const auto end = std::find_if_not(input.rbegin(), input.rend(), [](int ch) {
                     return std::isspace(ch) != 0;
                   }).base();
  if (begin >= end) {
    return "";
  }
  return std::string(begin, end);
}

std::string stripQuotes(const std::string& value) {
  if (value.size() >= 2 &&
      ((value.front() == '"' && value.back() == '"') ||
       (value.front() == '\'' && value.back() == '\''))) {
    return value.substr(1, value.size() - 2);
  }
  return value;
}

[[maybe_unused]] std::string removeYamlComment(const std::string& line) {
  bool in_single = false;
  bool in_double = false;
  for (size_t i = 0; i < line.size(); ++i) {
    const char c = line[i];
    if (c == '\'' && !in_double) {
      in_single = !in_single;
    } else if (c == '"' && !in_single) {
      in_double = !in_double;
    } else if (c == '#' && !in_single && !in_double) {
      return line.substr(0, i);
    }
  }
  return line;
}

std::string lowerCopy(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return value;
}

std::string jsonEscape(const std::string& value) {
  std::ostringstream ss;
  for (const char c : value) {
    switch (c) {
      case '"':
        ss << "\\\"";
        break;
      case '\\':
        ss << "\\\\";
        break;
      case '\n':
        ss << "\\n";
        break;
      case '\r':
        ss << "\\r";
        break;
      case '\t':
        ss << "\\t";
        break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          ss << "\\u" << std::hex << std::setw(4) << std::setfill('0')
             << static_cast<int>(static_cast<unsigned char>(c));
        } else {
          ss << c;
        }
        break;
    }
  }
  return ss.str();
}

std::string jsonQuoted(const std::string& value) {
  return "\"" + jsonEscape(value) + "\"";
}

#ifdef RTR_HAS_YAML_CPP
void flattenYamlNode(const YAML::Node& node, const std::string& prefix,
                     const std::function<void(const std::string&,
                                              const std::string&)>& emit) {
  if (node.IsMap()) {
    for (const auto& item : node) {
      const std::string key = item.first.as<std::string>();
      const std::string next = prefix.empty() ? key : prefix + "." + key;
      flattenYamlNode(item.second, next, emit);
    }
  } else if (node.IsScalar()) {
    emit(prefix, node.as<std::string>());
  }
}
#endif

template <typename T>
T clampNumeric(T value, T min_value, T max_value) {
  return std::max(min_value, std::min(value, max_value));
}

}  // namespace

namespace rtr {

bool ParamService::loadFile(const std::string& path, std::string* error) {
  if (path.empty()) {
    return true;
  }

#ifdef RTR_HAS_YAML_CPP
  try {
    const YAML::Node root = YAML::LoadFile(path);
    flattenYamlNode(root, "", [this](const std::string& key,
                                      const std::string& value) {
      rememberConfigValue(key, value);
    });
    return true;
  } catch (const std::exception& e) {
    if (error) {
      *error = e.what();
    }
    return false;
  }
#else
  std::ifstream input(path);
  if (!input) {
    if (error) {
      *error = "unable to open config file: " + path;
    }
    return false;
  }

  std::vector<std::pair<int, std::string>> stack;
  std::string line;
  while (std::getline(input, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    const std::string without_comment = removeYamlComment(line);
    if (trim(without_comment).empty()) {
      continue;
    }

    int indent = 0;
    while (indent < static_cast<int>(without_comment.size()) &&
           without_comment[static_cast<size_t>(indent)] == ' ') {
      ++indent;
    }
    const std::string content = trim(without_comment);
    const size_t colon = content.find(':');
    if (colon == std::string::npos) {
      continue;
    }

    std::string key = trim(content.substr(0, colon));
    std::string value = trim(content.substr(colon + 1));
    key = stripQuotes(key);

    while (!stack.empty() && stack.back().first >= indent) {
      stack.pop_back();
    }

    if (value.empty()) {
      stack.emplace_back(indent, key);
      continue;
    }

    std::string full_key;
    for (const auto& part : stack) {
      if (!full_key.empty()) {
        full_key += ".";
      }
      full_key += part.second;
    }
    if (!full_key.empty()) {
      full_key += ".";
    }
    full_key += key;
    rememberConfigValue(full_key, stripQuotes(value));
  }
  return true;
#endif
}

void ParamService::declareDouble(const std::string& key, double default_value,
                                 double min_value, double max_value,
                                 const std::string& description,
                                 bool runtime_mutable) {
  ParamInfo info;
  info.key = key;
  info.type = ParamType::Double;
  info.value = default_value;
  info.default_value = default_value;
  info.min_value = min_value;
  info.max_value = max_value;
  info.description = description;
  info.runtime_mutable = runtime_mutable;
  declareParam(info);
}

void ParamService::declareInt(const std::string& key, int64_t default_value,
                              int64_t min_value, int64_t max_value,
                              const std::string& description,
                              bool runtime_mutable) {
  ParamInfo info;
  info.key = key;
  info.type = ParamType::Int;
  info.value = default_value;
  info.default_value = default_value;
  info.min_value = min_value;
  info.max_value = max_value;
  info.description = description;
  info.runtime_mutable = runtime_mutable;
  declareParam(info);
}

void ParamService::declareBool(const std::string& key, bool default_value,
                               const std::string& description,
                               bool runtime_mutable) {
  ParamInfo info;
  info.key = key;
  info.type = ParamType::Bool;
  info.value = default_value;
  info.default_value = default_value;
  info.description = description;
  info.runtime_mutable = runtime_mutable;
  declareParam(info);
}

void ParamService::declareString(const std::string& key,
                                 const std::string& default_value,
                                 const std::string& description,
                                 bool runtime_mutable) {
  ParamInfo info;
  info.key = key;
  info.type = ParamType::String;
  info.value = default_value;
  info.default_value = default_value;
  info.description = description;
  info.runtime_mutable = runtime_mutable;
  declareParam(info);
}

double ParamService::getDouble(const std::string& key, double fallback) const {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto it = params_.find(key);
  if (it == params_.end() || !std::holds_alternative<double>(it->second.value)) {
    return fallback;
  }
  return std::get<double>(it->second.value);
}

int64_t ParamService::getInt(const std::string& key, int64_t fallback) const {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto it = params_.find(key);
  if (it == params_.end() || !std::holds_alternative<int64_t>(it->second.value)) {
    return fallback;
  }
  return std::get<int64_t>(it->second.value);
}

bool ParamService::getBool(const std::string& key, bool fallback) const {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto it = params_.find(key);
  if (it == params_.end() || !std::holds_alternative<bool>(it->second.value)) {
    return fallback;
  }
  return std::get<bool>(it->second.value);
}

std::string ParamService::getString(const std::string& key,
                                    const std::string& fallback) const {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto it = params_.find(key);
  if (it == params_.end() ||
      !std::holds_alternative<std::string>(it->second.value)) {
    return fallback;
  }
  return std::get<std::string>(it->second.value);
}

bool ParamService::setFromString(const std::string& key,
                                 const std::string& value,
                                 std::string* error) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto it = params_.find(key);
  if (it == params_.end()) {
    if (error) {
      *error = "unknown parameter: " + key;
    }
    return false;
  }
  const ParamValue parsed = parseValue(it->second.type, value, error);
  if (error && !error->empty()) {
    return false;
  }
  return setValueLocked(it->second, parsed, true, error);
}

bool ParamService::setDouble(const std::string& key, double value,
                             std::string* error) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = params_.find(key);
  if (it == params_.end()) {
    if (error) {
      *error = "unknown parameter: " + key;
    }
    return false;
  }
  return setValueLocked(it->second, value, true, error);
}

bool ParamService::setInt(const std::string& key, int64_t value,
                          std::string* error) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = params_.find(key);
  if (it == params_.end()) {
    if (error) {
      *error = "unknown parameter: " + key;
    }
    return false;
  }
  return setValueLocked(it->second, value, true, error);
}

bool ParamService::setBool(const std::string& key, bool value,
                           std::string* error) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = params_.find(key);
  if (it == params_.end()) {
    if (error) {
      *error = "unknown parameter: " + key;
    }
    return false;
  }
  return setValueLocked(it->second, value, true, error);
}

bool ParamService::setString(const std::string& key, const std::string& value,
                             std::string* error) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = params_.find(key);
  if (it == params_.end()) {
    if (error) {
      *error = "unknown parameter: " + key;
    }
    return false;
  }
  return setValueLocked(it->second, value, true, error);
}

std::vector<ParamInfo> ParamService::list() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<ParamInfo> result;
  result.reserve(params_.size());
  for (const auto& item : params_) {
    result.push_back(item.second);
  }
  return result;
}

std::optional<ParamInfo> ParamService::getInfo(const std::string& key) const {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto it = params_.find(key);
  if (it == params_.end()) {
    return std::nullopt;
  }
  return it->second;
}

std::string ParamService::typeName(ParamType type) {
  switch (type) {
    case ParamType::Double:
      return "double";
    case ParamType::Int:
      return "int";
    case ParamType::Bool:
      return "bool";
    case ParamType::String:
      return "string";
  }
  return "unknown";
}

std::string ParamService::valueToString(const ParamValue& value) {
  if (std::holds_alternative<double>(value)) {
    std::ostringstream ss;
    ss << std::setprecision(8) << std::get<double>(value);
    return ss.str();
  }
  if (std::holds_alternative<int64_t>(value)) {
    return std::to_string(std::get<int64_t>(value));
  }
  if (std::holds_alternative<bool>(value)) {
    return std::get<bool>(value) ? "true" : "false";
  }
  return std::get<std::string>(value);
}

void ParamService::declareParam(const ParamInfo& info) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto inserted = params_.emplace(info.key, info);
  if (!inserted.second) {
    inserted.first->second.description = info.description;
    inserted.first->second.min_value = info.min_value;
    inserted.first->second.max_value = info.max_value;
    inserted.first->second.runtime_mutable = info.runtime_mutable;
    return;
  }

  const auto pending = pending_config_.find(info.key);
  if (pending != pending_config_.end()) {
    std::string error;
    const ParamValue parsed =
        parseValue(info.type, pending->second, &error);
    if (error.empty()) {
      setValueLocked(inserted.first->second, parsed, false, nullptr);
    }
  }
}

bool ParamService::setValueLocked(ParamInfo& info, const ParamValue& value,
                                  bool respect_mutability,
                                  std::string* error) {
  if (respect_mutability && !info.runtime_mutable) {
    if (error) {
      *error = "parameter is not runtime mutable: " + info.key;
    }
    return false;
  }

  if (info.type == ParamType::Double) {
    if (!std::holds_alternative<double>(value)) {
      if (error) {
        *error = "expected double for " + info.key;
      }
      return false;
    }
    const double v = std::get<double>(value);
    if (info.min_value && v < std::get<double>(*info.min_value)) {
      if (error) {
        *error = "value below min for " + info.key;
      }
      return false;
    }
    if (info.max_value && v > std::get<double>(*info.max_value)) {
      if (error) {
        *error = "value above max for " + info.key;
      }
      return false;
    }
  } else if (info.type == ParamType::Int) {
    if (!std::holds_alternative<int64_t>(value)) {
      if (error) {
        *error = "expected int for " + info.key;
      }
      return false;
    }
    const int64_t v = std::get<int64_t>(value);
    if (info.min_value && v < std::get<int64_t>(*info.min_value)) {
      if (error) {
        *error = "value below min for " + info.key;
      }
      return false;
    }
    if (info.max_value && v > std::get<int64_t>(*info.max_value)) {
      if (error) {
        *error = "value above max for " + info.key;
      }
      return false;
    }
  } else if (info.type == ParamType::Bool) {
    if (!std::holds_alternative<bool>(value)) {
      if (error) {
        *error = "expected bool for " + info.key;
      }
      return false;
    }
  } else if (!std::holds_alternative<std::string>(value)) {
    if (error) {
      *error = "expected string for " + info.key;
    }
    return false;
  }

  info.value = value;
  if (error) {
    error->clear();
  }
  return true;
}

ParamValue ParamService::parseValue(ParamType type, const std::string& text,
                                    std::string* error) const {
  if (error) {
    error->clear();
  }
  try {
    switch (type) {
      case ParamType::Double:
        return std::stod(text);
      case ParamType::Int:
        return static_cast<int64_t>(std::stoll(text));
      case ParamType::Bool: {
        const std::string value = lowerCopy(stripQuotes(trim(text)));
        if (value == "true" || value == "1" || value == "yes" ||
            value == "on") {
          return true;
        }
        if (value == "false" || value == "0" || value == "no" ||
            value == "off") {
          return false;
        }
        throw std::invalid_argument("invalid bool");
      }
      case ParamType::String:
        return stripQuotes(text);
    }
  } catch (const std::exception& e) {
    if (error) {
      *error = "failed to parse value '" + text + "': " + e.what();
    }
  }
  return std::string{};
}

void ParamService::rememberConfigValue(const std::string& key,
                                       const std::string& value) {
  std::lock_guard<std::mutex> lock(mutex_);
  pending_config_[key] = value;
  auto it = params_.find(key);
  if (it == params_.end()) {
    return;
  }
  std::string error;
  const ParamValue parsed = parseValue(it->second.type, value, &error);
  if (error.empty()) {
    setValueLocked(it->second, parsed, false, nullptr);
  }
}

LogService::LogService(size_t capacity) : capacity_(capacity) {}

void LogService::info(const std::string& message) {
  write("INFO", message);
}

void LogService::warn(const std::string& message) {
  write("WARN", message);
}

void LogService::error(const std::string& message) {
  write("ERROR", message);
}

void LogService::debug(const std::string& message) {
  write("DEBUG", message);
}

std::vector<LogEntry> LogService::recent() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return std::vector<LogEntry>(entries_.begin(), entries_.end());
}

void LogService::write(const std::string& level, const std::string& message) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    entries_.push_back({nowMillis(), level, message});
    while (entries_.size() > capacity_) {
      entries_.pop_front();
    }
  }

#ifdef RTR_HAS_SPDLOG
  if (level == "INFO") {
    spdlog::info("{}", message);
  } else if (level == "WARN") {
    spdlog::warn("{}", message);
  } else if (level == "ERROR") {
    spdlog::error("{}", message);
  } else {
    spdlog::debug("{}", message);
  }
#else
  std::cout << "[" << level << "] " << message << std::endl;
#endif
}

DebugService::DebugService(size_t history_limit)
    : history_limit_(history_limit) {}

void DebugService::setGlobalEnabled(bool enabled) {
  global_enabled_.store(enabled, std::memory_order_relaxed);
  if (!enabled) {
    std::lock_guard<std::mutex> lock(mutex_);
    images_.clear();
    last_image_ms_.clear();
    values_.clear();
    histories_.clear();
  }
}

void DebugService::setEnabled(const std::string& key, bool enabled) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (enabled) {
    disabled_keys_.erase(key);
  } else {
    disabled_keys_.insert(key);
  }
}

bool DebugService::enabled(const std::string& key) const {
  if (!global_enabled_.load(std::memory_order_relaxed)) {
    return false;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  return disabled_keys_.count(key) == 0;
}

void DebugService::setImageRateLimitHz(double hz) {
  std::lock_guard<std::mutex> lock(mutex_);
  image_min_interval_ms_ = hz <= 0.0 ? 0.0 : 1000.0 / hz;
}

bool DebugService::image(const std::string& key, const ImageFrame& image) {
  if (!global_enabled_.load(std::memory_order_relaxed)) {
    return false;
  }
  if (!image.valid()) {
    return false;
  }
  const int64_t now = nowMillis();
  std::lock_guard<std::mutex> lock(mutex_);
  if (disabled_keys_.count(key) != 0) {
    return false;
  }
  const auto last = last_image_ms_.find(key);
  if (last != last_image_ms_.end() &&
      static_cast<double>(now - last->second) < image_min_interval_ms_) {
    return false;
  }
  images_[key] = {key, image, now};
  last_image_ms_[key] = now;
  return true;
}

#ifdef RTR_USE_OPENCV
bool DebugService::image(const std::string& key, const cv::Mat& mat) {
  if (mat.empty()) {
    return false;
  }
  cv::Mat rgb;
  if (mat.channels() == 3) {
    cv::cvtColor(mat, rgb, cv::COLOR_BGR2RGB);
  } else if (mat.channels() == 1) {
    cv::cvtColor(mat, rgb, cv::COLOR_GRAY2RGB);
  } else {
    return false;
  }

  ImageFrame frame;
  frame.width = rgb.cols;
  frame.height = rgb.rows;
  frame.channels = 3;
  frame.encoding = "rgb8";
  frame.data.assign(rgb.data, rgb.data + rgb.total() * rgb.elemSize());
  return image(key, frame);
}
#endif

void DebugService::scalar(const std::string& key, double value) {
  if (!global_enabled_.load(std::memory_order_relaxed)) {
    return;
  }
  const int64_t now = nowMillis();
  std::lock_guard<std::mutex> lock(mutex_);
  values_[key] = {key, DebugValueType::Scalar, value, now};
  rememberHistoryLocked(key, value, now);
}

void DebugService::integer(const std::string& key, int64_t value) {
  if (!global_enabled_.load(std::memory_order_relaxed)) {
    return;
  }
  const int64_t now = nowMillis();
  std::lock_guard<std::mutex> lock(mutex_);
  values_[key] = {key, DebugValueType::Int, value, now};
  rememberHistoryLocked(key, static_cast<double>(value), now);
}

void DebugService::boolean(const std::string& key, bool value) {
  if (!global_enabled_.load(std::memory_order_relaxed)) {
    return;
  }
  const int64_t now = nowMillis();
  std::lock_guard<std::mutex> lock(mutex_);
  values_[key] = {key, DebugValueType::Bool, value, now};
}

void DebugService::text(const std::string& key, const std::string& value) {
  if (!global_enabled_.load(std::memory_order_relaxed)) {
    return;
  }
  const int64_t now = nowMillis();
  std::lock_guard<std::mutex> lock(mutex_);
  values_[key] = {key, DebugValueType::Text, value, now};
}

std::optional<DebugImageEntry> DebugService::latestImage(
    const std::string& key) const {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto it = images_.find(key);
  if (it == images_.end()) {
    return std::nullopt;
  }
  return it->second;
}

DebugList DebugService::list() const {
  std::lock_guard<std::mutex> lock(mutex_);
  DebugList result;
  for (const auto& item : images_) {
    result.images.push_back(item.first);
  }
  for (const auto& item : values_) {
    if (item.second.type == DebugValueType::Scalar) {
      result.scalars.push_back(item.first);
    } else if (item.second.type == DebugValueType::Int) {
      result.integers.push_back(item.first);
    } else if (item.second.type == DebugValueType::Bool) {
      result.booleans.push_back(item.first);
    } else {
      result.texts.push_back(item.first);
    }
    result.values.push_back(item.second);
  }
  return result;
}

std::vector<DebugValueEntry> DebugService::values() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<DebugValueEntry> result;
  result.reserve(values_.size());
  for (const auto& item : values_) {
    result.push_back(item.second);
  }
  return result;
}

std::map<std::string, std::vector<HistoryPoint>> DebugService::histories()
    const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::map<std::string, std::vector<HistoryPoint>> result;
  for (const auto& item : histories_) {
    result[item.first] =
        std::vector<HistoryPoint>(item.second.begin(), item.second.end());
  }
  return result;
}

void DebugService::rememberHistoryLocked(const std::string& key, double value,
                                         int64_t timestamp_ms) {
  auto& history = histories_[key];
  history.push_back({timestamp_ms, value});
  while (history.size() > history_limit_) {
    history.pop_front();
  }
}

void CommandQueue::push(RuntimeCommand command) {
  std::lock_guard<std::mutex> lock(mutex_);
  queue_.push(std::move(command));
}

bool CommandQueue::tryPop(RuntimeCommand& command) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (queue_.empty()) {
    return false;
  }
  command = std::move(queue_.front());
  queue_.pop();
  return true;
}

Runtime::Runtime()
    : param_(),
      log_(),
      debug_(),
      rtr_(param_, log_, debug_) {
  std::signal(SIGINT, handleSignal);
#ifdef SIGTERM
  std::signal(SIGTERM, handleSignal);
#endif
}

Runtime::~Runtime() {
  if (webui_) {
    webui_->stop();
  }
}

int Runtime::run(std::unique_ptr<Task> task, const RuntimeOptions& options) {
  task_ = std::move(task);
  options_ = options;
#ifdef RTR_WEBUI_DIR
  if (options_.webui_static_dir.empty()) {
    options_.webui_static_dir = RTR_WEBUI_DIR;
  }
#endif

  param_.declareDouble("runtime.update_rate_hz", options_.update_rate_hz, 1.0,
                       1000.0, "Runtime update rate in Hz");
  param_.declareBool("runtime.webui.enabled", options_.webui_enabled,
                     "Enable built-in WebUI server");
  param_.declareInt("runtime.webui.port", options_.webui_port, 1, 65535,
                    "Built-in WebUI HTTP port");

  if (!options_.config_path.empty()) {
    std::string error;
    if (!param_.loadFile(options_.config_path, &error)) {
      log_.error("Failed to load config: " + error);
      setState(RuntimeState::Error);
      return 1;
    }
    log_.info("Loaded config: " + options_.config_path);
  }

  if (options_.update_rate_flag_set) {
    std::string error;
    param_.setDouble("runtime.update_rate_hz", options_.update_rate_hz, &error);
  }
  options_.update_rate_hz =
      param_.getDouble("runtime.update_rate_hz", options_.update_rate_hz);
  if (!options_.webui_flag_set) {
    options_.webui_enabled =
        param_.getBool("runtime.webui.enabled", options_.webui_enabled);
  } else {
    std::string error;
    param_.setBool("runtime.webui.enabled", options_.webui_enabled, &error);
  }
  if (options_.webui_port_flag_set) {
    std::string error;
    param_.setInt("runtime.webui.port", options_.webui_port, &error);
  }
  options_.webui_port =
      static_cast<int>(param_.getInt("runtime.webui.port", options_.webui_port));

  debug_.setGlobalEnabled(options_.webui_enabled);
  debug_.setImageRateLimitHz(15.0);

#if RTR_ENABLE_WEBUI
  if (options_.webui_enabled) {
    webui_ = std::make_unique<WebUiServer>(
        commands_, param_, log_, debug_,
        [this]() { return runtimeStatusJson(); });
    WebUiServerOptions web_options;
    web_options.port = options_.webui_port;
    web_options.static_dir = options_.webui_static_dir;
    std::string error;
    if (!webui_->start(web_options, &error)) {
      log_.warn("WebUI disabled: " + error);
      debug_.setGlobalEnabled(false);
    } else {
      std::ostringstream ss;
      ss << "WebUI listening at http://127.0.0.1:" << web_options.port;
      log_.info(ss.str());
    }
  }
#else
  if (options_.webui_enabled) {
    log_.warn("WebUI requested but this binary was built with RTR_ENABLE_WEBUI=OFF");
  }
#endif

  commands_.push({RuntimeCommandType::Run, "startup"});

  auto next_frame_at = std::chrono::steady_clock::now();
  auto last_loop_at = next_frame_at;
  int64_t executed_frames = 0;

  while (!shutdown_requested_) {
    if (g_signal_shutdown.exchange(false)) {
      commands_.push({RuntimeCommandType::Shutdown, "signal"});
    }

    handleCommands();

    if (state() == RuntimeState::Running) {
      const auto frame_begin = std::chrono::steady_clock::now();
      const bool collect_runtime_status = webui_ && webui_->running();
      if (collect_runtime_status) {
        const double dt_loop =
            std::chrono::duration<double>(frame_begin - last_loop_at).count();
        if (dt_loop > 1e-6) {
          std::lock_guard<std::mutex> lock(perf_mutex_);
          perf_.loop_hz = 1.0 / dt_loop;
        }
      }
      last_loop_at = frame_begin;

      try {
        task_->onUpdate(rtr_);
      } catch (const std::exception& e) {
        log_.error(std::string("Task update exception: ") + e.what());
        setState(RuntimeState::Error);
        commands_.push({RuntimeCommandType::Shutdown, "runtime-error"});
      }

      const auto frame_end = std::chrono::steady_clock::now();
      const int64_t frame_index = ++executed_frames;
      if (collect_runtime_status) {
        std::lock_guard<std::mutex> lock(perf_mutex_);
        perf_.update_ms =
            std::chrono::duration<double, std::milli>(frame_end - frame_begin)
                .count();
        perf_.frame_index = frame_index;
      }

      if (options_.max_frames > 0 && frame_index >= options_.max_frames) {
        commands_.push({RuntimeCommandType::Shutdown, "max-frames"});
      }

      const double rate =
          param_.getDouble("runtime.update_rate_hz", options_.update_rate_hz);
      const auto period =
          std::chrono::duration_cast<std::chrono::steady_clock::duration>(
              std::chrono::duration<double>(1.0 / std::max(rate, 1.0)));
      next_frame_at = frame_begin + period;
      std::this_thread::sleep_until(next_frame_at);
    } else {
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
  }

  if (state() != RuntimeState::Stopped && state() != RuntimeState::Error) {
    stopTask();
  }

  if (webui_) {
    webui_->stop();
  }

  return state() == RuntimeState::Error ? 1 : 0;
}

void Runtime::requestShutdown() {
  commands_.push({RuntimeCommandType::Shutdown, "local"});
}

RuntimeState Runtime::state() const {
  std::lock_guard<std::mutex> lock(state_mutex_);
  return state_;
}

std::string Runtime::stateString() const {
  return stateToString(state());
}

std::string Runtime::runtimeStatusJson() const {
  PerfStats perf;
  {
    std::lock_guard<std::mutex> lock(perf_mutex_);
    perf = perf_;
  }

  std::ostringstream ss;
  ss << "{";
  ss << "\"runtime_status\":" << jsonQuoted(stateString()) << ",";
  ss << "\"task_name\":" << jsonQuoted(options_.task_name) << ",";
  ss << "\"config_path\":" << jsonQuoted(options_.config_path) << ",";
  ss << "\"loop_hz\":" << std::setprecision(8) << perf.loop_hz << ",";
  ss << "\"update_ms\":" << std::setprecision(8) << perf.update_ms << ",";
  ss << "\"frame_index\":" << perf.frame_index << ",";
  ss << "\"update_rate_hz\":"
     << std::setprecision(8)
     << param_.getDouble("runtime.update_rate_hz", options_.update_rate_hz)
     << ",";
  ss << "\"webui\":" << ((webui_ && webui_->running()) ? "true" : "false")
     << ",";
  ss << "\"webui_port\":" << options_.webui_port << ",";
  ss << "\"initialized\":" << (initialized_ ? "true" : "false") << ",";
  ss << "\"started\":" << (started_once_ ? "true" : "false") << ",";
  ss << "\"shutdown_requested\":"
     << (shutdown_requested_.load() ? "true" : "false");
  ss << "}";
  return ss.str();
}

std::string Runtime::stateToString(RuntimeState state) {
  switch (state) {
    case RuntimeState::Idle:
      return "Idle";
    case RuntimeState::Starting:
      return "Starting";
    case RuntimeState::Running:
      return "Running";
    case RuntimeState::Paused:
      return "Paused";
    case RuntimeState::Stopping:
      return "Stopping";
    case RuntimeState::Stopped:
      return "Stopped";
    case RuntimeState::Error:
      return "Error";
  }
  return "Unknown";
}

std::string Runtime::commandToString(RuntimeCommandType command) {
  switch (command) {
    case RuntimeCommandType::Run:
      return "Run";
    case RuntimeCommandType::Pause:
      return "Pause";
    case RuntimeCommandType::Resume:
      return "Resume";
    case RuntimeCommandType::Stop:
      return "Stop";
    case RuntimeCommandType::Shutdown:
      return "Shutdown";
  }
  return "Unknown";
}

bool Runtime::initTask() {
  if (initialized_) {
    return true;
  }
  setState(RuntimeState::Starting);
  log_.info("Task onInit");
  if (!task_ || !task_->onInit(rtr_)) {
    log_.error("Task onInit failed");
    setState(RuntimeState::Error);
    return false;
  }
  initialized_ = true;
  return true;
}

bool Runtime::startTask() {
  if (!initTask()) {
    return false;
  }
  setState(RuntimeState::Starting);
  log_.info("Task onStart");
  if (!task_->onStart(rtr_)) {
    log_.error("Task onStart failed");
    setState(RuntimeState::Error);
    return false;
  }
  started_once_ = true;
  setState(RuntimeState::Running);
  return true;
}

void Runtime::stopTask() {
  const RuntimeState current = state();
  if (current == RuntimeState::Stopped || current == RuntimeState::Idle ||
      current == RuntimeState::Error || !task_) {
    setState(RuntimeState::Stopped);
    return;
  }
  setState(RuntimeState::Stopping);
  log_.info("Task onStop");
  try {
    task_->onStop(rtr_);
  } catch (const std::exception& e) {
    log_.error(std::string("Task stop exception: ") + e.what());
    setState(RuntimeState::Error);
    return;
  }
  started_once_ = false;
  setState(RuntimeState::Stopped);
}

void Runtime::handleCommands() {
  RuntimeCommand command;
  while (commands_.tryPop(command)) {
    log_.info("Runtime command: " + commandToString(command.type) +
              " from " + command.source);
    applyCommand(command);
  }
}

void Runtime::applyCommand(const RuntimeCommand& command) {
  const RuntimeState current = state();
  switch (command.type) {
    case RuntimeCommandType::Run:
      if (current == RuntimeState::Paused) {
        applyCommand({RuntimeCommandType::Resume, command.source});
      } else if (current == RuntimeState::Idle ||
                 current == RuntimeState::Stopped) {
        if (!startTask()) {
          shutdown_requested_ = true;
        }
      }
      break;
    case RuntimeCommandType::Pause:
      if (current == RuntimeState::Running) {
        log_.info("Task onPause");
        task_->onPause(rtr_);
        setState(RuntimeState::Paused);
      }
      break;
    case RuntimeCommandType::Resume:
      if (current == RuntimeState::Paused) {
        log_.info("Task onResume");
        task_->onResume(rtr_);
        setState(RuntimeState::Running);
      }
      break;
    case RuntimeCommandType::Stop:
      if (current == RuntimeState::Running || current == RuntimeState::Paused ||
          current == RuntimeState::Starting) {
        stopTask();
      }
      break;
    case RuntimeCommandType::Shutdown:
      if (current != RuntimeState::Stopped && current != RuntimeState::Error &&
          current != RuntimeState::Idle) {
        stopTask();
      }
      shutdown_requested_ = true;
      break;
  }
}

void Runtime::setState(RuntimeState state) {
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (state_ == state) {
      return;
    }
    state_ = state;
  }
}

RuntimeOptions parseRuntimeOptions(int argc, char** argv) {
  RuntimeOptions options;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    auto requireValue = [&](const std::string& name) -> std::string {
      if (i + 1 >= argc) {
        throw std::runtime_error("missing value for " + name);
      }
      return argv[++i];
    };

    if (arg == "--task") {
      options.task_name = requireValue(arg);
    } else if (arg == "--config") {
      options.config_path = requireValue(arg);
    } else if (arg == "--webui") {
      options.webui_enabled = true;
      options.webui_flag_set = true;
    } else if (arg == "--no-webui") {
      options.webui_enabled = false;
      options.webui_flag_set = true;
    } else if (arg == "--port") {
      options.webui_port = std::stoi(requireValue(arg));
      options.webui_port_flag_set = true;
    } else if (arg == "--rate") {
      options.update_rate_hz = std::stod(requireValue(arg));
      options.update_rate_flag_set = true;
    } else if (arg == "--max-frames") {
      options.max_frames = std::stoll(requireValue(arg));
    } else if (arg == "--webui-dir") {
      options.webui_static_dir = requireValue(arg);
    } else if (arg == "--help" || arg == "-h") {
      printUsage();
      std::exit(0);
    } else {
      throw std::runtime_error("unknown argument: " + arg);
    }
  }
  return options;
}

void printUsage() {
  std::cout
      << "RoboTaskRuntime\n"
      << "Usage:\n"
      << "  robotaskrun --task minimal --config configs/minimal_task.yaml "
         "[--webui]\n"
      << "  robotaskrun --task spr_vision_fake_test "
         "--config configs/spr_vision_26_fake_test.yaml "
         "[--webui]\n\n"
      << "Options:\n"
      << "  --task <name>               Select the single task to run\n"
      << "  --config <path>             YAML config path\n"
      << "  --webui / --no-webui        Override WebUI config\n"
      << "  --port <port>               Override WebUI port\n"
      << "  --rate <hz>                 Override runtime update rate\n"
      << "  --max-frames <n>            Run a bounded smoke test and exit\n";
}

}  // namespace rtr
