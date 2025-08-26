#pragma once

#include "TeamManager.hpp"
#include <dpp/dpp.h>

#include <chrono>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Modern C++ session management for user selection
class UserSelectionSession {
private:
	std::string session_id;
	int num_teams;
	std::vector<User> available_users;
	std::unordered_set<uint64_t> selected_user_ids;
	std::chrono::steady_clock::time_point created_at;

public:
	UserSelectionSession(std::string id, int teams, std::vector<User> users)
			: session_id(std::move(id)), num_teams(teams), available_users(std::move(users)), created_at(std::chrono::steady_clock::now())
	{
	}

	std::vector<User> get_selected_users() const
	{
		std::vector<User> result;
		for (const auto &user : available_users) {
			if (selected_user_ids.contains(static_cast<uint64_t>(user.discord_id))) {
				result.push_back(user);
			}
		}
		return result;
	}

	void update_selection(const std::vector<std::string> &selected_ids)
	{
		selected_user_ids.clear();
		for (const auto &id_str : selected_ids) {
			try {
				selected_user_ids.insert(std::stoull(id_str));
			} catch (const std::exception &) {
				// Skip invalid IDs
			}
		}
	}

	void toggle_user_selection(uint64_t user_id)
	{
		if (selected_user_ids.contains(user_id)) {
			selected_user_ids.erase(user_id);
		}
		else {
			selected_user_ids.insert(user_id);
		}
	}

	void select_all()
	{
		selected_user_ids.clear();
		for (const auto &user : available_users) {
			selected_user_ids.insert(static_cast<uint64_t>(user.discord_id));
		}
	}

	void clear_selection() { selected_user_ids.clear(); }

	const std::string &get_session_id() const { return session_id; }
	int get_num_teams() const { return num_teams; }
	const std::vector<User> &get_available_users() const { return available_users; }
	const std::unordered_set<uint64_t> &get_selected_user_ids() const { return selected_user_ids; }

	bool is_expired() const
	{
		using namespace std::chrono_literals;
		return std::chrono::steady_clock::now() - created_at > 15min;
	}
};

class UserSelectionManager {
private:
	std::unordered_map<std::string, std::unique_ptr<UserSelectionSession>> sessions;

	std::string generate_session_id()
	{
		static std::random_device rd;
		static std::mt19937 gen(rd());
		static std::uniform_int_distribution<> dis(100000, 999999);
		return std::to_string(dis(gen));
	}

public:
	std::string create_session(int num_teams, std::vector<User> users)
	{
		cleanup_expired_sessions();

		auto session_id = generate_session_id();
		sessions[session_id] = std::make_unique<UserSelectionSession>(session_id, num_teams, std::move(users));
		return session_id;
	}

	UserSelectionSession *get_session(const std::string &session_id)
	{
		auto it = sessions.find(session_id);
		return it != sessions.end() ? it->second.get() : nullptr;
	}

	void remove_session(const std::string &session_id) { sessions.erase(session_id); }

private:
	void cleanup_expired_sessions()
	{
		for (auto it = sessions.begin(); it != sessions.end();) {
			if (it->second->is_expired()) {
				it = sessions.erase(it);
			}
			else {
				++it;
			}
		}
	}
};

class CommandHandler {
private:
	TeamManager &team_manager;
	UserSelectionManager selection_manager;

	// Individual command handlers
	void handle_add_user(const dpp::slashcommand_t &event);
	void handle_remove_user(const dpp::slashcommand_t &event);
	void handle_update_power(const dpp::slashcommand_t &event);
	void handle_list_users(const dpp::slashcommand_t &event);
	void handle_create_teams(const dpp::slashcommand_t &event);
	void handle_match_history(const dpp::slashcommand_t &event);
	void handle_help(const dpp::slashcommand_t &event);

	// Component interaction handlers - using correct DPP event type
	void handle_user_selection_interaction(const dpp::button_click_t &event);
	void handle_user_selection_interaction(const dpp::select_click_t &event);
	void handle_toggle_user_interaction(const dpp::button_click_t &event);
	void handle_create_teams_button_interaction(const dpp::button_click_t &event, const std::string &session_id);
	void handle_select_all_interaction(const dpp::button_click_t &event, const std::string &session_id);
	void handle_clear_selection_interaction(const dpp::button_click_t &event, const std::string &session_id);

	// Helper functions
	void create_user_selection_interface(const dpp::slashcommand_t &event, int num_teams, const std::vector<User> &users);

	std::optional<std::string> extract_session_id(const std::string &custom_id, const std::string &prefix);

	dpp::message create_selection_message(const UserSelectionSession &session);
	dpp::message create_button_selection_message(const UserSelectionSession &session);
	dpp::message create_teams_result_message(const std::vector<Team> &teams);
	dpp::message create_teams_result_with_selection(const UserSelectionSession &session, const std::vector<Team> &teams);

public:
	explicit CommandHandler(TeamManager &manager);

	void handle_command(const dpp::slashcommand_t &event);
	void handle_button_click(const dpp::button_click_t &event);
	void handle_select_click(const dpp::select_click_t &event);

	static std::vector<dpp::slashcommand> create_commands(dpp::snowflake bot_id);
};