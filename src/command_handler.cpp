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
#include <random>
#include <sstream>
#include <unordered_set>

namespace terry::bot {

std::string command_handler::make_token()
{
	static std::mt19937_64 rng{std::random_device{}()};
	std::uniform_int_distribution<uint64_t> dist;
	std::ostringstream oss;
	oss << std::hex << dist(rng);
	return oss.str();
}

/**
 * @brief Handle a slash command by name and dispatch to the right subcommand.
 * 				Unknown commands are answered with an ephemeral error.
 */
void command_handler::on_slash(const dpp::slashcommand_t &ev)
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

	reply_err(ev, text::unknown_command);
}

/**
 * @brief Handle button clicks originating from the assignment panel.
 * 				Expected custom_id format: panel:<panel_id>:<action>[:arg]
 * 				Actions:
 * 				  - assign      → (re)form teams from the current selection
 * 				  - win:<index> → record winners for the last formed teams
 * 				  - end         → close the panel (remove components, drop session)
 */
void command_handler::on_button(const dpp::button_click_t &ev)
{
	const auto &cid = ev.custom_id;

	// Accept only interactions originating from our panel widgets
	// (custom_id must start with "panel:")
	if (!starts_with(cid, "panel:")) {
		reply_err(ev, text::unsupported_button);
		return;
	}

	// custom_id layout: panel:<panel_id>:<action>[:arg]
	auto rest = cid.substr(6);
	auto p1 = rest.find(':');
	if (p1 == std::string::npos)
		return;
	auto pid = rest.substr(0, p1);
	auto action = rest.substr(p1 + 1);

	auto it = sessions_.find(pid);
	if (it == sessions_.end()) {
		reply_err(ev, text::panel_expired);
		return;
	}

	auto &sess = it->second;
	if (!sess.active) {
		reply_err(ev, text::panel_expired);
		return;
	}

	if (ev.command.usr.id != sess.owner_id) {
		reply_err(ev, text::panel_owner_only);
		return;
	}

	if (action == "assign") {
		// Server-side feasibility: at least one per team; uneven sizes allowed.
		const int P = static_cast<int>(sess.selected.size());
		const int T = sess.num_teams;

		if (P == 0) {
			reply_err(ev, "Please select participants from the list below first.");
			return;
		}
		if (T > P) {
			reply_err(ev, "Cannot form teams: team count (" + std::to_string(T) + ") exceeds participant count (" + std::to_string(P) + ").");
			return;
		}

		//  seed to keep results varied per click.
		const uint64_t seed = static_cast<uint64_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count());

		// Form teams with uneven sizes allowed (minimize total-power spread).
		sess.last_teams = tm_.form_teams(sess.selected, T, seed);

		if (sess.last_teams.empty()) {
			reply_err(ev, "Team assignment failed. Please re-check participants and team count.");
			return;
		}

		ev.reply(dpp::ir_update_message, build_panel_message(sess));
		return;
	}

	if (starts_with(action, "win:")) {
		int idx{};
		{
			auto s = action.substr(4);
			auto sv = std::string_view{s};
			int val{};
			auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), val, 10);
			if (ec != std::errc{} || ptr != sv.data() + sv.size()) {
				reply_err(ev, text::invalid_team_index);
				return;
			}
			idx = val;
		}

		if (idx < 0 || idx >= (int)sess.last_teams.size()) {
			reply_err(ev, text::invalid_team_index);
			return;
		}

		if (auto res = tm_.record_match(sess.last_teams, std::vector<int>{idx}); !res) {
			reply_err(ev, res.error().message);
		}
		else {
			if (auto sres = tm_.save(); !sres) { /* ignore */
			}
			refresh_session_snapshot(sess); // sync new powers into panel snapshot
			auto m = build_panel_message(sess);
			// Show which team won and the K-factor used after rating updates.
			m.set_content("✅ 已記錄勝利隊伍：隊伍 " + std::to_string(idx + 1) + "，並更新了戰力（K = " + std::format("{:.3f}", tm_.get_k_factor()) + "）");
			ev.reply(dpp::ir_update_message, m);
		}
		return;
	}

	if (action == "end") {
		sess.active = false;
		auto m = build_panel_message(sess);
		m.components.clear(); // disable all interactive components (close the panel)
		m.set_content("🔒 面板已由 <@" + std::to_string((uint64_t)sess.owner_id) + "> 關閉");
		ev.reply(dpp::ir_update_message, m);
		sessions_.erase(it);
		return;
	}

	reply_err(ev, text::unknown_panel_action);
}

