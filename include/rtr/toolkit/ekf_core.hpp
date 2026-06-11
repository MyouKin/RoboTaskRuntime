#pragma once

#include <string>

namespace rtr {

class EkfCore {
 public:
  virtual ~EkfCore() = default;
  virtual void reset() = 0;
  virtual std::string name() const { return "EkfCore"; }
};

}  // namespace rtr

