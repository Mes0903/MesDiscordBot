
#pragma once

#include "types.hpp"
#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace terry::bot {

using json = nlohmann::json;

struct user {
	user_id id{};
	std::string username;
	int combat_power{}; // >= 0

	[[nodiscard]] json to_json() const;
	[[nodiscard]] static user from_json(const json &);
};

struct team {
	std::vector<user> members;
	int total_power{};

	void add_member(const user &u)
	{
		members.push_back(u);
		total_power += u.combat_power;
	}
	void recalc_total_power();
};

struct match_record {
	timestamp when{};
	std::vector<team> teams;
	std::vector<int> winning_teams; // indices

	[[nodiscard]] json to_json() const;
	[[nodiscard]] static match_record from_json(const json &);
};

[[nodiscard]] std::string format_timestamp(timestamp tp);

} // namespace terry::bot
