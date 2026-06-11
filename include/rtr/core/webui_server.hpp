#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <thread>

#include "rtr/core/runtime.hpp"

namespace rtr {

struct WebUiServerOptions {
  int port = 8080;
  std::string static_dir;
};

class WebUiServer {
 public:
  WebUiServer(CommandQueue& commands, ParamService& params, LogService& logs,
              DebugService& debug,
              std::function<std::string()> runtime_status_provider);
  ~WebUiServer();

  bool start(const WebUiServerOptions& options, std::string* error = nullptr);
  void stop();
  bool running() const { return running_; }
  int port() const { return options_.port; }

 private:
  void serveLoop();
  void handleClient(std::intptr_t client_socket);
  std::string handleRequest(const std::string& method, const std::string& path,
                            const std::string& body, std::string* content_type,
                            int* status_code);

  CommandQueue& commands_;
  ParamService& params_;
  LogService& logs_;
  DebugService& debug_;
  std::function<std::string()> runtime_status_provider_;
  WebUiServerOptions options_;
  std::atomic<bool> running_{false};
  std::thread thread_;
  std::intptr_t server_socket_ = -1;
};

}  // namespace rtr