/**
 * @brief Handle select-menu changes originating from the assignment panel.
 * 			  The menu lists registered users; selections populate `sess.selected`.
 * 			  After updating the selection, any previously generated teams are cleared
 * 			  until the user presses the "assign" button again.
 */
void command_handler::on_select(const dpp::select_click_t &ev)
{
	const auto &cid = ev.custom_id;

	if (!starts_with(cid, "panel:")) {
		reply_err(ev, text::unsupported_select);
		return;
	}

	// custom_id layout for the select menu: panel:<panel_id>:select
	auto rest = cid.substr(6);
	auto p1 = rest.find(':');
	if (p1 == std::string::npos)
		return;
	auto pid = rest.substr(0, p1);
	auto action = rest.substr(p1 + 1);
	if (action != "select")
		return;

	auto it = sessions_.find(pid);
	if (it == sessions_.end()) {
		reply_err(ev, text::panel_expired);
		return;
	}
	auto &sess = it->second;

	if (!sess.active) {
		reply_err(ev, text::panel_expired);
		return;
	}

	if (ev.command.usr.id != sess.owner_id) {
		reply_err(ev, text::panel_owner_only);
		return;
	}

	// Update the session's participant selection from the select menu values
	sess.selected.clear();
	for (const auto &v : ev.values) {
		uint64_t id{};
		auto sv = std::string_view{v};
		auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), id, 10);
		if (ec == std::errc{} && ptr == sv.data() + sv.size()) {
			sess.selected.push_back(user_id{id});
		}
		else {
			reply_err(ev, "選單值格式錯誤");
			return;
		}
	}
	// Clear any previously generated teams until the next "Assign"
	sess.last_teams.clear();
	ev.reply(dpp::ir_update_message, build_panel_message(sess));
}

/**
 * @brief Declare all guild commands the bot provides. These are registered in on_ready().
 */
std::vector<dpp::slashcommand> command_handler::commands(dpp::snowflake bot_id)
{
	using sc = dpp::slashcommand;
	std::vector<sc> cmds;
	cmds.emplace_back("help", "顯示指令清單與說明", bot_id);
	cmds.emplace_back("adduser", "新增或更新使用者的戰力", bot_id)
			.add_option(dpp::command_option(dpp::co_user, "user", "Discord 使用者", true))
			.add_option(dpp::command_option(dpp::co_number, "power", "戰力 (>=0.0)", true));
	cmds.emplace_back("removeuser", "移除使用者", bot_id).add_option(dpp::command_option(dpp::co_user, "user", "Discord 使用者", true));
	cmds.emplace_back("listusers", "顯示已註冊的使用者", bot_id);
	cmds.emplace_back("formteams", "分配隊伍", bot_id).add_option(dpp::command_option(dpp::co_integer, "teams", "隊伍數量（預設 2）", false));
	cmds.emplace_back("history", "顯示近期對戰紀錄", bot_id).add_option(dpp::command_option(dpp::co_integer, "count", "要顯示幾筆（預設 5）", false));

	return cmds;
}

/**
 * @brief Reply with an embedded help panel listing all supported commands and flows.
 */
