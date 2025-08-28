/**
 * @brief
 * Responsibilities:
 *   - Dispatch slash-commands (help/adduser/removeuser/listusers/formteams/history)
 *   - Handle panel interactions (buttons & select menu)
 *   - Build the dynamic "team assignment" control panel message
 *
 * Design notes:
 *   - We use custom_id in the shape `panel:<panel_id>:<action>[:arg]`
 *   - All interactions are restricted to the panel owner
 *   - Parsing of integers uses std::from_chars (no exceptions, fast) — we check
 *		`ec == std::errc{}` to indicate success and verify full-string consumption.
 *   - The panel can be "ended" which removes the components and invalidates the session.
 */

#include "command_handler.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <format>
#include <iterator>
#include <random>
#include <unordered_set>
#include <utility>

namespace terry::bot {

/**
 * @brief clean the expired sessions.
 */
void command_handler::keep_recent_sessions_(std::size_t max_sessions)
{
	if (sessions_.size() <= max_sessions)
		return;
	std::vector<std::pair<std::string, std::chrono::steady_clock::time_point>> order;
	order.reserve(sessions_.size());
	for (const auto &kv : sessions_) {
		const auto &s = kv.second;
		const auto tp = (s.last_touch == std::chrono::steady_clock::time_point{} ? s.created_at : s.last_touch);
		order.emplace_back(kv.first, tp);
	}
	std::ranges::sort(order, {}, &decltype(order)::value_type::second);
	const std::size_t need = sessions_.size() - max_sessions;
	for (std::size_t i = 0; i < need; ++i)
		sessions_.erase(order[i].first);
}

std::string command_handler::make_token_()
{
	static std::mt19937_64 rng{std::random_device{}()};
	std::uniform_int_distribution<uint64_t> dist;
	return std::format("{:x}", dist(rng));
}

/**
 * @brief Handle a slash command by name and dispatch to the right subcommand.
 * 				Unknown commands are answered with an ephemeral error.
 */
void command_handler::on_slash(const dpp::slashcommand_t &ev)
{
	auto name = ev.command.get_command_name();
	if (name == "help")
		return cmd_help_(ev);
	if (name == "adduser")
		return cmd_adduser_(ev);
	if (name == "removeuser")
		return cmd_removeuser_(ev);
	if (name == "listusers")
		return cmd_listusers_(ev);
	if (name == "formteams")
		return cmd_formteams_(ev);
	if (name == "history")
		return cmd_history_(ev);
	if (name == "setwinner")
		return cmd_setwinner_(ev);

	reply_err(ev, text::unknown_command);
}

/**
 * @brief Handle button clicks from both panels.
 *        custom_id format: panel:<panel_id>:<action>[:arg]
 *        Actions:
 *          - assign      → (re)form teams from the current selection (formteams only)
 *          - newmatch    → append a match entry from current teams without changing ratings (formteams only)
 *          - win:<index> → set winners for an existing match (setwinner only; index is 0-based)
 *          - end         → close the panel (remove components and erase the session)
 */
void command_handler::on_button(const dpp::button_click_t &ev)
{
	const std::string_view cid = ev.custom_id;

	// Only accept our own widgets
	if (!cid.starts_with("panel:")) [[unlikely]] {
		reply_err(ev, text::unsupported_button);
		return;
	}

	// Split "panel:<panel_id>:<action>[:arg]"
	const auto rest = cid.substr(6);
	const auto p1 = rest.find(':');
	if (p1 == std::string::npos) [[unlikely]]
		return;

	const auto pid = rest.substr(0, p1);
	const auto action = rest.substr(p1 + 1);

	auto it = sessions_.find(std::string{pid});
	if (it == sessions_.end()) [[unlikely]] {
		reply_err(ev, text::panel_expired);
		return;
	}
	auto &sess = it->second;

	if (!sess.active) [[unlikely]] {
		reply_err(ev, text::panel_expired);
		return;
	}
	if (ev.command.usr.id != sess.owner_id) [[unlikely]] {
		reply_err(ev, text::panel_owner_only);
		return;
	}

	// Touch for LRU retention
	sess.last_touch = std::chrono::steady_clock::now();

	// --- assign (formteams only) ---
	if (action == "assign") {
		if (sess.kind != panel_kind::formteams) [[unlikely]] {
			reply_err(ev, text::unknown_panel_action);
			return;
		}

		// Basic feasibility (uneven team sizes allowed, but at least one member overall)
		const int P = static_cast<int>(sess.selected.size());
		const int T = sess.num_teams;

		if (P == 0) [[unlikely]] {
			reply_err(ev, terry::bot::text::need_one_per_team);
			return;
		}
		if (T > P) [[unlikely]] {
			reply_err(ev, terry::bot::text::teams_too_much);
			return;
		}

		// (Re)form teams
		if (auto res = tm_.form_teams(sess.selected, T); !res) [[unlikely]] {
			reply_err(ev, res.error().message);
			return;
		}
		else {
			sess.last_teams = std::move(*res);
		}

		ev.reply(dpp::ir_update_message, build_formteams_panel_msg_(sess));
		return;
	}

	// --- newmatch (formteams only) ---
	if (action == "newmatch") {
		if (sess.kind != panel_kind::formteams) [[unlikely]] {
			reply_err(ev, text::unknown_panel_action);
			return;
		}
		if (sess.last_teams.empty()) [[unlikely]] {
			reply_err(ev, "尚未分配隊伍，請先點「分配」");
			return;
		}

		const auto now = std::chrono::time_point_cast<timestamp::duration>(std::chrono::system_clock::now());
		if (auto a = tm_.add_match(sess.last_teams, now); !a) [[unlikely]] {
			reply_err(ev, a.error().message);
			return;
		}
		if (auto sres = tm_.save(); !sres) [[unlikely]] {
			ev.reply(dpp::message(std::string(text::err_prefix) + sres.error().message).set_flags(dpp::m_ephemeral));
			return;
		}

		auto m = build_formteams_panel_msg_(sess);
		m.set_content("🆕 已新增一場比賽到對戰紀錄（待 `/setwinner` 設定勝負）");
		ev.reply(dpp::ir_update_message, m);
		return;
	}

	// --- win:<index> (setwinner only) ---
	if (action.starts_with("win:")) {
		if (sess.kind != panel_kind::setwinner) [[unlikely]] {
			reply_err(ev, text::unknown_panel_action);
			return;
		}

		// parse team index
		int idx{};
		{
			const auto sv = action.substr(4);
			int val{};
			auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), val, 10);
			if (ec != std::errc{} || ptr != sv.data() + sv.size()) [[unlikely]] {
				reply_err(ev, text::invalid_team_index);
				return;
			}
			idx = val;
		}

