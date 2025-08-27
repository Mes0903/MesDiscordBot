#pragma once

#include "team_manager.hpp"
#include <dpp/dpp.h>

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace terry::bot {

namespace text {
inline constexpr std::string_view unknown_command = "未知的指令";
inline constexpr std::string_view unsupported_button = "不支援的按鈕";
inline constexpr std::string_view unsupported_select = "不支援的選項";
inline constexpr std::string_view panel_expired = "此面板已失效";
inline constexpr std::string_view panel_owner_only = "只有面板擁有者才能操作此面板";
inline constexpr std::string_view need_one_per_team = "請至少選擇一名成員加入每個隊伍";
inline constexpr std::string_view invalid_team_index = "無效的隊伍索引";
inline constexpr std::string_view unknown_panel_action = "未知的面板操作";
inline constexpr std::string_view teams_must_positive = "隊伍數量需大於 0";
inline constexpr std::string_view no_registered_users = "目前沒有註冊的使用者，請先用 `/adduser` 新增";
inline constexpr std::string_view no_users = "尚無使用者";
inline constexpr std::string_view no_history = "尚無對戰紀錄";

inline constexpr std::string_view ok_prefix = "✅ ";
inline constexpr std::string_view err_prefix = "❌ ";
inline constexpr std::string_view trophy = "🏆 ";
inline constexpr std::string_view runner_up = "🥈 ";
} // namespace text

inline dpp::message make_err_msg(std::string_view s) { return dpp::message(std::string(text::err_prefix).append(s)).set_flags(dpp::m_ephemeral); }
inline void reply_err(const dpp::slashcommand_t &ev, std::string_view s) { ev.reply(make_err_msg(s)); }
inline void reply_err(const dpp::button_click_t &ev, std::string_view s) { ev.reply(dpp::ir_channel_message_with_source, make_err_msg(s)); }
inline void reply_err(const dpp::select_click_t &ev, std::string_view s) { ev.reply(dpp::ir_channel_message_with_source, make_err_msg(s)); }

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
