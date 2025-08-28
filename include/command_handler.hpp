/**
 * @brief
 * Responsibilities:
 *   - Dispatch slash-commands (help/adduser/removeuser/listusers/formteams/history)
 *   - Manage interactive panel lifecycle (buttons & select menu)
 *   - Enforce ownership and session validity for interactions
 * Notes:
 *   - custom_id format: `panel:<panel_id>:<action>[:arg]`
 *   - Integer parsing uses std::from_chars (no exceptions)
 */

#pragma once

#include "team_manager.hpp"
#include <dpp/dpp.h>

#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace terry::bot {

namespace text {
inline constexpr std::string_view unknown_command = "未知的指令";
inline constexpr std::string_view unsupported_button = "不支援的按鈕";
inline constexpr std::string_view unsupported_select = "不支援的選項";
inline constexpr std::string_view panel_expired = "此面板已失效";
inline constexpr std::string_view panel_owner_only = "只有面板擁有者才能操作此面板";
inline constexpr std::string_view need_one_per_team = "請至少選擇一名成員";
inline constexpr std::string_view invalid_team_index = "無效的隊伍索引";
inline constexpr std::string_view unknown_panel_action = "未知的面板操作";
inline constexpr std::string_view teams_must_positive = "隊伍數量需大於 0";
inline constexpr std::string_view teams_too_much = "使用者數量不足以分配該隊伍數量";
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

// Panel usage
enum class panel_kind { formteams, setwinner };

/**
 * @struct panel_session
 * @brief Tracks the state for an interactive assignment panel.
 * @note Invalidated when the owner presses "End", or upon expiry.
 */
struct panel_session {
	std::string panel_id;
	dpp::snowflake guild_id{}, channel_id{}, owner_id{};
	int num_teams = 2;
	std::vector<user_id> selected;
	std::vector<team> last_teams;
	bool active = true;
	// Lifecycle time points (count-based eviction; no time-based expiry)
	std::chrono::steady_clock::time_point created_at{};
	std::chrono::steady_clock::time_point last_touch{};
	panel_kind kind{panel_kind::formteams};
	// For setwinner: currently selected snapshot index in sessions_
	std::optional<std::size_t> chosen{};
};

/**
 * @brief A snapshot of a formed match used by /setwinner.
 * It stores only the essentials we need to render and update winners.
 */
struct match_snapshot {
	std::string snap_id;			 // stable ID used by <select> value
	timestamp when{};					 // when the teams were formed
	dpp::snowflake owner_id{}; // who formed the teams
	std::vector<team> teams;	 // formed teams (members + total_point)
};

/**
 * @class command_handler
 * @brief Wires DPP events (slash/button/select) to bot behaviors.
 */
class command_handler {
public:
	/**
	 * @brief Construct a command handler bound to a dpp::cluster and team_manager.
	 * @param bot Discord cluster (event source/sink).
	 * @param tm  Reference to the shared team_manager.
	 */
	explicit command_handler(team_manager &tm) : tm_(tm) {}

	// DPP entrypoints
	/** @brief Handle incoming slash-commands. */
	void on_slash(const dpp::slashcommand_t &ev);
	/** @brief Handle button interactions from the panel. */
	void on_button(const dpp::button_click_t &ev);
	/** @brief Handle select-menu interactions from the panel. */
	void on_select(const dpp::select_click_t &ev);

	// slash command registration
	[[nodiscard]] static std::vector<dpp::slashcommand> commands(dpp::snowflake bot_id);

private:
	team_manager &tm_;
	std::unordered_map<std::string, panel_session, std::hash<std::string_view>, std::equal_to<>> sessions_;
	void keep_recent_sessions_(std::size_t max_sessions = 8);

	// helpers (ui/panel)
	static std::string make_token_();

	// commands
	void cmd_help_(const dpp::slashcommand_t &ev);
	void cmd_adduser_(const dpp::slashcommand_t &ev);
	void cmd_removeuser_(const dpp::slashcommand_t &ev);
	void cmd_listusers_(const dpp::slashcommand_t &ev);
	void cmd_formteams_(const dpp::slashcommand_t &ev);
	void cmd_history_(const dpp::slashcommand_t &ev);
	void cmd_setwinner_(const dpp::slashcommand_t &ev);

	/** @brief Build the interactive assignment panel message for a given session. */
	[[nodiscard]] dpp::message build_formteams_panel_msg_(const panel_session &s) const;
	[[nodiscard]] dpp::message build_setwinner_panel_msg_(const panel_session &sess) const;
};

} // namespace terry::bot