		if (!sess.chosen) [[unlikely]] {
			reply_err(ev, "目前沒有選定的場次可更新");
			return;
		}
		if (idx < 0 || idx >= static_cast<int>(sess.last_teams.size())) [[unlikely]] {
			reply_err(ev, text::invalid_team_index);
			return;
		}

		// 1) snapshot old winners for rollback
		auto before = tm_.match_by_index(*sess.chosen);
		std::vector<int> old_winners = before ? before->winning_teams : std::vector<int>{};

		// 2) set new winners in-memory
		if (auto r = tm_.set_match_winner_by_index(*sess.chosen, std::vector<int>{idx}); !r) [[unlikely]] {
			reply_err(ev, r.error().message);
			return;
		}

		// 3) recompute all ratings/W-L from baseline; if fails, rollback winners
		if (auto r = tm_.recompute_all_from_history(); !r) [[unlikely]] {
			// rollback winners to keep state consistent
			if (auto rb = tm_.set_match_winner_by_index(*sess.chosen, std::move(old_winners)); !rb) {
				ev.reply(dpp::message("❌ 重算失敗且回滾也失敗：" + rb.error().message).set_flags(dpp::m_ephemeral));
			}
			else {
				ev.reply(dpp::message("❌ 重算失敗，已還原勝負：" + r.error().message).set_flags(dpp::m_ephemeral));
			}
			return;
		}

		// 4) single save for both users + matches
		if (auto s = tm_.save(); !s) [[unlikely]] {
			ev.reply(dpp::message("❌ 存檔失敗：" + s.error().message).set_flags(dpp::m_ephemeral));
			return;
		}

