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

enum class team_strategy {
	tiered_random, // default: bucketed random by tiers
	snake_draft,
	greedy_spread, // your current greedy
	local_search	 // refine any initial assignment
};

struct form_options {
	team_strategy strategy = team_strategy::tiered_random;
	std::optional<uint64_t> seed{};
	int max_iters = 1500;			// for local_search
	bool equal_sizes = false; // 若你將來想限制隊伍同人數，可用得到
};

} // namespace terry::bot
