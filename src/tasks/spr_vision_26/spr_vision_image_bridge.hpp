#pragma once

#include <opencv2/opencv.hpp>

#include "rtr/core/data.hpp"

namespace tasks::spr_vision_26 {

rtr::ImageFrame toImageFrame(const cv::Mat& bgr, const std::string& frame_id);

}  // namespace tasks::spr_vision_26

