#include "spr_vision_image_bridge.hpp"

namespace tasks::spr_vision_26 {

rtr::ImageFrame toImageFrame(const cv::Mat& bgr, const std::string& frame_id) {
  rtr::ImageFrame frame;
  if (bgr.empty()) {
    return frame;
  }

  cv::Mat rgb;
  if (bgr.channels() == 3) {
    cv::cvtColor(bgr, rgb, cv::COLOR_BGR2RGB);
  } else if (bgr.channels() == 1) {
    cv::cvtColor(bgr, rgb, cv::COLOR_GRAY2RGB);
  } else {
    return frame;
  }

  frame.header.stamp = rtr::TimeStamp::now();
  frame.header.frame_id = frame_id;
  frame.width = rgb.cols;
  frame.height = rgb.rows;
  frame.channels = 3;
  frame.encoding = "rgb8";
  frame.data.assign(rgb.data, rgb.data + rgb.total() * rgb.elemSize());
  return frame;
}

}  // namespace tasks::spr_vision_26

