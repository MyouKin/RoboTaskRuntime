#pragma once

#include <optional>
#include <string>

#include "rtr/core/runtime.hpp"
#include "rtr/io/sink.hpp"

namespace rtr {

template <typename T>
class DummySink : public Sink<T> {
 public:
  bool configure(Rtr& rtr, const std::string& prefix) override {
    prefix_ = prefix;
    rtr.param().declareBool(prefix + ".enabled", true,
                            "Enable the dummy output sink");
    rtr.log().info("DummySink configured at " + prefix);
    return true;
  }

  bool open() override {
    opened_ = true;
    return true;
  }

  bool send(const T& data) override {
    if (!opened_) {
      return false;
    }
    last_ = data;
    ++send_count_;
    return true;
  }

  void stop() override {}

  void close() override {
    opened_ = false;
    last_.reset();
  }

  int64_t sendCount() const { return send_count_; }
  const std::optional<T>& last() const { return last_; }

 private:
  std::string prefix_;
  bool opened_ = false;
  int64_t send_count_ = 0;
  std::optional<T> last_;
};

}  // namespace rtr

