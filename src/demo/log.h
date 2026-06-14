#ifndef CAT_ENGINE_DEMO_LOG_H
#define CAT_ENGINE_DEMO_LOG_H

#include <string_view>

enum class severity {
  trace,
  debug,
  info,
  warn,
  error,
  fatal
};

#ifdef NDEBUG
constexpr severity log_level = severity:info;
#else
constexpr severity log_level = severity::debug;
#endif

void log(severity severity, std::string_view message);

#endif /* CAT_ENGINE_DEMO_LOG_H */
