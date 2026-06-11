#pragma once

#include <deque>
#include <mutex>
#include <string>
#include <vector>

#include "rtr/core/data.hpp"

namespace rtr {

struct LogEntry {
  int64_t timestamp_ms = 0;
  std::string level;
  std::string message;
};

class LogService {
 public:
  explicit LogService(size_t capacity = 300);

  void info(const std::string& message);
  void warn(const std::string& message);
  void error(const std::string& message);
  void debug(const std::string& message);

  std::vector<LogEntry> recent() const;

 private:
  void write(const std::string& level, const std::string& message);

  size_t capacity_ = 300;
  mutable std::mutex mutex_;
  std::deque<LogEntry> entries_;
};

}  // namespace rtr

