# RoboTaskRuntime

RoboTaskRuntime 是一个轻量级“单任务机器人算法运行框架”。它面向机器人算法开发、动态调参、运行状态观察、复杂 debug 数据展示和快速部署。它不替代 ROS2，也不规定 detector / tracker / planner / controller 等固定流程；每次进程只运行一个 `Task`，Task 内部如何组织算法、状态机和数据流完全由用户决定。

框架负责提供统一生命周期、运行控制、动态参数、日志、Runtime 固定状态、debug 数据发布、WebUI、无硬件测试能力，以及后续接入 ROS2、真实硬件和第三方可视化工具的扩展边界。

## 第三方库

- `spdlog`：推荐日志后端。若系统已安装，CMake 会自动使用；若缺失，第一版会使用一个很小的 console fallback，便于干净环境先跑通。
- `yaml-cpp`：推荐 YAML 配置解析库。若系统已安装，CMake 会自动使用；若缺失，第一版会使用仅覆盖示例配置的 YAML 子集解析器。
- `OpenCV`：可选。第一版默认 `RTR_USE_OPENCV=OFF`，debug 图像使用内置 `ImageFrame` RGB8 数据；后续需要 `cv::Mat` 边界时可打开。

这些库都成熟、轻量、跨平台，避免框架重复实现日志、配置和图像生态里的基础轮子。本项目不会自动安装系统依赖。

## 构建

macOS:

```bash
brew install cmake spdlog yaml-cpp
cmake -B build
cmake --build build
```

Linux Ubuntu/Debian:

```bash
sudo apt update
sudo apt install cmake g++ libspdlog-dev libyaml-cpp-dev
cmake -B build
cmake --build build
```

Windows:

```powershell
vcpkg install spdlog yaml-cpp
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=C:/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release
```

无第三方库也可先尝试：

```bash
cmake -B build
cmake --build build
```

可选项：

```bash
cmake -B build -DRTR_ENABLE_WEBUI=ON -DRTR_USE_OPENCV=OFF
cmake -B build -DRTR_REQUIRE_SPDLOG=ON -DRTR_REQUIRE_YAML_CPP=ON
```

## 运行

Linux / macOS:

```bash
./robotaskrun --task minimal --config configs/minimal_task.yaml --webui
./robotaskrun --task spr_vision_fake_test --config configs/spr_vision_26_fake_test.yaml --webui
```

Windows:

```powershell
.\robotaskrun.exe --task minimal --config configs\minimal_task.yaml --webui
.\robotaskrun.exe --task spr_vision_fake_test --config configs\spr_vision_26_fake_test.yaml --webui
```

WebUI 默认读取配置里的端口，示例为 `http://127.0.0.1:8080`。不启用 WebUI 时：

```bash
./robotaskrun --task spr_vision_fake_test --config configs/spr_vision_26_fake_test.yaml --no-webui
```

框架仍会运行 Task、参数和日志；`rtr.debug()` 全局关闭，`enabled(key)` 返回 false，scalar/int/bool/text/image 写入都会直接 no-op，避免 WebUI debug 路径占用运行资源。

## 模块职责

- `include/rtr/core/runtime.hpp`：Runtime 状态机、命令队列、性能统计和 Task-facing `Rtr` 门面。
- `include/rtr/core/task.hpp`：Task 生命周期接口。
- `include/rtr/core/param.hpp`：动态参数声明、读取、修改和配置加载。
- `include/rtr/core/log.hpp`：日志服务，封装 spdlog/fallback，并维护最近日志 ring buffer。
- `include/rtr/core/debug.hpp`：用户 Task debug 数据服务，支持 latest value、scalar/int 历史曲线、文本、bool 和限频 latest image。
- `include/rtr/core/data.hpp`：`TimeStamp`、`Header`、`Stamped<T>`、`ImageFrame` 等轻量数据工具。
- `include/rtr/core/webui_server.hpp`：WebUI 服务接口，WebUI 通过 `CommandQueue` 控制 Runtime。
- `include/rtr/io/`：`DataSource`、`Sink`、`SimCameraSource`、`DummySink`。
- `include/rtr/toolkit/`：可复用算法工具，第一版包含 PID 和 EKF 骨架。
- `src/tasks/minimal_task/`：最小生命周期测试任务。
- `include/tasks/spr_vision_26/`、`src/task/spr_vision_26/`：从 `spr_vision_26-main` 迁移来的 SPR 视觉任务。fake 测试任务默认构建，真实 Linux 硬件任务通过 CMake 选项启用。
- `src/webui/`：WebUI 静态文件。

## MinimalTask

`MinimalTask` 覆盖 `onInit`、`onStart`、`onUpdate`、`onPause`、`onResume`、`onStop`，声明少量参数，周期性更新 `rtr.debug()` 值并写生命周期日志。它不依赖硬件，也不依赖复杂 debug 图像。

## SPR Vision 26 Task

SPR 视觉任务的迁移说明在 [src/task/spr_vision_26/README.md](/Users/myoukin/Codes/RoboTaskRuntime/src/task/spr_vision_26/README.md)。

macOS 上可先运行 fake 主流程测试：

```bash
./robotaskrun --task spr_vision_fake_test --config configs/spr_vision_26_fake_test.yaml --webui
```

部署到目标 Linux 机器后，再打开真实任务：

```bash
cmake -B build -DRTR_ENABLE_SPR_VISION_26_REAL=ON
cmake --build build -j
./robotaskrun --task spr_vision_task --config configs/spr_vision_26_standard4.yaml --webui
```

## 新增 Task

1. 新建 `src/tasks/my_task/my_task.hpp/.cpp`，继承 `rtr::Task`。
2. 在 `onInit(Rtr& rtr)` 中声明参数、初始化数据源/输出端/算法对象。
3. 在 `onUpdate(Rtr& rtr)` 中读取参数，在安全点更新内部算法对象。
4. 在 `src/main.cpp` 的 `createTask()` 中注册任务名。
5. 新增对应 YAML 配置。

Task 只接触 `Rtr& rtr`，不要直接控制 Runtime 状态机，也不要直接访问 WebUI。

## 新增 DataSource

实现 `rtr::DataSource`：

```cpp
class MySource : public rtr::DataSource {
 public:
  bool configure(rtr::Rtr& rtr, const std::string& prefix) override;
  bool open() override;
  bool start() override;
  void stop() override;
  void close() override;
};
```

真实相机、视频文件、IMU、ROS topic 都应作为 `DataSource` 扩展。硬件 SDK 头文件不要进入 core。

## 新增 Sink

实现 `rtr::Sink<T>`，用于串口、CAN、ROS topic、文件或 dummy 输出。真实设备相关实现应放在后续可选模块中，不污染 core。

## 后续扩展

- OpenCV：打开 `-DRTR_USE_OPENCV=ON` 后，可给 `rtr.debug().image(key, cv::Mat)` 增加更完整的编码和 overlay 能力。
- ROS2：新增 `RosImageSource`、`RosTopicSink`、Bridge 模块即可，Task 仍通过 `DataSource`、`Sink` 和 `Rtr` 服务交互。
- 真实硬件：新增 `OpenCVCameraSource`、`HikCameraSource`、`SerialSink`、`CanSink` 等可选模块。不要在 core 中包含相机 SDK、串口、CAN 或平台专用头文件。
- WebUI：当前是轻量本地 HTTP API。后续可替换为 WebSocket、SSE 或独立前端服务，Runtime 控制仍通过 `CommandQueue`。