		auto m = build_setwinner_panel_msg_(sess);
		m.set_content("📝 已更新勝方為：隊伍 " + std::to_string(idx + 1) + "；已重算隱分與戰績並存檔");
		ev.reply(dpp::ir_update_message, m);
		return;
	}

	// --- end (both panels) ---
	if (action == "end") [[unlikely]] {
		sess.active = false;
		auto m = (sess.kind == panel_kind::formteams) ? build_formteams_panel_msg_(sess) : build_setwinner_panel_msg_(sess);
		m.components.clear(); // disable all interactive components (close the panel)
		m.set_content("🔒 面板已由 <@" + std::to_string(static_cast<uint64_t>(std::as_const(sess.owner_id))) + "> 關閉");
		ev.reply(dpp::ir_update_message, m);
		sessions_.erase(it);
		return;
	}

	// Fallback
	reply_err(ev, text::unknown_panel_action);
}

/**
 * @brief Handle select-menu changes.
 *        - action == "select" for the formteams panel (choose participants)
 *        - action == "choose" for the setwinner panel (pick a persisted match by its absolute history index)
 */
void command_handler::on_select(const dpp::select_click_t &ev)
{
	const std::string_view cid = ev.custom_id;
	if (!cid.starts_with("panel:")) [[unlikely]] {
		reply_err(ev, text::unsupported_select);
		return;
	}

	// "panel:<panel_id>:<action>"
	const auto rest = cid.substr(6);
	const auto p1 = rest.find(':');
	if (p1 == std::string::npos) [[unlikely]]
		return;

	const auto pid = rest.substr(0, p1);
	const auto action = rest.substr(p1 + 1);

	auto it = sessions_.find(std::string{pid});
	if (it == sessions_.end()) [[unlikely]] {
		reply_err(ev, text::panel_expired);
		return;
	}
	auto &sess = it->second;

	if (!sess.active) [[unlikely]] {
		reply_err(ev, text::panel_expired);
		return;
	}
	if (ev.command.usr.id != sess.owner_id) [[unlikely]] {
		reply_err(ev, text::panel_owner_only);
		return;
	}

	// Touch for LRU retention
	sess.last_touch = std::chrono::steady_clock::now();

	// --- formteams: member selection ---
	if (action == "select") {
		if (sess.kind != panel_kind::formteams) [[unlikely]] {
			reply_err(ev, text::unsupported_select);
			return;
		}

		// Replace selection from the select menu values
		sess.selected.clear();
		for (const auto &v : ev.values) {
			uint64_t id{};
			const auto sv = std::string_view{v};
			auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), id, 10);
			if (ec == std::errc{} && ptr == sv.data() + sv.size()) [[likely]] {
				sess.selected.push_back(user_id{id});
			}
			else {
				reply_err(ev, "選單值格式錯誤");
				return;
			}
		}

		// Clear any previously generated teams until "assign" is pressed again
		sess.last_teams.clear();
		ev.reply(dpp::ir_update_message, build_formteams_panel_msg_(sess));
		return;
	}

	// --- setwinner: choose a persisted match by absolute history index ---
	if (action == "choose") {
		if (sess.kind != panel_kind::setwinner) [[unlikely]] {
			reply_err(ev, text::unsupported_select);
			return;
		}
		if (ev.values.empty()) [[unlikely]] {
			reply_err(ev, "未選擇任何場次");
			return;
		}

		// Parse history index from option value
		std::size_t hist_idx{};
		{
			const auto sv = std::string_view{ev.values.front()};
			auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), hist_idx, 10);
			if (ec != std::errc{} || ptr != sv.data() + sv.size()) [[unlikely]] {
				reply_err(ev, "場次索引格式錯誤");
				return;
			}
		}

		// Fetch the match; update the panel snapshot
		if (auto mr = tm_.match_by_index(hist_idx); !mr) {
			ev.reply(dpp::message("❌ 找不到該場比賽").set_flags(dpp::m_ephemeral));
			return;
		}
		else {
			sess.chosen = hist_idx;
			sess.num_teams = static_cast<int>(mr->teams.size());
			sess.last_teams = mr->teams;
		}

		ev.reply(dpp::ir_update_message, build_setwinner_panel_msg_(sess));
		return;
	}

	// Unknown select action
	reply_err(ev, text::unsupported_select);
}

