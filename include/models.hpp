/**
 * @brief
 *  Contains lightweight structs and helpers:
 *    - user          : registered player with combat power and W/L stats
 *    - team          : collection of users with cached total power
 *    - match_record  : a persisted game with timestamp, teams, and winners
 *  Also exposes JSON (de)serialization and small utilities.
 */

#pragma once

#include "types.hpp"
#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace terry::bot {

struct user {
	using json = nlohmann::json;

	user_id id{};
	std::string username;
	double combat_power{}; // >= 0
	int wins{};						 // persisted in users.json
	int games{};					 // persisted in users.json

	[[nodiscard]] json to_json() const;
	[[nodiscard]] static user from_json(const json &);
};

struct team {
	std::vector<user> members;
	double total_power{};

	void add_member(const user &u)
	{
		members.push_back(u);
		total_power += u.combat_power;
	}
	void recalc_total_power();
};

struct match_record {
	using json = nlohmann::json;

	timestamp when{};
	std::vector<team> teams;
	std::vector<int> winning_teams; // indices

	[[nodiscard]] json to_json() const;
	[[nodiscard]] static match_record from_json(const json &);
};

/**
 * @brief Format a timestamp as local time string (YYYY-MM-DD HH:MM:SS).
 */
[[nodiscard]] std::string format_timestamp(timestamp tp);

} // namespace terry::bot