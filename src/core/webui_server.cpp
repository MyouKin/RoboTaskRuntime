#include "rtr/core/webui_server.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace {

#ifdef _WIN32
using SocketHandle = SOCKET;
constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
SocketHandle toSocket(std::intptr_t value) {
  return static_cast<SocketHandle>(value);
}
std::intptr_t fromSocket(SocketHandle value) {
  return static_cast<std::intptr_t>(value);
}
void closeSocket(SocketHandle socket) {
  if (socket != kInvalidSocket) {
    closesocket(socket);
  }
}
#else
using SocketHandle = int;
constexpr SocketHandle kInvalidSocket = -1;
SocketHandle toSocket(std::intptr_t value) {
  return static_cast<SocketHandle>(value);
}
std::intptr_t fromSocket(SocketHandle value) {
  return static_cast<std::intptr_t>(value);
}
void closeSocket(SocketHandle socket) {
  if (socket >= 0) {
    close(socket);
  }
}
#endif

std::string trimCopy(const std::string& input) {
  const auto begin = std::find_if_not(input.begin(), input.end(), [](int ch) {
    return std::isspace(ch) != 0;
  });
  const auto end = std::find_if_not(input.rbegin(), input.rend(), [](int ch) {
                     return std::isspace(ch) != 0;
                   }).base();
  if (begin >= end) {
    return "";
  }
  return std::string(begin, end);
}

std::string lowerCopy(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return value;
}

std::string jsonEscape(const std::string& value) {
  std::ostringstream ss;
  for (const char c : value) {
    switch (c) {
      case '"':
        ss << "\\\"";
        break;
      case '\\':
        ss << "\\\\";
        break;
      case '\n':
        ss << "\\n";
        break;
      case '\r':
        ss << "\\r";
        break;
      case '\t':
        ss << "\\t";
        break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          ss << "\\u" << std::hex << std::setw(4) << std::setfill('0')
             << static_cast<int>(static_cast<unsigned char>(c));
        } else {
          ss << c;
        }
        break;
    }
  }
  return ss.str();
}

std::string jsonQuoted(const std::string& value) {
  return "\"" + jsonEscape(value) + "\"";
}

std::string paramValueJson(const rtr::ParamValue& value) {
  if (std::holds_alternative<double>(value)) {
    std::ostringstream ss;
    ss << std::setprecision(8) << std::get<double>(value);
    return ss.str();
  }
  if (std::holds_alternative<int64_t>(value)) {
    return std::to_string(std::get<int64_t>(value));
  }
  if (std::holds_alternative<bool>(value)) {
    return std::get<bool>(value) ? "true" : "false";
  }
  return jsonQuoted(std::get<std::string>(value));
}

std::string debugValueTypeName(rtr::DebugValueType type) {
  switch (type) {
    case rtr::DebugValueType::Scalar:
      return "scalar";
    case rtr::DebugValueType::Int:
      return "int";
    case rtr::DebugValueType::Bool:
      return "bool";
    case rtr::DebugValueType::Text:
      return "text";
  }
  return "unknown";
}

std::string debugValueJson(const rtr::DebugVariant& value) {
  if (std::holds_alternative<double>(value)) {
    std::ostringstream ss;
    ss << std::setprecision(8) << std::get<double>(value);
    return ss.str();
  }
  if (std::holds_alternative<int64_t>(value)) {
    return std::to_string(std::get<int64_t>(value));
  }
  if (std::holds_alternative<bool>(value)) {
    return std::get<bool>(value) ? "true" : "false";
  }
  return jsonQuoted(std::get<std::string>(value));
}