/**
 * @brief Declare all guild commands the bot provides. These are registered in on_ready().
 */
std::vector<dpp::slashcommand> command_handler::commands(dpp::snowflake bot_id)
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
	cmds.emplace_back("setwinner", "開啟/切換最近 8 場的勝負紀錄面板", bot_id);

	return cmds;
}

/**
 * @brief Reply with an embedded help panel listing all supported commands and flows.
 */
void command_handler::cmd_help_(const dpp::slashcommand_t &ev)
{
	dpp::embed e;
	e.set_title("指令說明 / Help");

	// User management
	e.add_field("使用者管理",
							"• `/adduser <user> <point>` 新增或更新成員分數\n"
							"• `/removeuser <user>` 移除成員\n"
							"• `/listusers` 顯示使用者清單",
							false);

	// Team formation panel
	e.add_field("分隊面板",
							"• `/formteams <teams>` 開啟面板，預設為 2 組\n"
							"• 於下拉選單勾選參與者（Discord 限制：列表最多 25 人）\n"
							"• 按 **「分配」** 產生/重抽隊伍\n"
							"• 按 **「新增場次」** 把目前隊伍**加入對戰紀錄**（先不標勝負）\n"
							"• 按 **「結束」** 關閉面板\n"
							"• 之後用 **`/setwinner`** 選擇最近 8 場並**紀錄/更改勝負**",
							false);

	// Records
	e.add_field("戰績紀錄", "• `/history [count]` 顯示最近戰績，預設為 5 筆記錄\n", false);

	ev.reply(dpp::message().add_embed(e));
}

/**
 * @brief Add or update a user's point.
 * 			  We also try to capture a snapshot of the username/global_name for display.
 * 			  Persist the change to disk via team_manager::save().
 */
void command_handler::cmd_adduser_(const dpp::slashcommand_t &ev)
{
	dpp::snowflake uid = std::get<dpp::snowflake>(ev.get_parameter("user"));
	double point = std::get<double>(ev.get_parameter("point"));

	// try to get the user name from the slash command
	std::string username_snapshot;
	if (auto it = ev.command.resolved.users.find(uid); it != ev.command.resolved.users.end()) [[likely]] {
		const dpp::user &ru = it->second;
		if (!ru.global_name.empty()) [[likely]] // the new username format, like mes_0903
			username_snapshot = ru.global_name;
		else if (!ru.username.empty()) // the old username format, like Mes#0903
			username_snapshot = ru.username;
	}

	if (username_snapshot.empty()) [[unlikely]] {
		// try to find the username from DPP cache
		if (auto u = dpp::find_user(uid)) {
			if (!u->username.empty())
				username_snapshot = u->username;
			else if (!u->global_name.empty())
				username_snapshot = u->global_name;
		}
	}

	if (username_snapshot.empty()) [[unlikely]] {
		// try to find the username from our own DB
		const auto all = tm_.list_users(user_sort::by_name_asc);
		if (auto it = std::find_if(all.begin(), all.end(), [uid](const user &x) { return x.id == uid; }); it != all.end() && !it->username.empty()) {
			username_snapshot = it->username;
		}
	}

	// update/insert the user data
	if (auto res = tm_.upsert_user(uid, username_snapshot, point); !res) [[unlikely]] {
		ev.reply(dpp::message(std::string(text::err_prefix) + res.error().message).set_flags(dpp::m_ephemeral));
		return;
	}

	// save into the users.json file
	if (auto sres = tm_.save(); !sres) [[unlikely]] {
		ev.reply(dpp::message(std::string(text::err_prefix) + sres.error().message).set_flags(dpp::m_ephemeral));
	}

	ev.reply(dpp::message(std::string(text::ok_prefix) + "新增/更新使用者 <@" + std::to_string(static_cast<uint64_t>(std::as_const(uid))) + "> 的分數為 " +
												std::format("{:.3f}", point)));
}

/**
 * @brief Remove a user from the registry by Discord snowflake ID, then save to disk.
 */
