#pragma once

#include <string>

namespace rtr {

class Rtr;

class DataSource {
 public:
  virtual bool configure(Rtr& rtr, const std::string& prefix) = 0;
  virtual bool open() = 0;
  virtual bool start() = 0;
  virtual void stop() = 0;
  virtual void close() = 0;
  virtual ~DataSource() = default;
};

}  // namespace rtr

