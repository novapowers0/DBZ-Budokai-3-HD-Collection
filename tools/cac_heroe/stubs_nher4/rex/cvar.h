#pragma once
#include <string>
#include <unordered_map>

namespace rex::cvar {

inline std::unordered_map<std::string, bool*>& BoolRegistry() {
  static std::unordered_map<std::string, bool*> reg;
  return reg;
}

inline std::unordered_map<std::string, std::string>& ExtraFlags() {
  static std::unordered_map<std::string, std::string> m;
  return m;
}

struct BoolRegHelper {
  BoolRegHelper(const char* name, bool* ptr) { BoolRegistry()[name] = ptr; }
};

inline std::string GetFlagByName(const std::string& name) {
  if (auto it = BoolRegistry().find(name); it != BoolRegistry().end())
    return *(it->second) ? "true" : "false";
  if (auto it = ExtraFlags().find(name); it != ExtraFlags().end()) return it->second;
  return "";
}

inline void SetBoolFlag(const std::string& name, bool v) {
  if (auto it = BoolRegistry().find(name); it != BoolRegistry().end()) {
    *(it->second) = v;
  } else {
    ExtraFlags()[name] = v ? "true" : "false";
  }
}

}  // namespace rex::cvar

#define REXCVAR_DEFINE_BOOL(name, def, cat, desc) \
  bool g_rexcvar_##name = (def);                  \
  static ::rex::cvar::BoolRegHelper g_rexcvar_reg_##name(#name, &g_rexcvar_##name)

#define REXCVAR_DECLARE(type, name) extern type g_rexcvar_##name
#define REXCVAR_GET(name) (g_rexcvar_##name)
#define REXCVAR_SET(name, val) (g_rexcvar_##name = (val))
