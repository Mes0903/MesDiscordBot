#pragma once

#include "team_manager.hpp"
#include <dpp/dpp.h>

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace terry::bot {

struct selection_session {
	std::string panel_id;
	dpp::snowflake guild_id{};
	dpp::snowflake channel_id{};
	dpp::snowflake owner_id{};
	int num_teams{};
	std::vector<user_id> selected;
	std::vector<team> last_teams;
	bool active{true};
};

class command_handler {
public:
	explicit command_handler(team_manager &tm) : tm_(tm) {}

	// DPP entrypoints
	void on_slash(const dpp::slashcommand_t &ev);
	void on_button(const dpp::button_click_t &ev);
	void on_select(const dpp::select_click_t &ev);

	// slash command registration
	[[nodiscard]] static std::vector<dpp::slashcommand> commands(dpp::snowflake bot_id);

private:
	team_manager &tm_;
	std::unordered_map<std::string, selection_session> sessions_; // key: panel_id

	// Resolve a user's friendly name within the guild
	[[nodiscard]] std::string display_name(user_id uid, dpp::snowflake guild) const; // unused

	// commands
	void cmd_help(const dpp::slashcommand_t &ev);
	void cmd_adduser(const dpp::slashcommand_t &ev);
	void cmd_removeuser(const dpp::slashcommand_t &ev);
	void cmd_listusers(const dpp::slashcommand_t &ev);
	void cmd_formteams(const dpp::slashcommand_t &ev);
	void cmd_history(const dpp::slashcommand_t &ev);

	// helpers (ui/panel)
	static std::string make_token();
	static bool starts_with(const std::string &s, const std::string &p) { return s.rfind(p, 0) == 0; }
	[[nodiscard]] dpp::message build_panel_message(const selection_session &s) const;
};

} // namespace terry::bot