void command_handler::cmd_help(const dpp::slashcommand_t &ev)
{
	dpp::embed e;
	e.set_title("指令說明 / Help");

	// User management
	e.add_field("使用者管理",
							"• `/adduser <user> <power>` 新增或更新成員戰力\n"
							"• `/removeuser <user>` 移除成員\n"
							"• `/listusers` 顯示使用者清單",
							false);

	// Team formation panel
	e.add_field("分隊面板",
							"• `/formteams <teams>` 開啟面板\n"
							"• 於下拉選單勾選參與者\n"
							"• 按 **「分配」** 按鈕產生/重抽隊伍\n"
							"• 按 **「隊伍 i 勝」** 按鈕紀錄勝方，其中 i 為隊伍編號\n"
							"• 按 **「結束」** 按鈕結束面板",
							false);

	// Records
	e.add_field("戰績紀錄", "• `/history [count]` 顯示最近戰績\n", false);

	ev.reply(dpp::message().add_embed(e));
}

/**
 * @brief Add or update a user's combat power.
 * 			  We also try to capture a snapshot of the username/global_name for display.
 * 			  Persist the change to disk via team_manager::save().
 */
void command_handler::cmd_adduser(const dpp::slashcommand_t &ev)
{
	dpp::snowflake uid = std::get<dpp::snowflake>(ev.get_parameter("user"));
	double power = std::get<double>(ev.get_parameter("power"));

	std::string username_snapshot;
	if (auto it = ev.command.resolved.users.find(uid); it != ev.command.resolved.users.end()) {
		const dpp::user &ru = it->second;
		if (!ru.username.empty())
			username_snapshot = ru.username;
		else if (!ru.global_name.empty())
			username_snapshot = ru.global_name;
	}

	if (username_snapshot.empty()) {
		if (auto u = dpp::find_user(uid)) {
			if (!u->username.empty())
				username_snapshot = u->username;
			else if (!u->global_name.empty())
				username_snapshot = u->global_name;
		}
	}

	if (username_snapshot.empty()) {
		auto all = tm_.list_users(user_sort::by_name_asc);
		if (auto it = std::find_if(all.begin(), all.end(), [uid](const user &x) { return x.id == uid; }); it != all.end() && !it->username.empty()) {
			username_snapshot = it->username;
		}
	}

	if (auto res = tm_.upsert_user(uid, username_snapshot, power); !res) {
		ev.reply(dpp::message("❌ " + res.error().message).set_flags(dpp::m_ephemeral));
		return;
	}

	if (auto sres = tm_.save(); !sres) { /* ignore */
	}
	ev.reply(dpp::message("✅ 新增/更新使用者 <@" + std::to_string((uint64_t)uid) + "> 的戰力為 " + std::format("{:.3f}", power)));
}

/**
 * @brief Remove a user from the registry by Discord snowflake ID, then save to disk.
 */
void command_handler::cmd_removeuser(const dpp::slashcommand_t &ev)
{
	dpp::snowflake uid = std::get<dpp::snowflake>(ev.get_parameter("user"));
	if (auto res = tm_.remove_user(uid); !res) {
		ev.reply(dpp::message("❌ " + res.error().message).set_flags(dpp::m_ephemeral));
	}
	else {
		if (auto sres = tm_.save(); !sres) { /* ignore */
		}
		ev.reply(dpp::message("🗑️ 移除使用者 <@" + std::to_string((uint64_t)uid) + ">"));
	}
}

/**
 * @brief List all registered users sorted by combat power (descending).
 * 				Shows a simple win-rate summary when available.
 */
void command_handler::cmd_listusers(const dpp::slashcommand_t &ev)
{
	dpp::embed e;
	e.set_title("使用者清單");

	std::ostringstream os;
	auto users = tm_.list_users(user_sort::by_power_desc);
	for (const auto &u : users) {
		int rate = (u.games > 0) ? (u.wins * 100 + u.games / 2) / u.games : 0;
		os << "<@" << static_cast<uint64_t>(u.id) << "> ** (" << std::format("{:.3f}", u.combat_power) << " CP)**"
			 << " — 勝率 " << rate << "% (" << u.wins << "/" << u.games << ")\n";
	}

	if (users.empty()) {
		reply_err(ev, text::no_users);
		return;
	}

	e.set_description(os.str());
	ev.reply(dpp::message().add_embed(e));
}

