/**
 * @brief
 * Shared type aliases, enums, and small helpers used across the bot.
 * Kept minimal to avoid heavy header dependencies.
 */

#pragma once

#include <dpp/dpp.h>

#include <chrono>
#include <expected>
#include <string>
#include <string_view>
#include <variant>

namespace terry::bot {

/** @brief Discord snowflake ID wrapper (strong typedef). */
using user_id = dpp::snowflake;
using guild_id = dpp::snowflake;
using timestamp = std::chrono::sys_seconds;

/** @brief Error wrapper used in std::expected results. */
struct error {
	std::string message;
};

using ok_t = std::monostate;

} // namespace terry::bot
