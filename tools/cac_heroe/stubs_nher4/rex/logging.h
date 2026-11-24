#pragma once
#include <string>
#include <vector>

namespace rex::test_log {
inline std::vector<std::string>& Messages() {
  static std::vector<std::string> v;
  return v;
}
inline void Record(const char* level, const char* fmt) {
  Messages().push_back(std::string(level) + ": " + fmt);
}
}  // namespace rex::test_log

#define REXLOG_INFO(fmt, ...) ::rex::test_log::Record("INFO", fmt)
#define REXLOG_WARN(fmt, ...) ::rex::test_log::Record("WARN", fmt)
#define REXLOG_ERROR(fmt, ...) ::rex::test_log::Record("ERROR", fmt)
#define REXLOG_DEBUG(fmt, ...) ::rex::test_log::Record("DEBUG", fmt)
