#include "TeamManager.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>

void TeamManager::load_data()
{
	load_users();
	load_matches();
}

void TeamManager::save_data()
{
	save_users();
	save_matches();
}

void TeamManager::add_user(dpp::snowflake discord_id, const std::string &username, int combat_power)
{
	users[static_cast<uint64_t>(discord_id)] = User{discord_id, username, combat_power};
	save_users();
}

bool TeamManager::remove_user(dpp::snowflake discord_id)
{
	auto it = users.find(static_cast<uint64_t>(discord_id));
	if (it != users.end()) {
		users.erase(it);
		save_users();
		return true;
	}
	return false;
}

bool TeamManager::update_combat_power(dpp::snowflake discord_id, int new_power)
{
	auto it = users.find(static_cast<uint64_t>(discord_id));
	if (it != users.end()) {
		it->second.combat_power = new_power;
		save_users();
		return true;
	}
	return false;
}

std::vector<User> TeamManager::get_all_users() const
{
	std::vector<User> result;
	for (const auto &[id, user] : users) {
		result.push_back(user);
	}
	std::sort(result.begin(), result.end(), [](const User &a, const User &b) { return a.combat_power > b.combat_power; });
	return result;
}

User *TeamManager::get_user(dpp::snowflake discord_id)
{
	auto it = users.find(static_cast<uint64_t>(discord_id));
	return it != users.end() ? &it->second : nullptr;
}

std::vector<Team> TeamManager::create_balanced_teams(const std::vector<dpp::snowflake> &participant_ids, int num_teams)
{
	std::vector<User> participants;

	// Get users for participants
	for (auto id : participant_ids) {
		auto *user = get_user(id);
		if (user) {
			participants.push_back(*user);
		}
	}

	if (participants.empty() || num_teams <= 0) {
		return {};
	}

	// Sort by combat power (descending) for better balancing
	std::sort(participants.begin(), participants.end(), [](const User &a, const User &b) { return a.combat_power > b.combat_power; });

	// Create teams
	std::vector<Team> teams(num_teams);

	// Distribute players using a balanced approach
	for (size_t i = 0; i < participants.size(); ++i) {
		// Find team with lowest total power
		auto min_team = std::min_element(teams.begin(), teams.end(), [](const Team &a, const Team &b) { return a.total_power < b.total_power; });

		min_team->add_member(participants[i]);
	}

	// Add some randomization by occasionally swapping players between teams
	for (int swap_attempts = 0; swap_attempts < static_cast<int>(participants.size()) / 4; ++swap_attempts) {
		std::uniform_int_distribution<size_t> team_dist(0, teams.size() - 1);
		size_t team1_idx = team_dist(rng);
		size_t team2_idx = team_dist(rng);

		if (team1_idx == team2_idx || teams[team1_idx].members.empty() || teams[team2_idx].members.empty()) {
			continue;
		}

		std::uniform_int_distribution<size_t> member1_dist(0, teams[team1_idx].members.size() - 1);
		std::uniform_int_distribution<size_t> member2_dist(0, teams[team2_idx].members.size() - 1);

		size_t member1_idx = member1_dist(rng);
		size_t member2_idx = member2_dist(rng);

		// Calculate power difference before and after swap
		int power_diff_before = std::abs(teams[team1_idx].total_power - teams[team2_idx].total_power);
		int power_diff_after =
				std::abs((teams[team1_idx].total_power - teams[team1_idx].members[member1_idx].combat_power + teams[team2_idx].members[member2_idx].combat_power) -
								 (teams[team2_idx].total_power - teams[team2_idx].members[member2_idx].combat_power + teams[team1_idx].members[member1_idx].combat_power));

		// Only swap if it doesn't make balance significantly worse
		if (power_diff_after <= power_diff_before + 50) { // Allow some tolerance for randomization
			User temp = teams[team1_idx].members[member1_idx];
			teams[team1_idx].members[member1_idx] = teams[team2_idx].members[member2_idx];
			teams[team2_idx].members[member2_idx] = temp;

			// Recalculate total power
			teams[team1_idx].recalculate_total_power();
			teams[team2_idx].recalculate_total_power();
		}
	}

	return teams;
}

void TeamManager::record_match(const std::vector<Team> &teams, const std::vector<int> &winning_teams)
{
	Match match;
	match.teams = teams;
	match.winning_teams = winning_teams;
	match.timestamp = std::chrono::system_clock::now();

	match_history.push_back(match);
	save_matches();
}

std::vector<Match> TeamManager::get_recent_matches(int count) const
{
	std::vector<Match> recent;
	int start = std::max(0, static_cast<int>(match_history.size()) - count);

	for (int i = static_cast<int>(match_history.size()) - 1; i >= start; --i) {
		recent.push_back(match_history[i]);
	}

	return recent;
}

void TeamManager::load_users()
{
	std::ifstream file(USERS_FILE);
	if (!file.is_open())
		return;

	try {
		json j;
		file >> j;

		for (const auto &user_json : j) {
			User user = User::from_json(user_json);
			users[static_cast<uint64_t>(user.discord_id)] = user;
		}
	} catch (const std::exception &e) {
		std::cerr << "Error loading users: " << e.what() << std::endl;
	}
}

void TeamManager::save_users()
{
	std::ofstream file(USERS_FILE);
	if (!file.is_open())
		return;

	json j = json::array();
	for (const auto &[id, user] : users) {
		j.push_back(user.to_json());
	}

	file << j.dump(2);
}

void TeamManager::load_matches()
{
	std::ifstream file(MATCHES_FILE);
	if (!file.is_open())
		return;

	try {
		json j;
		file >> j;

		for (const auto &match_json : j) {
			match_history.push_back(Match::from_json(match_json));
		}
	} catch (const std::exception &e) {
		std::cerr << "Error loading matches: " << e.what() << std::endl;
	}
}

void TeamManager::save_matches()
{
	std::ofstream file(MATCHES_FILE);
	if (!file.is_open())
		return;

	json j = json::array();
	for (const auto &match : match_history) {
		j.push_back(match.to_json());
	}

	file << j.dump(2);
}
