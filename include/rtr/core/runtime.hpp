#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <string>

#include "rtr/core/debug.hpp"
#include "rtr/core/log.hpp"
#include "rtr/core/param.hpp"
#include "rtr/core/task.hpp"

namespace rtr {

class WebUiServer;

enum class RuntimeState {
  Idle,
  Starting,
  Running,
  Paused,
  Stopping,
  Stopped,
  Error,
};

enum class RuntimeCommandType {
  Run,
  Pause,
  Resume,
  Stop,
  Shutdown,
};

struct RuntimeCommand {
  RuntimeCommandType type = RuntimeCommandType::Run;
  std::string source = "local";
};

class CommandQueue {
 public:
  void push(RuntimeCommand command);
  bool tryPop(RuntimeCommand& command);

 private:
  std::mutex mutex_;
  std::queue<RuntimeCommand> queue_;
};

class Rtr {
 public:
  Rtr(ParamService& param, LogService& log, DebugService& debug)
      : param_(param), log_(log), debug_(debug) {}

  ParamService& param() { return param_; }
  LogService& log() { return log_; }
  DebugService& debug() { return debug_; }

 private:
  ParamService& param_;
  LogService& log_;
  DebugService& debug_;
};

struct RuntimeOptions {
  std::string task_name = "minimal";
  std::string config_path;
  bool webui_enabled = false;
  bool webui_flag_set = false;
  int webui_port = 8080;
  bool webui_port_flag_set = false;
  double update_rate_hz = 60.0;
  bool update_rate_flag_set = false;
  int64_t max_frames = -1;
  std::string webui_static_dir;
};

struct PerfStats {
  double loop_hz = 0.0;
  double update_ms = 0.0;
  int64_t frame_index = 0;
};

class Runtime {
 public:
  Runtime();
  ~Runtime();

  int run(std::unique_ptr<Task> task, const RuntimeOptions& options);
  void requestShutdown();

  RuntimeState state() const;
  std::string stateString() const;

  CommandQueue& commands() { return commands_; }
  ParamService& param() { return param_; }
  LogService& log() { return log_; }
  DebugService& debug() { return debug_; }

  static std::string stateToString(RuntimeState state);
  static std::string commandToString(RuntimeCommandType command);

 private:
  bool initTask();
  bool startTask();
  void stopTask();
  void handleCommands();
  void applyCommand(const RuntimeCommand& command);
  void setState(RuntimeState state);
  std::string runtimeStatusJson() const;

  mutable std::mutex state_mutex_;
  mutable std::mutex perf_mutex_;
  RuntimeState state_ = RuntimeState::Idle;
  std::atomic<bool> shutdown_requested_{false};
  bool initialized_ = false;
  bool started_once_ = false;

  RuntimeOptions options_;
  std::unique_ptr<Task> task_;
  ParamService param_;
  LogService log_;
  DebugService debug_;
  Rtr rtr_;
  CommandQueue commands_;
  std::unique_ptr<WebUiServer> webui_;
  PerfStats perf_;
};

RuntimeOptions parseRuntimeOptions(int argc, char** argv);
void printUsage();

}  // namespace rtr
