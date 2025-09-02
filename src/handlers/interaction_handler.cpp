#include "core/constants.hpp"
#include "handlers/interaction_handler.hpp"
#include "ui/message_builder.hpp"

#include <charconv>
#include <format>

namespace terry {

interaction_handler::interaction_handler(std::shared_ptr<team_service> team_svc, std::shared_ptr<match_service> match_svc,
																				 std::shared_ptr<session_manager> session_mgr, std::shared_ptr<panel_builder> panel_bld)
		: team_svc_(std::move(team_svc)), match_svc_(std::move(match_svc)), session_mgr_(std::move(session_mgr)), panel_bld_(std::move(panel_bld))
{
}

auto interaction_handler::parse_custom_id(std::string_view custom_id) const -> std::optional<parsed_custom_id>
{

	if (!custom_id.starts_with("panel:")) {
		return std::nullopt;
	}

	auto rest = custom_id.substr(6);
	auto first_colon = rest.find(':');
	if (first_colon == std::string_view::npos) {
		return std::nullopt;
	}

	parsed_custom_id out;
	out.panel_id = std::string(rest.substr(0, first_colon));

	auto action_part = rest.substr(first_colon + 1);
	auto second_colon = action_part.find(':');

	if (second_colon != std::string_view::npos) {
		out.action = std::string(action_part.substr(0, second_colon));
		out.arg = std::string(action_part.substr(second_colon + 1));
	}
	else {
		out.action = std::string(action_part);
	}

	return out;
}

auto interaction_handler::on_button(const dpp::button_click_t &ev) -> void
{
	auto parsed = parse_custom_id(ev.custom_id);
	if (!parsed) {
		return ui::message_builder::reply_error(ev, constants::text::unsupported_button);
	}

	auto session = session_mgr_->get_session(parsed->panel_id);
	if (!session) {
		return ui::message_builder::reply_error(ev, constants::text::panel_expired);
	}

	auto &sess = session->get();
	if (ev.command.usr.id != sess.owner_id) {
		return ui::message_builder::reply_error(ev, constants::text::panel_owner_only);
	}

	if (parsed->action == "assign") {
		handle_assign(ev, sess);
	}
	else if (parsed->action == "newmatch") {
		handle_newmatch(ev, sess);
	}
	else if (parsed->action == "end") {
		handle_end(ev, sess);
	}
	else if (parsed->action == "win") {
		handle_win(ev, sess);
	}
	else if (parsed->action == "remove") {
		handle_remove(ev, sess);
	}
	else {
		ui::message_builder::reply_error(ev, "未知的按鈕操作");
	}
}

auto interaction_handler::on_select(const dpp::select_click_t &ev) -> void
{
	auto parsed = parse_custom_id(ev.custom_id);
	if (!parsed) {
		return ui::message_builder::reply_error(ev, "不支援的選項");
	}

	auto session = session_mgr_->get_session(parsed->panel_id);
	if (!session) {
		return ui::message_builder::reply_error(ev, constants::text::panel_expired);
	}

	auto &sess = session->get();
	if (ev.command.usr.id != sess.owner_id) {
		return ui::message_builder::reply_error(ev, constants::text::panel_owner_only);
	}

	if (parsed->action == "select") {
		handle_user_select(ev, sess);
	}
	else if (parsed->action == "choose") {
		handle_match_choose(ev, sess);
	}
	else {
		ui::message_builder::reply_error(ev, "不支援的選項操作");
	}
}

auto interaction_handler::handle_assign(const dpp::button_click_t &ev, panel_session &sess) -> void
{
	if (sess.selected_users.empty()) {
		return ui::message_builder::reply_error(ev, "請至少選擇一名成員");
	}

	if (static_cast<int>(sess.selected_users.size()) < sess.num_teams) {
		return ui::message_builder::reply_error(ev, "使用者數量不足以分配該隊伍數量");
	}

	// Get selected users
	std::vector<user> participants;
	for (auto uid : sess.selected_users) {
		if (auto u = match_svc_->find_user(uid)) {
			participants.push_back(u->get());
		}
	}

	// Form teams
	auto teams_res = team_svc_->form_teams(participants, {.num_teams = sess.num_teams});
	if (!teams_res) {
		return ui::message_builder::reply_error(ev, teams_res.error().what());
	}

	sess.formed_teams = std::move(*teams_res);

	auto users = match_svc_->list_users();
	auto msg = panel_bld_->build_formteams_panel(sess, users);
	ev.reply(dpp::ir_update_message, msg);
}

auto interaction_handler::handle_newmatch(const dpp::button_click_t &ev, panel_session &sess) -> void
{
	if (sess.formed_teams.empty()) {
		return ui::message_builder::reply_error(ev, "尚未分配隊伍，請先點「分配」");
	}

	auto now = std::chrono::time_point_cast<type::timestamp::duration>(std::chrono::system_clock::now());
	auto res = match_svc_->add_match(sess.formed_teams, now);

	if (!res) {
		return ui::message_builder::reply_error(ev, res.error().what());
	}

	if (auto save_res = match_svc_->save(); !save_res) {
		return ui::message_builder::reply_error(ev, save_res.error().what());
	}

	auto users = match_svc_->list_users();
	auto msg = panel_bld_->build_formteams_panel(sess, users);
	msg.set_content("🆕 已新增一場比賽到對戰紀錄（待 `/sethistory` 設定勝負）");
	return ev.reply(dpp::ir_update_message, msg);
}

auto interaction_handler::handle_win(const dpp::button_click_t &ev, panel_session &sess) -> void
{
	// Parse and validate the custom_id. Expected format:
	//   "panel:{panel_id}:win:{team_idx}"
	// We rely on the existing parse_custom_id(...) helper which returns:
	//   parsed_custom_id { panel_id: string, action: string, arg: optional<string> }.
	auto parsed = parse_custom_id(ev.custom_id);
	if (!parsed || parsed->action != "win") {
		return ui::message_builder::reply_error(ev, "無法解析按鈕：不是有效的勝方設定按鈕");
	}

	// Ensure there is a selected match to update.
	if (!sess.selected_match_index) {
		return ui::message_builder::reply_error(ev, "目前沒有選定的場次可更新");
	}

	// Extract team index from the parsed arg.
	if (!parsed->arg) {
		return ui::message_builder::reply_error(ev, "缺少隊伍索引");
	}

	int team_idx = -1;
	{
		// Parse integer without allocations; arg is the substring after the last ':'.
		auto &a = *parsed->arg;
		auto [ptr, ec] = std::from_chars(a.data(), a.data() + a.size(), team_idx);
		if (ec != std::errc{}) {
			return ui::message_builder::reply_error(ev, "隊伍索引格式錯誤");
		}
	}

	// Validate team index against the currently loaded teams in session.
	// Code populates `sess.formed_teams` when a match is chosen, and also stores `sess.num_teams` accordingly.
	if (team_idx < 0 || team_idx >= static_cast<int>(sess.formed_teams.size())) {
		return ui::message_builder::reply_error(ev, "無效的隊伍索引");
	}

	// Persist winner to the selected match.
	if (auto res = match_svc_->set_match_winner(*sess.selected_match_index, std::vector<int>{team_idx}); !res) {
		return ui::message_builder::reply_error(ev, res.error().what());
	}

	// Recompute ratings for all users after the match outcome is updated.
	if (auto recompute = match_svc_->recompute_ratings(); !recompute) {
		return ui::message_builder::reply_error(ev, recompute.error().what());
	}

	// Save to storage.
	if (auto saved = match_svc_->save(); !saved) {
		return ui::message_builder::reply_error(ev, saved.error().what());
	}

	// Rebuild the history panel with global indices
	constexpr int kMaxRecent = 8;
	auto indexed_matches = match_svc_->recent_indexed_matches(kMaxRecent);

	// Re-render the panel message.
	auto msg = panel_bld_->build_sethistory_panel(sess, indexed_matches);
	msg.set_content(std::format("📝 已更新勝方為：隊伍 {}；已重算隱分與戰績並存檔", team_idx + 1));
	return ev.reply(dpp::ir_update_message, msg);
}

auto interaction_handler::handle_remove(const dpp::button_click_t &ev, panel_session &sess) -> void
{
	if (!sess.selected_match_index) {
		return ui::message_builder::reply_error(ev, "目前沒有選定的場次可移除");
	}

	// remove the game
	if (auto res = match_svc_->delete_match(*sess.selected_match_index); !res) {
		return ui::message_builder::reply_error(ev, res.error().what());
	}
	// recalulate & save the point
	if (auto res = match_svc_->recompute_ratings(); !res) {
		return ui::message_builder::reply_error(ev, res.error().what());
	}
	if (auto res = match_svc_->save(); !res) {
		return ui::message_builder::reply_error(ev, res.error().what());
	}

	// reset the select menu
	sess.selected_match_index.reset();

	const int kMaxRecent = 8;
	auto indexed_matches = match_svc_->recent_indexed_matches(kMaxRecent);

	auto msg = panel_bld_->build_sethistory_panel(sess, indexed_matches);
	msg.set_content("🗑️ 已移除該筆對戰紀錄；已重算隱分並存檔");
	return ev.reply(dpp::ir_update_message, msg);
}

auto interaction_handler::handle_end(const dpp::button_click_t &ev, panel_session &sess) -> void
{
	sess.active = false;
	session_mgr_->remove_session(sess.panel_id);

	dpp::message msg;
	msg.set_content(std::format("🔒 面板已由 {} 關閉", util::mention(sess.owner_id)));
	return ev.reply(dpp::ir_update_message, msg);
}

auto interaction_handler::handle_user_select(const dpp::select_click_t &ev, panel_session &sess) -> void
{
	sess.selected_users.clear();

	for (const auto &val : ev.values) {
		uint64_t id = 0;
		auto [ptr, ec] = std::from_chars(val.data(), val.data() + val.size(), id);
		if (ec == std::errc{}) {
			sess.selected_users.push_back(dpp::snowflake{id});
		}
	}

	sess.formed_teams.clear();

	auto users = match_svc_->list_users();
	auto msg = panel_bld_->build_formteams_panel(sess, users);
	return ev.reply(dpp::ir_update_message, msg);
}

auto interaction_handler::handle_match_choose(const dpp::select_click_t &ev, panel_session &sess) -> void
{
	if (ev.values.empty()) {
		return ui::message_builder::reply_error(ev, "未選擇任何場次");
	}

	std::size_t hist_idx = 0;
	auto [ptr, ec] = std::from_chars(ev.values[0].data(), ev.values[0].data() + ev.values[0].size(), hist_idx);

	if (ec != std::errc{}) {
		return ui::message_builder::reply_error(ev, "場次索引格式錯誤");
	}

	auto match = match_svc_->match_by_index(hist_idx);
	if (!match) {
		return ui::message_builder::reply_error(ev, "找不到該場比賽");
	}

	sess.selected_match_index = hist_idx;
	sess.formed_teams = match->teams;
	sess.num_teams = static_cast<int>(match->teams.size());

	constexpr int kMaxRecent = 8;
	auto indexed_matches = match_svc_->recent_indexed_matches(kMaxRecent);

	auto msg = panel_bld_->build_sethistory_panel(sess, indexed_matches);
	return ev.reply(dpp::ir_update_message, msg);
}

} // namespace terry
