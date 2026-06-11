#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#ifdef RTR_USE_OPENCV
#include <opencv2/core.hpp>
#endif

namespace rtr {

struct TimeStamp {
  int64_t sec = 0;
  uint32_t nsec = 0;

  static TimeStamp now() {
    using namespace std::chrono;
    const auto ns = duration_cast<nanoseconds>(
        system_clock::now().time_since_epoch());
    TimeStamp stamp;
    stamp.sec = ns.count() / 1000000000LL;
    stamp.nsec = static_cast<uint32_t>(ns.count() % 1000000000LL);
    return stamp;
  }
};

inline int64_t nowMillis() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(
             system_clock::now().time_since_epoch())
      .count();
}

struct Header {
  TimeStamp stamp;
  std::string frame_id;
  uint64_t sequence = 0;
};

template <typename T>
struct Stamped {
  Header header;
  T data;
};

struct ImageFrame {
  Header header;
  int width = 0;
  int height = 0;
  int channels = 3;
  std::string encoding = "rgb8";
  std::vector<uint8_t> data;

  bool valid() const {
    return width > 0 && height > 0 && channels > 0 &&
           data.size() >= static_cast<size_t>(width * height * channels);
  }

  uint8_t* pixel(int x, int y) {
    return data.data() + static_cast<size_t>((y * width + x) * channels);
  }

  const uint8_t* pixel(int x, int y) const {
    return data.data() + static_cast<size_t>((y * width + x) * channels);
  }
};

}  // namespace rtr

