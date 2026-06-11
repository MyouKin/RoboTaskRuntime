#pragma once

#include <string>

namespace rtr {

class Rtr;

template <typename T>
class Sink {
 public:
  virtual bool configure(Rtr& rtr, const std::string& prefix) = 0;
  virtual bool open() = 0;
  virtual bool send(const T& data) = 0;
  virtual void stop() = 0;
  virtual void close() = 0;
  virtual ~Sink() = default;
};

}  // namespace rtr

