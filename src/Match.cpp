#include "Match.hpp"

#include <iomanip>
#include <sstream>

json Match::to_json() const
{
	json j;
	j["timestamp"] = std::chrono::duration_cast<std::chrono::seconds>(timestamp.time_since_epoch()).count();
	j["winning_teams"] = winning_teams;

	for (size_t i = 0; i < teams.size(); ++i) {
		json team_json;
		team_json["total_power"] = teams[i].total_power;
		for (const auto &member : teams[i].members) {
			team_json["members"].push_back(member.to_json());
		}
		j["teams"].push_back(team_json);
	}

	return j;
}

Match Match::from_json(const json &j)
{
	Match match;
	match.timestamp = std::chrono::system_clock::time_point(std::chrono::seconds(j["timestamp"].get<int64_t>()));
	match.winning_teams = j["winning_teams"].get<std::vector<int>>();

	for (const auto &team_json : j["teams"]) {
		Team team;
		team.total_power = team_json["total_power"].get<int>();
		for (const auto &member_json : team_json["members"]) {
			team.members.push_back(User::from_json(member_json));
		}
		match.teams.push_back(team);
	}

	return match;
}

std::string format_timestamp(const std::chrono::system_clock::time_point &tp)
{
	auto time_t = std::chrono::system_clock::to_time_t(tp);
	std::stringstream ss;
	ss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
	return ss.str();
}
