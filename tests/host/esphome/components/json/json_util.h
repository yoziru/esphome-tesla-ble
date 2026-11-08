#pragma once
#include <map>
#include <sstream>
#include <string>
struct JsonObject {
  std::map<std::string, std::string> *values;
  struct Field {
    std::string &value;
    template<typename T> void operator=(const T &v) { std::ostringstream s; s << v; value = s.str(); }
    void operator=(const std::string &v) { value = "\"" + v + "\""; }
    void operator=(const char *v) { value = "\"" + std::string(v) + "\""; }
  };
  Field operator[](const std::string &key) { return {(*values)[key]}; }
};
namespace esphome::json {
  template<typename F> std::string build_json(F fn) {
    std::map<std::string, std::string> values;
    fn(JsonObject{&values}); std::string result = "{";
    for (const auto &[key, value] : values) {
      if (result.size() > 1) result += ",";
      result += "\""+key+"\":"+value;
    }
    return result+"}";
  }
}
