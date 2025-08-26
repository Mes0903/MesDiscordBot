#pragma once

#include "Match.hpp"

#include <random>
#include <unordered_map>
#include <vector>

class TeamManager {
private:
	std::unordered_map<uint64_t, User> users;
	std::vector<Match> match_history;
	std::mt19937 rng{std::random_device{}()};

	static constexpr const char *USERS_FILE = "users.json";
	static constexpr const char *MATCHES_FILE = "matches.json";

	void load_users();
	void save_users();
	void load_matches();
	void save_matches();

public:
	TeamManager() = default;

	// Data persistence
	void load_data();
	void save_data();

	// User management
	void add_user(dpp::snowflake discord_id, const std::string &username, int combat_power);
	bool remove_user(dpp::snowflake discord_id);
	bool update_combat_power(dpp::snowflake discord_id, int new_power);
	std::vector<User> get_all_users() const;
	User *get_user(dpp::snowflake discord_id);

	// Team formation
	std::vector<Team> create_balanced_teams(const std::vector<dpp::snowflake> &participant_ids, int num_teams);

	// Match management
	void record_match(const std::vector<Team> &teams, const std::vector<int> &winning_teams);
	std::vector<Match> get_recent_matches(int count = 10) const;
};
