#pragma once

namespace rtr {

class Rtr;

class Task {
 public:
  virtual bool onInit(Rtr& rtr) = 0;
  virtual bool onStart(Rtr& rtr) {
    (void)rtr;
    return true;
  }
  virtual void onUpdate(Rtr& rtr) = 0;
  virtual void onPause(Rtr& rtr) { (void)rtr; }
  virtual void onResume(Rtr& rtr) { (void)rtr; }
  virtual void onStop(Rtr& rtr) { (void)rtr; }
  virtual void onReset(Rtr& rtr) { (void)rtr; }
  virtual ~Task() = default;
};

}  // namespace rtr