std::string jsonExtract(const std::string& body, const std::string& key) {
  const std::string pattern = "\"" + key + "\"";
  const size_t key_pos = body.find(pattern);
  if (key_pos == std::string::npos) {
    return "";
  }
  size_t colon = body.find(':', key_pos + pattern.size());
  if (colon == std::string::npos) {
    return "";
  }
  ++colon;
  while (colon < body.size() &&
         std::isspace(static_cast<unsigned char>(body[colon])) != 0) {
    ++colon;
  }
  if (colon >= body.size()) {
    return "";
  }
  if (body[colon] == '"') {
    std::string out;
    bool escaped = false;
    for (size_t i = colon + 1; i < body.size(); ++i) {
      const char c = body[i];
      if (escaped) {
        out.push_back(c);
        escaped = false;
      } else if (c == '\\') {
        escaped = true;
      } else if (c == '"') {
        return out;
      } else {
        out.push_back(c);
      }
    }
    return out;
  }

  size_t end = colon;
  while (end < body.size() && body[end] != ',' && body[end] != '}') {
    ++end;
  }
  return trimCopy(body.substr(colon, end - colon));
}

std::string urlDecode(const std::string& input) {
  std::string out;
  out.reserve(input.size());
  for (size_t i = 0; i < input.size(); ++i) {
    if (input[i] == '%' && i + 2 < input.size()) {
      const std::string hex = input.substr(i + 1, 2);
      char* end = nullptr;
      const long value = std::strtol(hex.c_str(), &end, 16);
      if (end && *end == '\0') {
        out.push_back(static_cast<char>(value));
        i += 2;
      }
    } else if (input[i] == '+') {
      out.push_back(' ');
    } else {
      out.push_back(input[i]);
    }
  }
  return out;
}

bool sendAll(SocketHandle socket, const std::string& data) {
  size_t sent = 0;
  while (sent < data.size()) {
    const int chunk =
        send(socket, data.data() + sent, static_cast<int>(data.size() - sent), 0);
    if (chunk <= 0) {
      return false;
    }
    sent += static_cast<size_t>(chunk);
  }
  return true;
}

std::string readFile(const std::string& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return "";
  }
  std::ostringstream ss;
  ss << input.rdbuf();
  return ss.str();
}

std::string staticFilePath(const std::string& static_dir,
                           const std::string& path) {
  if (path == "/" || path == "/index.html") {
    return static_dir + "/index.html";
  }
  if (path == "/app.js") {
    return static_dir + "/app.js";
  }
  if (path == "/style.css") {
    return static_dir + "/style.css";
  }
  return "";
}

std::string statusReason(int status) {
  switch (status) {
    case 200:
      return "OK";
    case 400:
      return "Bad Request";
    case 404:
      return "Not Found";
    case 405:
      return "Method Not Allowed";
    case 500:
      return "Internal Server Error";
    default:
      return "OK";
  }
}

void pushCommand(rtr::CommandQueue& commands, rtr::RuntimeCommandType type,
                 const std::string& source) {
  commands.push({type, source});
}

std::string makeParamsJson(const std::vector<rtr::ParamInfo>& params) {
  std::ostringstream ss;
  ss << "{\"params\":[";
  for (size_t i = 0; i < params.size(); ++i) {
    const auto& p = params[i];
    if (i != 0) {
      ss << ",";
    }
    ss << "{";
    ss << "\"key\":" << jsonQuoted(p.key) << ",";
    ss << "\"type\":" << jsonQuoted(rtr::ParamService::typeName(p.type)) << ",";
    ss << "\"value\":" << paramValueJson(p.value) << ",";
    ss << "\"default\":" << paramValueJson(p.default_value) << ",";
    ss << "\"min\":"
       << (p.min_value ? paramValueJson(*p.min_value) : std::string("null"))
       << ",";
    ss << "\"max\":"
       << (p.max_value ? paramValueJson(*p.max_value) : std::string("null"))
       << ",";
    ss << "\"description\":" << jsonQuoted(p.description) << ",";
    ss << "\"mutable\":" << (p.runtime_mutable ? "true" : "false");
    ss << "}";
  }
  ss << "]}";
  return ss.str();
}

std::string makeLogsJson(const std::vector<rtr::LogEntry>& logs) {
  std::ostringstream ss;
  ss << "{\"logs\":[";
  for (size_t i = 0; i < logs.size(); ++i) {
    if (i != 0) {
      ss << ",";
    }
    ss << "{";
    ss << "\"timestamp_ms\":" << logs[i].timestamp_ms << ",";
    ss << "\"level\":" << jsonQuoted(logs[i].level) << ",";
    ss << "\"message\":" << jsonQuoted(logs[i].message);
    ss << "}";
  }
  ss << "]}";
  return ss.str();
}

