#pragma once

#include "core/utils.hpp"
#include "models/team.hpp"
#include <nlohmann/json.hpp>

#include <chrono>
#include <vector>

namespace terry {

class match_record {
public:
	type::timestamp when{};
	std::vector<team> teams;
	std::vector<int> winning_teams;

	// explicit object parameter
	[[nodiscard]] auto to_json(this const auto &self) -> nlohmann::json
	{
		nlohmann::json out;
		out["timestamp"] = std::chrono::duration_cast<std::chrono::seconds>(self.when.time_since_epoch()).count();
		out["winning_teams"] = self.winning_teams;
		out["teams"] = nlohmann::json::array();

		for (const auto &t : self.teams) {
			nlohmann::json tj;
			tj["members"] = nlohmann::json::array();
			for (const auto &m : t.members) {
				tj["members"].push_back({{"discord_id", util::id_to_u64(m.id)}});
			}
			out["teams"].push_back(std::move(tj));
		}
		return out;
	}

	[[nodiscard]] static auto from_json(const nlohmann::json &j) -> match_record
	{
		match_record mr;
		auto secs = j.at("timestamp").get<int64_t>();
		mr.when = type::timestamp{std::chrono::seconds{secs}};
		mr.winning_teams = j.at("winning_teams").get<std::vector<int>>();

		for (const auto &tj : j.at("teams")) {
			team t;
			if (tj.contains("members")) {
				for (const auto &mj : tj.at("members")) {
					user u;
					u.id = dpp::snowflake{mj.at("discord_id").get<uint64_t>()};
					t.add_member(std::move(u));
				}
			}
			mr.teams.push_back(std::move(t));
		}
		return mr;
	}

	// Helper method to check if a team won
	[[nodiscard]] auto is_winner(this const auto &self, int team_index) -> bool
	{
		return std::ranges::find(self.winning_teams, team_index) != self.winning_teams.end();
	}
};

// Utility function for formatting timestamps
[[nodiscard]] inline auto format_timestamp(type::timestamp tp) -> std::string
{
	const auto zt = std::chrono::zoned_time{std::chrono::current_zone(), tp};
	return std::format("{:%Y-%m-%d %H:%M:%S}", zt);
}

} // namespace terry
