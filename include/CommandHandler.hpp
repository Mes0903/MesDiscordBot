#pragma once

#include "TeamManager.hpp"
#include <dpp/dpp.h>

class CommandHandler {
private:
	TeamManager &team_manager;

	// Individual command handlers
	void handle_add_user(const dpp::slashcommand_t &event);
	void handle_remove_user(const dpp::slashcommand_t &event);
	void handle_update_power(const dpp::slashcommand_t &event);
	void handle_list_users(const dpp::slashcommand_t &event);
	void handle_create_teams(const dpp::slashcommand_t &event);
	void handle_match_history(const dpp::slashcommand_t &event);

public:
	explicit CommandHandler(TeamManager &manager);

	void handle_command(const dpp::slashcommand_t &event);

	// Command registration
	static std::vector<dpp::slashcommand> create_commands(dpp::snowflake bot_id);
};