std::string makeDebugListJson(const rtr::DebugList& list,
                              const std::map<std::string,
                                             std::vector<rtr::HistoryPoint>>&
                                  histories) {
  auto arrayOfStrings = [](const std::vector<std::string>& values) {
    std::ostringstream ss;
    ss << "[";
    for (size_t i = 0; i < values.size(); ++i) {
      if (i != 0) {
        ss << ",";
      }
      ss << jsonQuoted(values[i]);
    }
    ss << "]";
    return ss.str();
  };

  std::ostringstream ss;
  ss << "{";
  ss << "\"images\":" << arrayOfStrings(list.images) << ",";
  ss << "\"scalars\":" << arrayOfStrings(list.scalars) << ",";
  ss << "\"integers\":" << arrayOfStrings(list.integers) << ",";
  ss << "\"booleans\":" << arrayOfStrings(list.booleans) << ",";
  ss << "\"texts\":" << arrayOfStrings(list.texts) << ",";
  ss << "\"values\":[";
  for (size_t i = 0; i < list.values.size(); ++i) {
    const auto& entry = list.values[i];
    if (i != 0) {
      ss << ",";
    }
    ss << "{";
    ss << "\"key\":" << jsonQuoted(entry.key) << ",";
    ss << "\"type\":" << jsonQuoted(debugValueTypeName(entry.type)) << ",";
    ss << "\"value\":" << debugValueJson(entry.value) << ",";
    ss << "\"timestamp_ms\":" << entry.timestamp_ms;
    ss << "}";
  }
  ss << "],";
  ss << "\"history\":{";
  size_t key_index = 0;
  for (const auto& item : histories) {
    if (key_index++ != 0) {
      ss << ",";
    }
    ss << jsonQuoted(item.first) << ":[";
    for (size_t i = 0; i < item.second.size(); ++i) {
      if (i != 0) {
        ss << ",";
      }
      ss << "[" << item.second[i].timestamp_ms << ","
         << std::setprecision(8) << item.second[i].value << "]";
    }
    ss << "]";
  }
  ss << "}}";
  return ss.str();
}

void putLe16(std::string& out, size_t offset, uint16_t value) {
  out[offset] = static_cast<char>(value & 0xff);
  out[offset + 1] = static_cast<char>((value >> 8) & 0xff);
}

void putLe32(std::string& out, size_t offset, uint32_t value) {
  out[offset] = static_cast<char>(value & 0xff);
  out[offset + 1] = static_cast<char>((value >> 8) & 0xff);
  out[offset + 2] = static_cast<char>((value >> 16) & 0xff);
  out[offset + 3] = static_cast<char>((value >> 24) & 0xff);
}

std::string encodeBmp(const rtr::ImageFrame& image) {
  if (!image.valid() || image.channels < 3) {
    return "";
  }
  const int row_bytes = image.width * 3;
  const int padded_row = (row_bytes + 3) & ~3;
  const int pixel_bytes = padded_row * image.height;
  const int file_bytes = 54 + pixel_bytes;
  std::string out(static_cast<size_t>(file_bytes), '\0');

  out[0] = 'B';
  out[1] = 'M';
  putLe32(out, 2, static_cast<uint32_t>(file_bytes));
  putLe32(out, 10, 54);
  putLe32(out, 14, 40);
  putLe32(out, 18, static_cast<uint32_t>(image.width));
  putLe32(out, 22, static_cast<uint32_t>(image.height));
  putLe16(out, 26, 1);
  putLe16(out, 28, 24);
  putLe32(out, 34, static_cast<uint32_t>(pixel_bytes));

  for (int y = 0; y < image.height; ++y) {
    const int src_y = image.height - 1 - y;
    const size_t dst_row = static_cast<size_t>(54 + y * padded_row);
    for (int x = 0; x < image.width; ++x) {
      const uint8_t* src = image.pixel(x, src_y);
      const size_t dst = dst_row + static_cast<size_t>(x * 3);
      out[dst + 0] = static_cast<char>(src[2]);
      out[dst + 1] = static_cast<char>(src[1]);
      out[dst + 2] = static_cast<char>(src[0]);
    }
  }
  return out;
}

}  // namespace