void command_handler::cmd_removeuser_(const dpp::slashcommand_t &ev)
{
	dpp::snowflake uid = std::get<dpp::snowflake>(ev.get_parameter("user"));
	if (auto res = tm_.remove_user(uid); !res) [[unlikely]] {
		ev.reply(dpp::message(std::string(text::err_prefix) + res.error().message).set_flags(dpp::m_ephemeral));
	}
	else {
		if (auto sres = tm_.save(); !sres) [[unlikely]] { // save to users.json
			ev.reply(dpp::message(std::string(text::err_prefix) + sres.error().message).set_flags(dpp::m_ephemeral));
		}
		ev.reply(dpp::message("🗑️ 移除使用者 <@" + std::to_string(static_cast<uint64_t>(std::as_const(uid))) + ">"));
	}
}

/**
 * @brief List all registered users sorted by point (descending).
 * 				Shows a simple win-rate summary when available.
 */
void command_handler::cmd_listusers_(const dpp::slashcommand_t &ev)
{
	dpp::embed e;
	e.set_title("使用者清單");

	std::string desc;
	const auto users = tm_.list_users(user_sort::by_point_desc);
	for (const auto &u : users) {
		const int rate = (u.games > 0) ? static_cast<int>(std::lround((u.wins * 100.0) / u.games)) : 0;
		std::format_to(std::back_inserter(desc), "<@{}> **({:.3f} CP)** — 勝率 {}% ({}/{})\n", static_cast<uint64_t>(u.id), u.point, rate, u.wins, u.games);
	}

	if (users.empty()) [[unlikely]] {
		reply_err(ev, text::no_users);
		return;
	}

	e.set_description(std::move(desc));
	ev.reply(dpp::message().add_embed(e));
}

/**
 * @brief Start a new panel session for the caller:
 * 					- Generate a unique token as panel_id
 * 					- Restrict control to the panel owner
 * 					- Render the initial panel (no teams yet; only selection and action buttons)
 */
void command_handler::cmd_formteams_(const dpp::slashcommand_t &ev)
{
	int n = 2; // default number of teams
	{
		auto p = ev.get_parameter("teams");
		if (std::holds_alternative<int64_t>(p))
			n = static_cast<int>(std::get<int64_t>(p));
	}
	if (n < 1) [[unlikely]] {
		reply_err(ev, text::teams_must_positive);
		return;
	}

	{
		const auto all = tm_.list_users(user_sort::by_name_asc);
		if (all.empty()) [[unlikely]] {
			reply_err(ev, text::no_registered_users);
			return;
		}
		if (static_cast<std::size_t>(n) > all.size()) [[unlikely]] {
			reply_err(ev, text::teams_too_much);
			return;
		}
	}

	panel_session sess;
	sess.panel_id = make_token_();
	sess.guild_id = ev.command.guild_id;
	sess.channel_id = ev.command.channel_id;
	sess.owner_id = ev.command.usr.id;
	sess.num_teams = n;
	sess.created_at = std::chrono::steady_clock::now();
	sess.last_touch = sess.created_at;
	sess.kind = panel_kind::formteams;
	sessions_.emplace(sess.panel_id, sess);

	dpp::message msg = build_formteams_panel_msg_(sess);
	msg.set_content("👑 分配面板擁有者：<@" + std::to_string(static_cast<uint64_t>(std::as_const(sess.owner_id))) + "> — 只有擁有者可以操作此面板");
	ev.reply(msg);
}

/**
 * @brief Show the N most recent matches from persistent history.
 *				Each entry renders winners (if recorded), timestamp, and per-team member tags.
 */