/**
 * @brief Start a new selection session ("panel") for the caller:
 * 					- Generate a unique token as panel_id
 * 					- Restrict control to the panel owner
 * 					- Render the initial panel (no teams yet; only selection and action buttons)
 */
void command_handler::cmd_formteams(const dpp::slashcommand_t &ev)
{
	int n = 2;
	{
		auto p = ev.get_parameter("teams");
		if (std::holds_alternative<int64_t>(p))
			n = static_cast<int>(std::get<int64_t>(p));
	}

	if (n <= 0) {
		reply_err(ev, text::teams_must_positive);
		return;
	}
	auto all = tm_.list_users(user_sort::by_name_asc);
	if (all.empty()) {
		reply_err(ev, text::no_registered_users);
		return;
	}

	selection_session s;
	s.panel_id = make_token();
	s.guild_id = ev.command.guild_id;
	s.channel_id = ev.command.channel_id;
	s.owner_id = ev.command.usr.id;
	s.num_teams = n;

	sessions_.emplace(s.panel_id, s);

	dpp::message msg = build_panel_message(s);
	msg.set_content("👑 分配面板擁有者：<@" + std::to_string((uint64_t)s.owner_id) + "> — 只有擁有者可以操作此面板");
	ev.reply(msg);
}

/**
 * @brief Show the N most recent matches from persistent history.
 *				Each entry renders winners (if recorded), timestamp, and per-team member tags.
 */
void command_handler::cmd_history(const dpp::slashcommand_t &ev)
{
	int count = 5;
	auto p = ev.get_parameter("count");
	if (std::holds_alternative<int64_t>(p))
		count = static_cast<int>(std::get<int64_t>(p));

	auto recents = tm_.recent_matches(count);

	dpp::embed e;
	e.set_title("近期對戰");
	std::ostringstream os;

	if (recents.empty()) {
		reply_err(ev, text::no_history);
		return;
	}
	else {
		int idx = 1;
		for (const auto &m : recents) {
			// title line (winner summary)
			std::string winners;
			if (!m.winning_teams.empty()) {
				winners += "勝利隊伍：";
				for (size_t i = 0; i < m.winning_teams.size(); ++i) {
					if (i)
						winners += "、";
					winners += "隊伍 " + std::to_string(m.winning_teams[i] + 1);
				}
			}
			else {
				winners = "未紀錄勝方";
			}

			os << "**比賽 #" << idx++ << "（" << winners << "）**\n";
			os << format_timestamp(m.when) << "\n";

			// team lines: prefix trophy for winners, spaces for others
			std::unordered_set<int> winset(m.winning_teams.begin(), m.winning_teams.end());
			const std::string TROPHY_PREFIX = std::string(text::trophy);
			const std::string LOSE_PREFIX = std::string(text::runner_up);

			for (size_t ti = 0; ti < m.teams.size(); ++ti) {
				const bool is_winner = winset.count(static_cast<int>(ti)) > 0;
				const std::string &prefix = is_winner ? TROPHY_PREFIX : LOSE_PREFIX;

				os << prefix << "隊伍 " << (ti + 1) << "：";
				bool first = true;
				for (const auto &mem : m.teams[ti].members) {
					if (!first)
						os << "、";
					os << "<@" << static_cast<uint64_t>(mem.id) << ">";
					first = false;
				}
				os << "\n";
			}

			os << "\n";
		}
	}

	e.set_description(os.str());
	ev.reply(dpp::message().add_embed(e));
}

/**
 * @brief Build the dynamic control panel message:
 *					- An embed showing the current team count, selected users, and last formed teams
 *					- A select menu (multi-select) to choose participants
 *					- Buttons: "Assign" (primary), "End" (danger), and winner buttons (after teams exist)
 *        "Assign" is auto-disabled until we have at least one member per team.
 */