namespace rtr {

WebUiServer::WebUiServer(
    CommandQueue& commands, ParamService& params, LogService& logs,
    DebugService& debug,
    std::function<std::string()> runtime_status_provider)
    : commands_(commands),
      params_(params),
      logs_(logs),
      debug_(debug),
      runtime_status_provider_(std::move(runtime_status_provider)) {}

WebUiServer::~WebUiServer() {
  stop();
}

bool WebUiServer::start(const WebUiServerOptions& options, std::string* error) {
  if (running_) {
    return true;
  }

#ifdef _WIN32
  WSADATA data;
  if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
    if (error) {
      *error = "WSAStartup failed";
    }
    return false;
  }
#endif

  const SocketHandle server = socket(AF_INET, SOCK_STREAM, 0);
  if (server == kInvalidSocket) {
    if (error) {
      *error = "socket() failed";
    }
    return false;
  }

  int yes = 1;
  setsockopt(server, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<char*>(&yes),
             sizeof(yes));

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_ANY);
  address.sin_port = htons(static_cast<uint16_t>(options.port));

  if (bind(server, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
    closeSocket(server);
    if (error) {
      *error = "bind() failed on port " + std::to_string(options.port);
    }
    return false;
  }

  if (listen(server, 16) < 0) {
    closeSocket(server);
    if (error) {
      *error = "listen() failed";
    }
    return false;
  }

  options_ = options;
  server_socket_ = fromSocket(server);
  running_ = true;
  thread_ = std::thread(&WebUiServer::serveLoop, this);
  return true;
}

void WebUiServer::stop() {
  if (!running_) {
    return;
  }
  running_ = false;
  closeSocket(toSocket(server_socket_));
  server_socket_ = -1;
  if (thread_.joinable()) {
    thread_.join();
  }
#ifdef _WIN32
  WSACleanup();
#endif
}

void WebUiServer::serveLoop() {
  while (running_) {
    sockaddr_in client_addr{};
#ifdef _WIN32
    int len = sizeof(client_addr);
#else
    socklen_t len = sizeof(client_addr);
#endif
    const SocketHandle client =
        accept(toSocket(server_socket_), reinterpret_cast<sockaddr*>(&client_addr),
               &len);
    if (client == kInvalidSocket) {
      if (running_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
      }
      continue;
    }
    handleClient(fromSocket(client));
    closeSocket(client);
  }
}

void WebUiServer::handleClient(std::intptr_t client_socket) {
  const SocketHandle socket = toSocket(client_socket);
  std::string request;
  std::array<char, 4096> buffer{};
  int content_length = 0;
  size_t header_end = std::string::npos;

  while (request.size() < 1024 * 1024) {
    const int received =
        recv(socket, buffer.data(), static_cast<int>(buffer.size()), 0);
    if (received <= 0) {
      return;
    }
    request.append(buffer.data(), static_cast<size_t>(received));
    header_end = request.find("\r\n\r\n");
    if (header_end != std::string::npos) {
      std::istringstream headers(request.substr(0, header_end));
      std::string line;
      std::getline(headers, line);
      while (std::getline(headers, line)) {
        if (!line.empty() && line.back() == '\r') {
          line.pop_back();
        }
        const size_t colon = line.find(':');
        if (colon == std::string::npos) {
          continue;
        }
        const std::string key = lowerCopy(trimCopy(line.substr(0, colon)));
        const std::string value = trimCopy(line.substr(colon + 1));
        if (key == "content-length") {
          content_length = std::stoi(value);
        }
      }
      if (request.size() >= header_end + 4 + static_cast<size_t>(content_length)) {
        break;
      }
    }
  }

  if (header_end == std::string::npos) {
    return;
  }

  std::istringstream first_line(request.substr(0, header_end));
  std::string method;
  std::string path;
  std::string version;
  first_line >> method >> path >> version;

  const std::string body =
      request.substr(header_end + 4, static_cast<size_t>(content_length));
  std::string content_type = "application/json";
  int status_code = 200;
  std::string response_body =
      handleRequest(method, path, body, &content_type, &status_code);

  std::ostringstream response;
  response << "HTTP/1.1 " << status_code << " " << statusReason(status_code)
           << "\r\n";
  response << "Content-Type: " << content_type << "\r\n";
  response << "Content-Length: " << response_body.size() << "\r\n";
  response << "Connection: close\r\n";
  response << "Cache-Control: no-store\r\n";
  response << "\r\n";
  response << response_body;
  sendAll(socket, response.str());
}