void command_handler::cmd_history_(const dpp::slashcommand_t &ev)
{
	int count = 5;
	auto p = ev.get_parameter("count");
	if (std::holds_alternative<int64_t>(p))
		count = static_cast<int>(std::get<int64_t>(p));

	auto recents = tm_.recent_matches(count);

	dpp::embed e;
	e.set_title("近期對戰");
	std::string desc;

	if (recents.empty()) [[unlikely]] {
		reply_err(ev, text::no_history);
		return;
	}
	else {
		int idx = 1;
		for (const auto &match : recents) {
			std::string winners;
			if (!match.winning_teams.empty()) [[likely]] {
				winners.reserve(match.winning_teams.size() * 8);
				winners += "勝利隊伍：";
				for (size_t i = 0; i < match.winning_teams.size(); ++i) {
					if (i)
						winners += "、";
					winners += "隊伍 " + std::to_string(match.winning_teams[i] + 1);
				}
			}
			else {
				winners = "未紀錄勝方";
			}
			std::format_to(std::back_inserter(desc), "**比賽 #{}（{}）**\n", idx++, winners);
			desc += format_timestamp(match.when);
			desc.push_back('\n');

			// team lines: prefix trophy for winners, spaces for others
			std::unordered_set<int> winset(match.winning_teams.begin(), match.winning_teams.end());
			const std::string TROPHY_PREFIX = std::string(text::trophy);
			const std::string LOSE_PREFIX = std::string(text::runner_up);

			for (size_t ti = 0; ti < match.teams.size(); ++ti) {
				const bool is_winner = winset.contains(static_cast<int>(ti));
				const std::string &prefix = is_winner ? TROPHY_PREFIX : LOSE_PREFIX;
				std::format_to(std::back_inserter(desc), "{}隊伍 {}：", prefix, ti + 1);

				bool first = true;
				for (const auto &mem : match.teams[ti].members) {
					if (!first)
						desc += "、";
					std::format_to(std::back_inserter(desc), "<@{}>", static_cast<uint64_t>(mem.id));
					first = false;
				}
				desc.push_back('\n');
			}

			desc.push_back('\n');
		}
	}

	e.set_description(std::move(desc));
	ev.reply(dpp::message().add_embed(e));
}

void command_handler::cmd_setwinner_(const dpp::slashcommand_t &ev)
{
	constexpr int kMaxRecent = 8;
	auto recents = tm_.recent_matches_with_index(kMaxRecent); // oldest → newest
	if (recents.empty()) {
		ev.reply(dpp::message("❌ 目前沒有任何對戰紀錄（請先在分隊面板中分配後按「新增場次」）").set_flags(dpp::m_ephemeral));
		return;
	}

	panel_session sess;
	sess.panel_id = make_token_();
	sess.guild_id = ev.command.guild_id;
	sess.channel_id = ev.command.channel_id;
	sess.owner_id = ev.command.usr.id;
	// initial render uses the most recent one (the last element in recents)
	const auto &[hist_idx, match] = recents.back();
	sess.num_teams = static_cast<int>(match.teams.size());
	sess.last_teams = match.teams;
	sess.created_at = std::chrono::steady_clock::now();
	sess.last_touch = sess.created_at;
	sess.kind = panel_kind::setwinner;
	sess.chosen = hist_idx; // absolute history index

	sessions_.emplace(sess.panel_id, sess);
	keep_recent_sessions_();

	dpp::message msg = build_setwinner_panel_msg_(sess);
	msg.set_content("🏁 勝負紀錄面板（可從下拉清單切換最近 8 場）");
	ev.reply(msg);
}

/**
 * @brief Build the dynamic control panel message:
 *					- An embed showing the current team count, selected users, and last formed teams
 *					- A select menu (multi-select) to choose participants
 *					- Buttons: "Assign" (primary), "End" (danger), and winner buttons (after teams exist)
 *        "Assign" is auto-disabled until we have at least one member per team.
 */
