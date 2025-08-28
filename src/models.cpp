/**
 * @brief
 * Defines basic structures and JSON (de)serialization helpers:
 *	 - user        : registered player with combat power and W/L stats
 *	 - team        : a collection of users with a cached total_power
 *	 - match_record: stored match with timestamp, teams, and winners
 * Also includes `format_timestamp` used for UI embedding.
 */

#include "models.hpp"

#include <format>

namespace terry::bot {

using json = nlohmann::json;

/**
 * @brief Serialize a user to JSON for persistence.
 */
json user::to_json() const
{
	return json{{"discord_id", static_cast<uint64_t>(id)}, {"username", username}, {"combat_power", combat_power}, {"wins", wins}, {"games", games}};
}

/**
 * @brief Construct a user from JSON (defensive defaults for optional fields).
 */
user user::from_json(const json &j)
{
	user u;
	u.id = user_id(j.at("discord_id").get<uint64_t>());
	u.username = j.at("username").get<std::string>();
	u.combat_power = j.at("combat_power").get<double>();
	u.wins = j.value("wins", 0);
	u.games = j.value("games", 0);
	return u;
}

/**
 * @brief Recompute team's `total_power` by summing member combat power.
 */
void team::recalc_total_power()
{
	total_power = 0.0;
	for (const auto &m : members)
		total_power += m.combat_power;
}

/**
 * @brief Serialize a match record (timestamp seconds, winners, and team member IDs only).
 */
json match_record::to_json() const
{
	json out;
	out["timestamp"] = std::chrono::duration_cast<std::chrono::seconds>(when.time_since_epoch()).count();
	out["winning_teams"] = winning_teams;
	out["teams"] = json::array();
	for (const auto &t : teams) {
		json tj;
		tj["members"] = json::array();
		for (const auto &m : t.members) {
			tj["members"].push_back({{"discord_id", static_cast<uint64_t>(m.id)}});
		}
		out["teams"].push_back(std::move(tj));
	}
	return out;
}

/**
 * @brief Parse a match record from JSON (members are restored as ID-only).
 */
match_record match_record::from_json(const json &j)
{
	match_record mr;
	auto secs = j.at("timestamp").get<int64_t>();
	mr.when = timestamp{std::chrono::seconds{secs}};
	mr.winning_teams = j.at("winning_teams").get<std::vector<int>>();

	mr.teams.clear();
	for (const auto &tj : j.at("teams")) {
		team t;
		if (tj.contains("members")) {
			for (const auto &mj : tj.at("members")) {
				user u;
				u.id = user_id{mj.at("discord_id").get<uint64_t>()};
				t.members.push_back(std::move(u));
			}
		}
		mr.teams.push_back(std::move(t));
	}
	return mr;
}

/**
 * @brief Format a system_clock timestamp as local time "YYYY-MM-DD HH:MM:SS".
 */
std::string format_timestamp(timestamp tp)
{
	const auto zt = std::chrono::zoned_time{std::chrono::current_zone(), tp};
	return std::format("{:%Y-%m-%d %H:%M:%S}", zt);
}

} // namespace terry::bot
