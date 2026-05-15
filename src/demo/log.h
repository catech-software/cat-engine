#ifndef CAT_ENGINE_DEMO_LOG_H
#define CAT_ENGINE_DEMO_LOG_H

#include <string_view>

enum class Severity {
  TRACE,
  DEBUG,
  INFO,
  WARN,
  ERROR,
  FATAL
};

constexpr Severity log_level = Severity::WARN;

void log(Severity severity, std::string_view message);

#endif /* CAT_ENGINE_DEMO_LOG_H */
