cmake_minimum_required(VERSION 3.16)

project(RoboTaskRuntime VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(RTR_ENABLE_WEBUI "Build the lightweight built-in WebUI server" ON)
option(RTR_USE_OPENCV "Enable OpenCV-backed debug image helpers" OFF)
option(RTR_REQUIRE_SPDLOG "Fail configure if spdlog is not found" OFF)
option(RTR_REQUIRE_YAML_CPP "Fail configure if yaml-cpp is not found" OFF)
option(RTR_ENABLE_SPR_VISION_26_FAKE_TEST "Build the macOS-friendly SPR Vision fake test task" ON)
option(RTR_ENABLE_SPR_VISION_26_REAL "Build the Linux hardware SPR Vision task" OFF)

find_package(spdlog QUIET)
find_package(yaml-cpp QUIET)
find_package(fmt QUIET)
find_package(Eigen3 QUIET)

set(RTR_HAS_SPR_VISION_26_FAKE_TEST OFF)
set(RTR_SPR_VISION_26_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/task/spr_vision_26")

if(RTR_USE_OPENCV)
  find_package(OpenCV REQUIRED COMPONENTS core imgproc)
endif()

if(RTR_ENABLE_SPR_VISION_26_FAKE_TEST)
  find_package(OpenCV QUIET COMPONENTS core imgproc calib3d)
  if(OpenCV_FOUND AND Eigen3_FOUND AND fmt_FOUND AND spdlog_FOUND AND yaml-cpp_FOUND)
    set(RTR_HAS_SPR_VISION_26_FAKE_TEST ON)
  else()
    message(WARNING
      "SPR Vision fake test task disabled because one or more dependencies are missing "
      "(OpenCV, Eigen3, fmt, spdlog, yaml-cpp).")
  endif()
endif()

set(RTR_SOURCES
  src/main.cpp
  src/core/runtime.cpp
  src/core/webui_server.cpp
  src/io/sim_io.cpp
  src/tasks/minimal_task/minimal_task.cpp
  src/task/spr_vision_26/spr_vision_task.cpp
)

if(RTR_HAS_SPR_VISION_26_FAKE_TEST)
  list(APPEND RTR_SOURCES
    src/task/spr_vision_26/spr_vision_image_bridge.cpp
    src/task/spr_vision_26/spr_vision_fake_test_task.cpp
    src/task/spr_vision_26/vendor/debug/param_tuner.cpp
    src/task/spr_vision_26/vendor/tasks/auto_aim/aimer.cpp
    src/task/spr_vision_26/vendor/tasks/auto_aim/armor.cpp
    src/task/spr_vision_26/vendor/tasks/auto_aim/shooter.cpp
    src/task/spr_vision_26/vendor/tasks/auto_aim/solver.cpp
    src/task/spr_vision_26/vendor/tasks/auto_aim/target.cpp
    src/task/spr_vision_26/vendor/tasks/auto_aim/tracker.cpp
    src/task/spr_vision_26/vendor/tools/extended_kalman_filter.cpp
    src/task/spr_vision_26/vendor/tools/logger.cpp
    src/task/spr_vision_26/vendor/tools/math_tools.cpp
    src/task/spr_vision_26/vendor/tools/trajectory.cpp
  )
endif()

if(RTR_ENABLE_SPR_VISION_26_REAL)
  if(APPLE)
    message(FATAL_ERROR "RTR_ENABLE_SPR_VISION_26_REAL is intended for the target Linux robot.")
  endif()
  find_package(OpenCV REQUIRED)
  find_package(OpenVINO REQUIRED COMPONENTS Runtime)
  find_package(nlohmann_json REQUIRED)

  list(APPEND RTR_SOURCES
    src/task/spr_vision_26/spr_vision_image_bridge.cpp
    src/task/spr_vision_26/vendor/debug/param_tuner.cpp
    src/task/spr_vision_26/vendor/io/camera.cpp
    src/task/spr_vision_26/vendor/io/cboard.cpp
    src/task/spr_vision_26/vendor/io/dm_imu/dm_imu.cpp
    src/task/spr_vision_26/vendor/io/gimbal/gimbal.cpp
    src/task/spr_vision_26/vendor/io/hikrobot/hikrobot.cpp
    src/task/spr_vision_26/vendor/io/mindvision/mindvision.cpp
    src/task/spr_vision_26/vendor/io/usbcamera/usbcamera.cpp
    src/task/spr_vision_26/vendor/io/serial/src/serial.cc
    src/task/spr_vision_26/vendor/io/serial/src/impl/unix.cc
    src/task/spr_vision_26/vendor/io/serial/src/impl/list_ports/list_ports_linux.cc
    src/task/spr_vision_26/vendor/tasks/auto_aim/aimer.cpp
    src/task/spr_vision_26/vendor/tasks/auto_aim/armor.cpp
    src/task/spr_vision_26/vendor/tasks/auto_aim/aux_solver.cpp
    src/task/spr_vision_26/vendor/tasks/auto_aim/classifier.cpp
    src/task/spr_vision_26/vendor/tasks/auto_aim/detector.cpp
    src/task/spr_vision_26/vendor/tasks/auto_aim/shooter.cpp
    src/task/spr_vision_26/vendor/tasks/auto_aim/solver.cpp
    src/task/spr_vision_26/vendor/tasks/auto_aim/target.cpp
    src/task/spr_vision_26/vendor/tasks/auto_aim/tracker.cpp
    src/task/spr_vision_26/vendor/tasks/auto_aim/voter.cpp
    src/task/spr_vision_26/vendor/tasks/auto_aim/yolo.cpp
    src/task/spr_vision_26/vendor/tasks/auto_aim/yolos/yolo11.cpp
    src/task/spr_vision_26/vendor/tasks/auto_aim/yolos/yolov5.cpp
    src/task/spr_vision_26/vendor/tasks/auto_aim/yolos/yolov8.cpp
    src/task/spr_vision_26/vendor/tasks/auto_aim/planner/planner.cpp
    src/task/spr_vision_26/vendor/tasks/auto_aim/planner/tinympc/admm.cpp
    src/task/spr_vision_26/vendor/tasks/auto_aim/planner/tinympc/codegen.cpp
    src/task/spr_vision_26/vendor/tasks/auto_aim/planner/tinympc/rho_benchmark.cpp
    src/task/spr_vision_26/vendor/tasks/auto_aim/planner/tinympc/tiny_api.cpp
    src/task/spr_vision_26/vendor/tasks/auto_buff/buff_aimer.cpp
    src/task/spr_vision_26/vendor/tasks/auto_buff/buff_detector.cpp
    src/task/spr_vision_26/vendor/tasks/auto_buff/buff_solver.cpp
    src/task/spr_vision_26/vendor/tasks/auto_buff/buff_target.cpp
    src/task/spr_vision_26/vendor/tasks/auto_buff/buff_type.cpp
    src/task/spr_vision_26/vendor/tasks/auto_buff/yolo11_buff.cpp
    src/task/spr_vision_26/vendor/tools/crc.cpp
    src/task/spr_vision_26/vendor/tools/extended_kalman_filter.cpp
    src/task/spr_vision_26/vendor/tools/img_tools.cpp
    src/task/spr_vision_26/vendor/tools/logger.cpp
    src/task/spr_vision_26/vendor/tools/math_tools.cpp
    src/task/spr_vision_26/vendor/tools/pid.cpp
    src/task/spr_vision_26/vendor/tools/plotter.cpp
    src/task/spr_vision_26/vendor/tools/ransac_sine_fitter.cpp
    src/task/spr_vision_26/vendor/tools/recorder.cpp
    src/task/spr_vision_26/vendor/tools/trajectory.cpp
  )
endif()

add_executable(robotaskrun ${RTR_SOURCES})

set_target_properties(robotaskrun PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_SOURCE_DIR}"
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_SOURCE_DIR}"
  RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_CURRENT_SOURCE_DIR}"
  RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_include_directories(robotaskrun PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/src/task/spr_vision_26/vendor
  ${CMAKE_CURRENT_SOURCE_DIR}/src/task/spr_vision_26
)

