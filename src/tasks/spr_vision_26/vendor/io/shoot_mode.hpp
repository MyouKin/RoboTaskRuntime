#ifndef IO__SHOOT_MODE_HPP
#define IO__SHOOT_MODE_HPP

#include <string>
#include <vector>

namespace io
{
enum ShootMode
{
  left_shoot,
  right_shoot,
  both_shoot
};

const std::vector<std::string> SHOOT_MODES = {"left_shoot", "right_shoot", "both_shoot"};

}  // namespace io

#endif  // IO__SHOOT_MODE_HPP

