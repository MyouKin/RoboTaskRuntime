# SPR Vision 26 Task

这个目录把原 `spr_vision_26-main` 的主流程迁移为 RoboTaskRuntime 的任务。迁移目标不是重写算法，而是让原来的视觉流程被 Runtime 生命周期、固定 runtime/status、日志和 WebUI debug 包起来，后续再逐步把可复用能力吸收到框架或更清晰的 task 子模块中。

## 分层

- `include/tasks/spr_vision_26/`：Runtime 看到的 task 头文件，只暴露 `SprVisionTask` 和 `SprVisionFakeTestTask`。
- `src/task/spr_vision_26/spr_vision_task.cpp`：真实 Linux 机器人任务适配层，保留原 `standard_mpc` 主流程和规划线程结构。
- `src/task/spr_vision_26/spr_vision_fake_test_task.cpp`：macOS 友好的 fake 数据任务，复用原 Solver、Tracker、Aimer，验证 `solve -> track -> aim -> command`。
- `src/task/spr_vision_26/spr_vision_image_bridge.*`：把 OpenCV 图像转换为 Runtime debug 图像。
- `src/task/spr_vision_26/vendor/`：从原项目搬来的主流程依赖代码。这里优先保持原样，只做编译边界、文件结构和可读性所需的小改动。
- `configs/spr_vision_26_fake_test.yaml`：fake 流程的 Runtime 配置。
- `configs/spr_vision_26_standard4.yaml`：真实任务的 Runtime 配置，指向 `vendor/configs/spr_standard4.yaml`。

## 任务名

- `spr_vision_fake_test`：开发机测试任务。它不使用神经网络推理、相机、串口、CAN 或厂商 SDK，只用 fake 装甲板数据跑通 Solver、Tracker、Aimer，并在 WebUI 显示 overlay 和 yaw/pitch 曲线。
- `spr_vision_task`：真实机器人任务。它面向目标 Linux 机器和指定硬件，默认不在 macOS 构建。

## macOS 开发验证

安装依赖：

```bash
brew install cmake opencv eigen fmt spdlog yaml-cpp
```

构建：

```bash
cmake -B build
cmake --build build
```

命令行 smoke test：

```bash
./robotaskrun --task spr_vision_fake_test --config configs/spr_vision_26_fake_test.yaml --no-webui --max-frames 30
```

WebUI：

```bash
./robotaskrun --task spr_vision_fake_test --config configs/spr_vision_26_fake_test.yaml --webui --port 8080
```

打开 `http://127.0.0.1:8080`，重点看：

- `spr.fake.overlay`：fake 装甲板、观测框、瞄准点和控制量 overlay。
- `spr.command.yaw_deg` / `spr.command.pitch_deg`：Aimer 输出的控制曲线。
- `spr.target.*`、`spr.solved.*`、`spr.tracker.state`：debug values / curves 中的中间观测量。

## Linux 真机部署

真实任务默认关闭。部署到目标 Linux 机器后再打开：

```bash
cmake -B build -DRTR_ENABLE_SPR_VISION_26_REAL=ON
cmake --build build -j
./robotaskrun --task spr_vision_task --config configs/spr_vision_26_standard4.yaml --webui
```

真实任务需要目标机具备原项目运行环境：

- OpenCV
- Eigen3
- fmt
- spdlog
- yaml-cpp
- OpenVINO Runtime
- nlohmann_json
- libusb
- 原相机 SDK 动态库，当前随 `vendor/io/hikrobot/lib` 和 `vendor/io/mindvision/lib` 保留
- 对串口、CAN、相机设备的系统权限

macOS 上不会构建真实任务，因为它依赖 Linux 硬件链路和厂商 SDK。

## 原流程映射

真实任务适配层按原 `standard_mpc` 的职责拆到 Runtime 生命周期：

- `onInit`：声明 Runtime 参数和状态入口。
- `onStart`：读取原 YAML，创建 Gimbal、Camera、YOLO、Solver、Tracker、Planner、Buff 相关对象，并启动原规划线程。
- `onUpdate`：保持主线程图像读取、状态读取、识别、求解、跟踪和 buff 分支处理。
- `planThread`：保持原队列取 target、调用 Planner、发送云台命令的线程模型。
- `onPause` / `onResume`：只暂停 Runtime update，不销毁原对象。
- `onStop`：安全停止规划线程并释放硬件对象。

这个设计允许原项目的多线程结构继续存在。Runtime 只负责在主线程调生命周期和提供 `Rtr` 服务；task 内部线程仍由 task 自己管理。跨线程写 `rtr.debug()` / `rtr.log()` 是允许的，但不要在工作线程里直接控制 Runtime 状态机。

## Fake 流程

`spr_vision_fake_test` 的目标是让 macOS 上也能验证主算法链路：

```text
fake armor points -> Solver::solve -> Tracker::track -> Aimer::aim -> io::Command -> rtr.debug
```

它刻意不 mock YOLO、相机或串口，而是直接构造一组合理的图像装甲板角点，减少和不可运行依赖的耦合。这样可以在本地检查：

- Solver 的 PnP 和装甲板位姿输出是否可用。
- Tracker 是否能稳定进入 tracking。
- Aimer 是否能输出 yaw/pitch command。
- Runtime WebUI 是否能显示图像和曲线。

## 修改原则

为了保持一致性，优先遵守这几个边界：

- 真机运行逻辑先不要改，尤其是识别、跟踪、规划、发送命令的顺序。
- 新增 Runtime 集成能力时，优先改 `spr_vision_task.cpp` 或 `spr_vision_fake_test_task.cpp`。
- `vendor/` 内代码只在确实需要时做小改动，例如解除硬件头文件耦合、修复明确 bug、补齐缺失 include。
- 如果某段代码未来要跨 task 复用，再从 `vendor/` 慢慢吸收到 `include/rtr` 或独立 toolkit，不要一次性大重构。

## 已做的轻量修复

- 把 `ShootMode` 从硬件通信头文件中拆出，降低 fake 测试对 SocketCAN 的编译耦合。
- 让 Tracker 只依赖 detection 数据结构，避免 fake 测试被完整感知模块拖入。
- Logger 会自动创建 `logs/`，避免首次运行时文件 sink 打开失败。
- EKF 的 `last_nis` 给出默认初始化值，避免潜在未初始化读。
- Fake task 使用确定性仿真时间戳，避免 WebUI 图像编码导致 Tracker 误判超大 `dt`。
- 真实 task 析构时会兜底停止规划线程，避免异常路径没有进入 `onStop` 时留下 joinable thread。

## 当前限制

- `spr_vision_task` 的真实硬件路径已经按原流程接入 Runtime，但没有在 macOS 上编译或运行验证。
- OpenVINO、相机 SDK、串口和 CAN 的实际部署问题需要在目标 Linux 机器上继续处理。
- `vendor/` 里仍保留原项目的工具、标定和辅助代码，当前 CMake 只把主流程需要的源文件纳入构建。
