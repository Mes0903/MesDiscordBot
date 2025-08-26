#pragma once

#include "Team.hpp"

#include <chrono>
#include <string>
#include <vector>

struct Match {
	std::vector<Team> teams;
	std::vector<int> winning_teams; // indices of winning teams
	std::chrono::system_clock::time_point timestamp;

	Match() = default;

	json to_json() const;
	static Match from_json(const json &j);
};

std::string format_timestamp(const std::chrono::system_clock::time_point &tp);