target_compile_definitions(robotaskrun PRIVATE
  RTR_WEBUI_DIR="${CMAKE_CURRENT_SOURCE_DIR}/src/webui"
  RTR_SPR_VISION_26_DIR="${RTR_SPR_VISION_26_DIR}"
  RTR_ENABLE_SPR_VISION_26_REAL=$<BOOL:${RTR_ENABLE_SPR_VISION_26_REAL}>
)

if(RTR_HAS_SPR_VISION_26_FAKE_TEST)
  target_compile_definitions(robotaskrun PRIVATE RTR_HAS_SPR_VISION_26_FAKE_TEST=1)
endif()

if(RTR_ENABLE_WEBUI)
  target_compile_definitions(robotaskrun PRIVATE RTR_ENABLE_WEBUI=1)
else()
  target_compile_definitions(robotaskrun PRIVATE RTR_ENABLE_WEBUI=0)
endif()

if(spdlog_FOUND)
  target_link_libraries(robotaskrun PRIVATE spdlog::spdlog)
  target_compile_definitions(robotaskrun PRIVATE RTR_HAS_SPDLOG=1)
else()
  if(RTR_REQUIRE_SPDLOG)
    message(FATAL_ERROR "spdlog was not found. Install spdlog or configure with -DRTR_REQUIRE_SPDLOG=OFF.")
  endif()
  message(STATUS "spdlog not found; using the small console fallback logger.")