dpp::message command_handler::build_formteams_panel_msg_(const panel_session &sess) const
{
	dpp::message msg;
	dpp::embed e;
	e.set_title("分配隊伍面板");

	const auto db_users = tm_.list_users(user_sort::by_name_asc);
	std::string body;
	std::format_to(std::back_inserter(body), "隊伍數量： **{}**\n", sess.num_teams);
	const bool can_assign = sess.selected.size() >= static_cast<size_t>(sess.num_teams);

	// Participants (rendered as Discord mentions)
	if (!sess.selected.empty()) [[likely]] {
		std::format_to(std::back_inserter(body), "參與者 ({})： ", sess.selected.size());
		for (auto id : sess.selected)
			std::format_to(std::back_inserter(body), "<@{}> ", static_cast<std::uint64_t>(std::as_const(id)));
		body += "\n\n";

		// Re-check the quantity once
		if (!can_assign) [[unlikely]]
			std::format_to(std::back_inserter(body), "⚠️ 需至少選擇 {} 名玩家（每隊 1 人）才能分配。\n", sess.num_teams);
	}
	else {
		body += "*於底下的清單中選取要參與隊伍分配的使用者*\n";
	}

	if (!sess.last_teams.empty()) [[likely]] {
		const auto [min_it, max_it] = std::ranges::minmax_element(sess.last_teams, {}, &team::total_point);
		const double minp = (min_it != sess.last_teams.end()) ? min_it->total_point : 0.0; // verify whether the iterator is valid, if not, default to 0.0
		const double maxp = (max_it != sess.last_teams.end()) ? max_it->total_point : 0.0; // verify whether the iterator is valid, if not, default to 0.0

		for (size_t i = 0; i < sess.last_teams.size(); ++i) {
			const auto &team = sess.last_teams[i];
			std::format_to(std::back_inserter(body), "隊伍 {}（總分數 {:.3f} CP）：", i + 1, team.total_point);
			bool first = true;
			for (const auto &m : team.members) {
				if (!first)
					body += "、";
				std::format_to(std::back_inserter(body), "<@{}>", static_cast<uint64_t>(m.id));
				first = false;
			}
			body.push_back('\n');
		}
		std::format_to(std::back_inserter(body), "最大分數差：{:.3f} CP\n", (maxp - minp));
	}

	e.set_description(std::move(body));
	msg.add_embed(e);

	dpp::component row1;
	dpp::component menu; // select menu, would call "on_select" during interaction
	menu.set_type(dpp::cot_selectmenu);
	menu.set_id("panel:" + sess.panel_id + ":select");
	menu.set_placeholder("選擇參與分配的成員 (可複選)");

	size_t max_opts = std::min<size_t>(db_users.size(), 25); // the options in the discord select menu have a hard limit of 25.
	std::unordered_set<uint64_t> chosen;										 // the chosen member in the last interaction
	for (auto id : sess.selected)
		chosen.insert(static_cast<std::uint64_t>(std::as_const(id)));

	// generate the select menu
	for (size_t i = 0; i < max_opts; ++i) {
		const auto &u = db_users[i];
		bool def = chosen.contains(static_cast<uint64_t>(std::as_const(u.id)));
		std::string label = u.username.empty() ? ("<@" + std::to_string(static_cast<uint64_t>(std::as_const(u.id))) + ">") : u.username;
		dpp::select_option opt(label + " (" + std::format("{:.3f}", u.point) + ")", std::to_string(static_cast<uint64_t>(std::as_const(u.id))));
		if (def)
			opt.set_default(true);

		menu.add_select_option(std::move(opt));
	}

	menu.set_min_values(0);
	menu.set_max_values((int)max_opts);
	row1.add_component(menu);
	msg.add_component(row1);

	dpp::component row2;
	row2.add_component(dpp::component()
												 .set_type(dpp::cot_button)
												 .set_style(dpp::cos_primary)
												 .set_label("分配")
												 .set_id("panel:" + sess.panel_id + ":assign")
												 .set_disabled(!can_assign));
	row2.add_component(dpp::component()
												 .set_type(dpp::cot_button)
												 .set_style(dpp::cos_success)
												 .set_label("新增場次")
												 .set_id("panel:" + sess.panel_id + ":newmatch")
												 .set_disabled(sess.last_teams.empty()));
	row2.add_component(dpp::component().set_type(dpp::cot_button).set_style(dpp::cos_danger).set_label("結束").set_id("panel:" + sess.panel_id + ":end"));
	msg.add_component(row2);

	return msg;
}

