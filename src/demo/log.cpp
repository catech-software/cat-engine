#include "demo/log.h"

#include <chrono>
#include <iostream>
#include <spanstream>
#include <string>
#include <string_view>

void log(severity severity, std::string_view message) {
  if (severity < log_level) return;

  std::ispanstream iss = std::ispanstream(message);
  std::string line;
  if (!std::getline(iss, line)) return;

  std::chrono::sys_time now = std::chrono::system_clock::now();
  std::chrono::local_time time = std::chrono::floor<std::chrono::seconds>(std::chrono::current_zone()->to_local(now));

  std::string severity_str;
  switch (severity) {
  case severity::trace:
    severity_str = "[\o{033}[1;30mTRACE\o{033}[0m]";
    break;
  case severity::debug:
    severity_str = "[\o{033}[1;37mDEBUG\o{033}[0m]";
    break;
  case severity::info:
    severity_str = "[\o{033}[1;36mINFO\o{033}[0m] ";
    break;
  case severity::warn:
    severity_str = "[\o{033}[1;33mWARN\o{033}[0m] ";
    break;
  case severity::error:
    severity_str = "[\o{033}[1;35mERROR\o{033}[0m]";
    break;
  case severity::fatal:
    severity_str = "[\o{033}[1;31mFATAL\o{033}[0m]";
    break;
  }

  std::println(std::cerr, "{:%T} {} {}", time, severity_str, line);
  while (std::getline(iss, line)) {
    std::println(std::cerr, "                 {}", line);
  }
}
