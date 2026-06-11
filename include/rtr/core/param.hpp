#pragma once

#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace rtr {

enum class ParamType {
  Double,
  Int,
  Bool,
  String,
};

using ParamValue = std::variant<double, int64_t, bool, std::string>;

struct ParamInfo {
  std::string key;
  ParamType type = ParamType::String;
  ParamValue value;
  ParamValue default_value;
  std::optional<ParamValue> min_value;
  std::optional<ParamValue> max_value;
  std::string description;
  bool runtime_mutable = true;
};

class ParamService {
 public:
  bool loadFile(const std::string& path, std::string* error = nullptr);

  void declareDouble(const std::string& key, double default_value,
                     double min_value, double max_value,
                     const std::string& description,
                     bool runtime_mutable = true);
  void declareInt(const std::string& key, int64_t default_value,
                  int64_t min_value, int64_t max_value,
                  const std::string& description,
                  bool runtime_mutable = true);
  void declareBool(const std::string& key, bool default_value,
                   const std::string& description,
                   bool runtime_mutable = true);
  void declareString(const std::string& key, const std::string& default_value,
                     const std::string& description,
                     bool runtime_mutable = true);

  double getDouble(const std::string& key, double fallback = 0.0) const;
  int64_t getInt(const std::string& key, int64_t fallback = 0) const;
  bool getBool(const std::string& key, bool fallback = false) const;
  std::string getString(const std::string& key,
                        const std::string& fallback = "") const;

  bool setFromString(const std::string& key, const std::string& value,
                     std::string* error = nullptr);
  bool setDouble(const std::string& key, double value,
                 std::string* error = nullptr);
  bool setInt(const std::string& key, int64_t value,
              std::string* error = nullptr);
  bool setBool(const std::string& key, bool value,
               std::string* error = nullptr);
  bool setString(const std::string& key, const std::string& value,
                 std::string* error = nullptr);

  std::vector<ParamInfo> list() const;
  std::optional<ParamInfo> getInfo(const std::string& key) const;

  static std::string typeName(ParamType type);
  static std::string valueToString(const ParamValue& value);

 private:
  void declareParam(const ParamInfo& info);
  bool setValueLocked(ParamInfo& info, const ParamValue& value,
                      bool respect_mutability, std::string* error);
  ParamValue parseValue(ParamType type, const std::string& text,
                        std::string* error) const;
  void rememberConfigValue(const std::string& key, const std::string& value);

  mutable std::mutex mutex_;
  std::map<std::string, ParamInfo> params_;
  std::map<std::string, std::string> pending_config_;
};

}  // namespace rtr