endif()

if(fmt_FOUND)
  target_link_libraries(robotaskrun PRIVATE fmt::fmt)
endif()

if(Eigen3_FOUND)
  target_link_libraries(robotaskrun PRIVATE Eigen3::Eigen)
endif()

if(yaml-cpp_FOUND)
  target_link_libraries(robotaskrun PRIVATE yaml-cpp::yaml-cpp)
  target_compile_definitions(robotaskrun PRIVATE RTR_HAS_YAML_CPP=1)
else()
  if(RTR_REQUIRE_YAML_CPP)
    message(FATAL_ERROR "yaml-cpp was not found. Install yaml-cpp or configure with -DRTR_REQUIRE_YAML_CPP=OFF.")
  endif()
  message(STATUS "yaml-cpp not found; using the minimal YAML subset reader for sample configs.")
endif()

if(RTR_USE_OPENCV OR RTR_HAS_SPR_VISION_26_FAKE_TEST OR RTR_ENABLE_SPR_VISION_26_REAL)
  target_link_libraries(robotaskrun PRIVATE ${OpenCV_LIBS})
  target_include_directories(robotaskrun PRIVATE ${OpenCV_INCLUDE_DIRS})
endif()

if(RTR_USE_OPENCV)
  target_compile_definitions(robotaskrun PRIVATE RTR_USE_OPENCV=1)
endif()

if(RTR_ENABLE_SPR_VISION_26_REAL)
  target_link_libraries(robotaskrun PRIVATE openvino::runtime nlohmann_json::nlohmann_json)
  target_include_directories(robotaskrun PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/task/spr_vision_26/vendor/io/serial/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src/task/spr_vision_26/vendor/io/hikrobot/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src/task/spr_vision_26/vendor/io/mindvision/include
  )
  if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64|AMD64")
    target_link_directories(robotaskrun PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/src/task/spr_vision_26/vendor/io/hikrobot/lib/amd64
      ${CMAKE_CURRENT_SOURCE_DIR}/src/task/spr_vision_26/vendor/io/mindvision/lib/amd64
    )
  elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
    target_link_directories(robotaskrun PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/src/task/spr_vision_26/vendor/io/hikrobot/lib/arm64
      ${CMAKE_CURRENT_SOURCE_DIR}/src/task/spr_vision_26/vendor/io/mindvision/lib/arm64
    )
  else()
    message(FATAL_ERROR "Unsupported SPR Vision hardware architecture: ${CMAKE_SYSTEM_PROCESSOR}")
  endif()
  target_link_libraries(robotaskrun PRIVATE MvCameraControl MVSDK usb-1.0)
endif()

if(WIN32)
  target_link_libraries(robotaskrun PRIVATE ws2_32)
endif()

if(MSVC)
  target_compile_options(robotaskrun PRIVATE /W4 /permissive-)
else()
  target_compile_options(robotaskrun PRIVATE -Wall -Wextra -Wpedantic)
endif()
