#include "demo/log.h"

#include <iostream>
#include <string>
#include <string_view>

void log(Severity severity, std::string_view message) {
  std::string severity_str;
  switch (severity) {
  case Severity::TRACE:
    severity_str = "TRACE";
    break;
  case Severity::DEBUG:
    severity_str = "DEBUG";
    break;
  case Severity::INFO:
    severity_str = "INFO";
    break;
  case Severity::WARN:
    severity_str = "WARN";
    break;
  case Severity::ERROR:
    severity_str = "ERROR";
    break;
  case Severity::FATAL:
    severity_str = "FATAL";
    break;
  }
  std::println(std::cerr, "[{}] {}", severity_str, message);
}
