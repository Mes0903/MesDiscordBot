#include "core/constants.hpp"
#include "handlers/command_handler.hpp"
#include "ui/embed_builder.hpp"
#include "ui/message_builder.hpp"

#include <format>

namespace terry {

command_handler::command_handler(std::shared_ptr<team_service> team_svc, std::shared_ptr<match_service> match_svc, std::shared_ptr<session_manager> session_mgr,
																 std::shared_ptr<panel_builder> panel_bld)
		: team_svc_(std::move(team_svc)), match_svc_(std::move(match_svc)), session_mgr_(std::move(session_mgr)), panel_bld_(std::move(panel_bld))
{
}

auto command_handler::on_slash(const dpp::slashcommand_t &ev) -> void
{
	auto name = ev.command.get_command_name();

	if (name == "help")
		return cmd_help(ev);
	if (name == "adduser")
		return cmd_adduser(ev);
	if (name == "removeuser")
		return cmd_removeuser(ev);
	if (name == "listusers")
		return cmd_listusers(ev);
	if (name == "formteams")
		return cmd_formteams(ev);
	if (name == "history")
		return cmd_history(ev);
	if (name == "sethistory")
		return cmd_sethistory(ev);

	return ui::message_builder::reply_error(ev, constants::text::unknown_command);
}

auto command_handler::commands(dpp::snowflake bot_id) -> std::vector<dpp::slashcommand>
{
	std::vector<dpp::slashcommand> cmds;

	cmds.emplace_back("help", "顯示指令清單與說明", bot_id);

	cmds.emplace_back("adduser", "新增或更新使用者的分數", bot_id)
			.add_option(dpp::command_option(dpp::co_user, "user", "Discord 使用者", true))
			.add_option(dpp::command_option(dpp::co_number, "point", "分數 (>=0.0)", true));

	cmds.emplace_back("removeuser", "移除使用者", bot_id).add_option(dpp::command_option(dpp::co_user, "user", "Discord 使用者", true));

	cmds.emplace_back("listusers", "顯示已註冊的使用者", bot_id);

	cmds.emplace_back("formteams", "分配隊伍", bot_id).add_option(dpp::command_option(dpp::co_integer, "teams", "隊伍數量（預設 2）", false));

	cmds.emplace_back("history", "顯示近期對戰紀錄", bot_id).add_option(dpp::command_option(dpp::co_integer, "count", "要顯示幾筆（預設 5）", false));

	cmds.emplace_back("sethistory", "開啟/切換最近 8 場的歷史編輯面板", bot_id);

	return cmds;
}

auto command_handler::cmd_help(const dpp::slashcommand_t &ev) -> void
{
	auto embed = ui::embed_builder::build_help();
	ev.reply(dpp::message().add_embed(embed));
}

auto command_handler::cmd_adduser(const dpp::slashcommand_t &ev) -> void
{
	dpp::snowflake uid = std::get<dpp::snowflake>(ev.get_parameter("user"));
	double point = std::get<double>(ev.get_parameter("point"));

	// Resolve a human-readable display name for `uid` in this guild.
	// Priority: guild nickname > global display name > username (handle).
	std::string display;

	// First, try the member object parsed from this slash invocation.
	if (auto mit = ev.command.resolved.members.find(uid); mit != ev.command.resolved.members.end()) {
		// Empty string means the member has no nickname in this guild.
		display = mit->second.get_nickname();
	}

	// If no nickname, fall back to the user parsed from this invocation.
	if (display.empty()) {
		if (auto uit = ev.command.resolved.users.find(uid); uit != ev.command.resolved.users.end()) {
			// Prefer global display name; otherwise use the account handle.
			display = !uit->second.global_name.empty() ? uit->second.global_name : uit->second.username;
		}
	}

	// Try the guild cache for a nickname.
	if (display.empty()) {
		const dpp::guild_member gm = dpp::find_guild_member(ev.command.guild_id, uid);
		if (gm.user_id) {															// non-zero means we actually found the member in cache
			const std::string nick = gm.get_nickname(); // may still be empty
			if (!nick.empty()) {
				display = nick;
			}
		}
	}

	// Last resort: user cache (global display name, then handle).
	if (display.empty()) {
		if (auto u = dpp::find_user(uid)) {
			display = !u->global_name.empty() ? u->global_name : u->username;
		}
	}

	// Store it.
	if (auto res = match_svc_->upsert_user(uid, display, point); !res) {
		return ui::message_builder::reply_error(ev, res.error().what());
	}

	if (auto res = match_svc_->save(); !res) {
		return ui::message_builder::reply_error(ev, res.error().what());
	}

	// Build a single response: content = success toast, embed = current user list
	// Query the latest user list (sorted just like `/listusers`)
	auto users = match_svc_->list_users(true);

	// Compose the success line first
	std::string ok = std::format("新增/更新使用者 {} 的分數為 {:.0f}", util::mention(uid), point);

	// If the list is empty (edge case), append the "no users" hint to content.
	if (users.empty()) {
		ok += std::format("\n{}", constants::text::no_users);
		return ev.reply(ui::message_builder::success(ok));
	}

	// Otherwise, attach the user list embed to the same reply.
	auto embed = ui::embed_builder::build_user_list(users);
	dpp::message msg = ui::message_builder::success(ok);
	msg.add_embed(embed);
	return ev.reply(msg);
}

auto command_handler::cmd_removeuser(const dpp::slashcommand_t &ev) -> void
{
	dpp::snowflake uid = std::get<dpp::snowflake>(ev.get_parameter("user"));

	if (auto res = match_svc_->remove_user(uid); !res) {
		return ui::message_builder::reply_error(ev, res.error().what());
	}

	if (auto res = match_svc_->save(); !res) {
		return ui::message_builder::reply_error(ev, res.error().what());
	}

	// Build a single response: content = success toast, embed = current user list
	{
		auto users = match_svc_->list_users(true);

		// Success line
		std::string ok = std::format("🗑️ 移除使用者 {}", util::mention(uid));

		if (users.empty()) {
			ok += std::format("\n{}", constants::text::no_users);
			return ev.reply(ui::message_builder::success(ok)); // or ui::message_builder::success(ok)
		}

		auto embed = ui::embed_builder::build_user_list(users);
		dpp::message msg = ui::message_builder::success(ok);
		msg.add_embed(embed);
		return ev.reply(msg);
	}
}

auto command_handler::cmd_listusers(const dpp::slashcommand_t &ev) -> void
{
	auto users = match_svc_->list_users(true);

	if (users.empty()) {
		return ui::message_builder::reply_error(ev, constants::text::no_users);
	}

	auto embed = ui::embed_builder::build_user_list(users);
	return ev.reply(dpp::message().add_embed(embed));
}

auto command_handler::cmd_formteams(const dpp::slashcommand_t &ev) -> void
{
	int num_teams = 2;
	if (auto p = ev.get_parameter("teams"); std::holds_alternative<int64_t>(p)) {
		num_teams = static_cast<int>(std::get<int64_t>(p));
	}

	if (num_teams < 1) {
		return ui::message_builder::reply_error(ev, constants::text::teams_must_positive);
	}

	auto users = match_svc_->list_users();
	if (users.empty()) {
		return ui::message_builder::reply_error(ev, "沒有註冊的使用者，請先用 `/adduser` 新增");
	}

	if (static_cast<std::size_t>(num_teams) > users.size()) {
		return ui::message_builder::reply_error(ev, "使用者數量不足以分配該隊伍數量");
	}

	panel_session sess{.guild_id = ev.command.guild_id,
										 .channel_id = ev.command.channel_id,
										 .owner_id = ev.command.usr.id,
										 .type = panel_type::formteams,
										 .num_teams = num_teams};

	auto panel_id = session_mgr_->create_session(std::move(sess));
	auto session = session_mgr_->get_session(panel_id);

	if (!session) {
		return ui::message_builder::reply_error(ev, "無法建立 session");
	}

	auto msg = panel_bld_->build_formteams_panel(session->get(), users);
	msg.set_content(std::format("👑 分配面板擁有者：{} — 只有擁有者可以操作此面板", util::mention(session->get().owner_id)));
	return ev.reply(msg);
}

auto command_handler::cmd_history(const dpp::slashcommand_t &ev) -> void
{
	int count = static_cast<int>(constants::limits::default_history_count);
	if (auto p = ev.get_parameter("count"); std::holds_alternative<int64_t>(p)) {
		count = static_cast<int>(std::get<int64_t>(p));
	}

	auto matches = match_svc_->recent_matches(count);

	if (matches.empty()) {
		return ui::message_builder::reply_error(ev, "尚無對戰紀錄");
	}

	auto embed = ui::embed_builder::build_history(matches);
	return ev.reply(dpp::message().add_embed(embed));
}

auto command_handler::cmd_sethistory(const dpp::slashcommand_t &ev) -> void
{
	constexpr int kMaxRecent = 8;
	auto indexed_matches = match_svc_->recent_indexed_matches(kMaxRecent);
	if (indexed_matches.empty()) {
		return ui::message_builder::reply_error(ev, "目前沒有任何對戰紀錄（請先在分隊面板中分配後按「新增場次」）");
	}

	panel_session sess{};
	sess.guild_id = ev.command.guild_id;
	sess.channel_id = ev.command.channel_id;
	sess.owner_id = ev.command.usr.id;
	sess.type = panel_type::sethistory;
	sess.num_teams = static_cast<int>(indexed_matches.front().second.teams.size());
	sess.formed_teams = indexed_matches.front().second.teams;
	sess.selected_match_index = indexed_matches.front().first; // global index

	auto panel_id = session_mgr_->create_session(std::move(sess));
	auto session = session_mgr_->get_session(panel_id);
	if (!session) {
		return ui::message_builder::reply_error(ev, "無法建立 session");
	}

	auto msg = panel_bld_->build_sethistory_panel(session->get(), indexed_matches);
	msg.set_content("🏅 歷史編輯面板（可從下拉清單切換最近 8 場）");
	return ev.reply(msg);
}

} // namespace terry
