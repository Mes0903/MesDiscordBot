
#include "models.hpp"

#include <iomanip>
#include <sstream>

namespace terry::bot {

json user::to_json() const
{
	return json{{"discord_id", static_cast<uint64_t>(id)}, {"username", username}, {"combat_power", combat_power}, {"wins", wins}, {"games", games}};
}

user user::from_json(const json &j)
{
	user u;
	u.id = user_id(j.at("discord_id").get<uint64_t>());
	u.username = j.at("username").get<std::string>();
	u.combat_power = j.at("combat_power").get<int>();
	u.wins = j.value("wins", 0);
	u.games = j.value("games", 0);
	return u;
}

void team::recalc_total_power()
{
	total_power = 0;
	for (const auto &m : members)
		total_power += m.combat_power;
}

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

std::string format_timestamp(timestamp tp)
{
	auto t = std::chrono::system_clock::time_point(tp);
	std::time_t tt = std::chrono::system_clock::to_time_t(t);
	std::tm *lt = std::localtime(&tt);
	std::ostringstream oss;
	oss << std::put_time(lt, "%Y-%m-%d %H:%M:%S");
	return oss.str();
}

} // namespace terry::bot