dpp::message command_handler::build_setwinner_panel_msg_(const panel_session &sess) const
{
	dpp::message msg;
	dpp::embed e;
	e.set_title("勝負紀錄面板");

	// Load recent persisted matches (oldest → newest among the slice)
	constexpr int kMaxRecent = 8;
	auto recents = tm_.recent_matches_with_index(kMaxRecent);

	// Resolve which match to render:
	// - Prefer sess.chosen (absolute history index)
	// - Otherwise fallback to most recent of the slice (if any)
	std::optional<std::size_t> chosen_hist_idx = sess.chosen;
	std::optional<match_record> chosen_mr;
	if (chosen_hist_idx) {
		chosen_mr = tm_.match_by_index(*chosen_hist_idx);
	}
	if (!chosen_mr && !recents.empty()) {
		chosen_hist_idx = recents.back().first;
		chosen_mr = recents.back().second;
	}

	// ----- Embed body -----
	std::string body;
	if (chosen_mr) {
		const auto &mr = *chosen_mr;

		std::format_to(std::back_inserter(body), "建立時間：**{}**\n\n", format_timestamp(mr.when));

		// Compute totals from CURRENT users_ ratings
		if (!mr.teams.empty()) {
			std::vector<double> totals;
			totals.reserve(mr.teams.size());
			for (const auto &t : mr.teams) {
				double sum = 0.0;
				for (const auto &m : t.members)
					if (const auto *u = tm_.find_user(m.id))
						sum += u->point;
				totals.push_back(sum);
			}
			auto [mn_it, mx_it] = std::minmax_element(totals.begin(), totals.end());
			const double minp = (mn_it != totals.end()) ? *mn_it : 0.0;
			const double maxp = (mx_it != totals.end()) ? *mx_it : 0.0;

			for (std::size_t i = 0; i < mr.teams.size(); ++i) {
				const auto &t = mr.teams[i];
				// re-emit members
				std::format_to(std::back_inserter(body), "隊伍 {}（總分 {:.3f} CP）：", i + 1, totals[i]);
				bool first = true;
				for (const auto &mem : t.members) {
					if (!first)
						body += "、";
					std::format_to(std::back_inserter(body), "<@{}>", static_cast<uint64_t>(mem.id));
					first = false;
				}
				body.push_back('\n');
			}
			std::format_to(std::back_inserter(body), "\n最大分差：{:.3f} CP\n", (maxp - minp));
		}
		else {
			body += "（此場沒有隊伍資料）\n";
		}

		// Show current winners if any
		if (!mr.winning_teams.empty()) {
			body += "\n**目前標記勝方**：";
			bool first = true;
			for (int w : mr.winning_teams) {
				if (!first)
					body += "、";
				body += "隊伍 " + std::to_string(w + 1);
				first = false;
			}
			body.push_back('\n');
		}
	}
	else {
		body += "（沒有可顯示的場次）\n";
	}

	e.set_description(std::move(body));
	msg.add_embed(e);

	// ----- Dropdown: recent 8 matches from persistent history (oldest → newest) -----
	{
		dpp::component row;
		dpp::component sel;
		sel.set_type(dpp::cot_selectmenu).set_id("panel:" + sess.panel_id + ":choose").set_min_values(1).set_max_values(1).set_placeholder("選擇要設定勝負的場次");

		for (std::size_t i = 0; i < recents.size(); ++i) {
			const auto &pair = recents[recents.size() - 1 - i]; // reverse
			const auto &[hist_idx, match] = pair;
			dpp::select_option opt;
			opt.set_label(std::format("#{} {}", i + 1, format_timestamp(match.when)));
			opt.set_value(std::to_string(hist_idx));
			if (chosen_hist_idx && *chosen_hist_idx == hist_idx)
				opt.set_default(true);
			sel.add_select_option(opt);
		}

		row.add_component(sel);
		msg.add_component(row);
	}

	// ----- Winner buttons (one per team) -----
	if (chosen_mr && !chosen_mr->teams.empty()) {
		const auto &mr = *chosen_mr;
		dpp::component row;
		int in_row = 0;

		for (std::size_t i = 0; i < mr.teams.size(); ++i) {
			if (in_row == 5) {
				msg.add_component(row);
				row = dpp::component{};
				in_row = 0;
			}

			row.add_component(dpp::component()
														.set_type(dpp::cot_button)
														.set_style(dpp::cos_success)
														.set_label("隊伍 " + std::to_string(i + 1) + " 勝")
														.set_id("panel:" + sess.panel_id + ":win:" + std::to_string(i)));
			++in_row;
		}

		if (in_row)
			msg.add_component(row);
	}

	// ----- End button -----
	{
		dpp::component row;
		row.add_component(dpp::component().set_type(dpp::cot_button).set_style(dpp::cos_danger).set_label("結束").set_id("panel:" + sess.panel_id + ":end"));
		msg.add_component(row);
	}

	return msg;
}

} // namespace terry::bot