std::string WebUiServer::handleRequest(const std::string& method,
                                       const std::string& path,
                                       const std::string& body,
                                       std::string* content_type,
                                       int* status_code) {
  if (method == "GET") {
    const size_t query = path.find('?');
    const std::string clean_path =
        query == std::string::npos ? path : path.substr(0, query);

    if (clean_path == "/api/runtime/status") {
      return runtime_status_provider_();
    }
    if (clean_path == "/api/params") {
      return makeParamsJson(params_.list());
    }
    if (clean_path == "/api/logs") {
      return makeLogsJson(logs_.recent());
    }
    if (clean_path == "/api/debug/list") {
      return makeDebugListJson(debug_.list(), debug_.histories());
    }

    const std::string image_prefix = "/api/debug/image/";
    if (clean_path.rfind(image_prefix, 0) == 0) {
      const std::string key = urlDecode(clean_path.substr(image_prefix.size()));
      const auto image = debug_.latestImage(key);
      if (!image) {
        *status_code = 404;
        return "{\"ok\":false,\"error\":\"image not available\"}";
      }
      std::string bmp = encodeBmp(image->image);
      if (bmp.empty()) {
        *status_code = 404;
        return "{\"ok\":false,\"error\":\"image encoding failed\"}";
      }
      *content_type = "image/bmp";
      return bmp;
    }

    const std::string file_path = staticFilePath(options_.static_dir, clean_path);
    if (!file_path.empty()) {
      std::string file = readFile(file_path);
      if (file.empty()) {
        *status_code = 404;
        return "not found";
      }
      if (clean_path == "/app.js") {
        *content_type = "application/javascript";
      } else if (clean_path == "/style.css") {
        *content_type = "text/css";
      } else {
        *content_type = "text/html";
      }
      return file;
    }

    *status_code = 404;
    return "{\"ok\":false,\"error\":\"not found\"}";
  }

  if (method == "POST") {
    if (path == "/api/run") {
      pushCommand(commands_, RuntimeCommandType::Run, "webui");
      return "{\"ok\":true}";
    }
    if (path == "/api/pause") {
      pushCommand(commands_, RuntimeCommandType::Pause, "webui");
      return "{\"ok\":true}";
    }
    if (path == "/api/resume") {
      pushCommand(commands_, RuntimeCommandType::Resume, "webui");
      return "{\"ok\":true}";
    }
    if (path == "/api/stop") {
      pushCommand(commands_, RuntimeCommandType::Stop, "webui");
      return "{\"ok\":true}";
    }
    if (path == "/api/shutdown") {
      pushCommand(commands_, RuntimeCommandType::Shutdown, "webui");
      return "{\"ok\":true}";
    }
    if (path == "/api/params/set") {
      const std::string key = jsonExtract(body, "key");
      const std::string value = jsonExtract(body, "value");
      if (key.empty()) {
        *status_code = 400;
        return "{\"ok\":false,\"error\":\"missing key\"}";
      }
      std::string error;
      if (!params_.setFromString(key, value, &error)) {
        *status_code = 400;
        return "{\"ok\":false,\"error\":" + jsonQuoted(error) + "}";
      }
      return "{\"ok\":true}";
    }
  }

  *status_code = 405;
  return "{\"ok\":false,\"error\":\"method not allowed\"}";
}

}  // namespace rtr
