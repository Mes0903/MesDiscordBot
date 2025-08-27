
#pragma once

#include <dpp/dpp.h>

#include <chrono>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <variant>

namespace terry::bot {

using user_id = dpp::snowflake;
using guild_id = dpp::snowflake;
using timestamp = std::chrono::sys_seconds;

struct error {
	std::string message;
};
using ok_t = std::monostate;

} // namespace terry::bot