dpp::message command_handler::build_panel_message(const selection_session &s) const
{
	dpp::message msg;
	dpp::embed e;
	e.set_title("分配隊伍面板");

	auto db_users = tm_.list_users(user_sort::by_name_asc);
	std::ostringstream body;
	body << "隊伍數量： **" << s.num_teams << "**\n";
	const bool can_assign = s.selected.size() >= static_cast<size_t>(s.num_teams);

	// Participants (rendered as Discord mentions)
	if (!s.selected.empty()) {
		body << "參與者 (" << s.selected.size() << ")： ";
		for (auto id : s.selected)
			body << "<@" << static_cast<uint64_t>(id) << "> ";
		body << "\n\n";

		if (!can_assign)
			body << "⚠️ 需至少選擇 " << s.num_teams << " 名玩家（每隊 1 人）才能分配。\n";
	}
	else {
		body << "*於底下的清單中選取要參與隊伍分配的使用者*\n";
	}

	if (!s.last_teams.empty()) {
		// C++23: use minmax_element with a projection instead of minmax on a range
		const auto [min_it, max_it] = std::ranges::minmax_element(s.last_teams, {}, &team::total_power);
		const double minp = (min_it != s.last_teams.end()) ? min_it->total_power : 0.0;
		const double maxp = (max_it != s.last_teams.end()) ? max_it->total_power : 0.0;

		for (size_t i = 0; i < s.last_teams.size(); ++i) {
			const auto &team = s.last_teams[i];
			body << "隊伍 " << (i + 1) << "（總戰力 " << std::format("{:.3f}", team.total_power) << " CP）：";
			bool first = true;
			for (const auto &m : team.members) {
				if (!first)
					body << "、";
				body << "<@" << static_cast<uint64_t>(m.id) << ">";
				first = false;
			}
			body << "\n";
		}
		body << "最大戰力差：" << std::format("{:.3f}", (maxp - minp)) << " CP\n";
	}

	e.set_description(body.str());
	msg.add_embed(e);

	dpp::component row1;
	dpp::component menu;
	menu.set_type(dpp::cot_selectmenu);
	menu.set_id("panel:" + s.panel_id + ":select");
	menu.set_placeholder("選擇參與分配的成員 (可複選)");

	size_t max_opts = std::min<size_t>(db_users.size(), 25);
	std::unordered_set<uint64_t> chosen;
	for (auto id : s.selected)
		chosen.insert((uint64_t)id);

	for (size_t i = 0; i < max_opts; ++i) {
		const auto &u = db_users[i];
		bool def = chosen.contains((uint64_t)u.id);
		std::string label = u.username.empty() ? ("<@" + std::to_string((uint64_t)u.id) + ">") : u.username;
		dpp::select_option opt(label + " (" + std::format("{:.3f}", u.combat_power) + ")", std::to_string((uint64_t)u.id), "已註冊成員");
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
												 .set_id("panel:" + s.panel_id + ":assign")
												 .set_disabled(!can_assign));
	row2.add_component(dpp::component().set_type(dpp::cot_button).set_style(dpp::cos_danger).set_label("結束").set_id("panel:" + s.panel_id + ":end"));
	msg.add_component(row2);

	// Winner buttons (visible only after teams are generated)
	if (!s.last_teams.empty()) {
		dpp::component row;
		int in_row = 0;
		for (size_t i = 0; i < s.last_teams.size(); ++i) {
			if (in_row == 5) {
				msg.add_component(row);
				row = dpp::component{};
				in_row = 0;
			}

			row.add_component(dpp::component()
														.set_type(dpp::cot_button)
														.set_style(dpp::cos_success)
														.set_label("隊伍 " + std::to_string(i + 1) + " 勝")
														.set_id("panel:" + s.panel_id + ":win:" + std::to_string(i)));
			++in_row;
		}

		if (in_row)
			msg.add_component(row);
	}

	return msg;
}

void command_handler::refresh_session_snapshot(selection_session &s) const
{
	for (auto &t : s.last_teams) {
		for (auto &m : t.members) {
			if (const auto *u = tm_.find_user(m.id)) {
				// Sync display fields from the latest registry values
				m.username = u->username;
				m.combat_power = u->combat_power;
			}
		}
		t.recalc_total_power(); // keep cached totals accurate
	}
}

} // namespace terry::bot
