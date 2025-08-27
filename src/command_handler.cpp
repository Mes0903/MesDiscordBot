#include "command_handler.hpp"

#include <algorithm>
#include <random>
#include <span>
#include <sstream>
#include <unordered_set>

namespace terry::bot {

// /listusers：顯示 tag（mention）
namespace {
dpp::embed users_embed(std::span<const user> users, std::string title)
{
	dpp::embed e;
	e.set_title(std::move(title));
	std::ostringstream os;
	for (const auto &u : users) {
		os << "<@" << static_cast<uint64_t>(u.id) << "> — **" << u.combat_power << "**\n";
	}
	if (users.empty())
		os << "_(no users)_";
	e.set_description(os.str());
	return e;
}
} // namespace

// 分組/歷史顯示用的人名（優先 username，取不到再退回 mention）
std::string command_handler::display_name(user_id uid, dpp::snowflake guild) const
{
	// 1) user 快取：username / global name
	if (auto u = dpp::find_user(uid)) {
		if (!u->username.empty())
			return u->username;
		if (!u->global_name.empty())
			return u->global_name;
	}
	// 2) guild 暱稱（只是顯示用途，不寫入 DB）
	if (auto g = dpp::find_guild(guild)) {
		if (auto it = g->members.find(uid); it != g->members.end()) {
			if (!it->second.get_nickname().empty())
				return it->second.get_nickname();
		}
	}
	// 3) 最後：mention（不要回傳純數字）
	return "<@" + std::to_string(static_cast<uint64_t>(uid)) + ">";
}

std::string command_handler::make_token()
{
	static std::mt19937_64 rng{std::random_device{}()};
	std::uniform_int_distribution<uint64_t> dist;
	std::ostringstream oss;
	oss << std::hex << dist(rng);
	return oss.str();
}

void command_handler::on_slash(const dpp::slashcommand_t &ev)
{
	auto name = ev.command.get_command_name();
	if (name == "help")
		return cmd_help(ev);
	if (name == "adduser")
		return cmd_adduser(ev);
	if (name == "removeuser")
		return cmd_removeuser(ev);
	if (name == "setpower")
		return cmd_setpower(ev);
	if (name == "listusers")
		return cmd_listusers(ev);
	if (name == "formteams")
		return cmd_formteams(ev);
	if (name == "history")
		return cmd_history(ev);
	if (name == "recordmatch")
		return cmd_recordmatch(ev);
	ev.reply(dpp::message("Unknown command").set_flags(dpp::m_ephemeral));
}

void command_handler::on_button(const dpp::button_click_t &ev)
{
	auto cid = ev.custom_id;

	// help panel close
	if (cid == "help:close") {
		dpp::message m;
		m.set_content("❎ Help panel closed.");
		ev.reply(dpp::ir_update_message, m);
		return;
	}

	// panel actions
	if (!starts_with(cid, "panel:")) {
		ev.reply(dpp::ir_channel_message_with_source, dpp::message("Unsupported button").set_flags(dpp::m_ephemeral));
		return;
	}

	// panel:<id>:<action>[:arg]
	auto rest = cid.substr(6);
	auto p1 = rest.find(':');
	if (p1 == std::string::npos)
		return;
	auto pid = rest.substr(0, p1);
	auto action = rest.substr(p1 + 1);

	auto it = sessions_.find(pid);
	if (it == sessions_.end()) {
		ev.reply(dpp::ir_channel_message_with_source, dpp::message("This panel is no longer active.").set_flags(dpp::m_ephemeral));
		return;
	}
	auto &sess = it->second;

	if (!sess.active) {
		ev.reply(dpp::ir_channel_message_with_source, dpp::message("This panel has ended.").set_flags(dpp::m_ephemeral));
		return;
	}
	if (ev.command.usr.id != sess.owner_id) {
		ev.reply(dpp::ir_channel_message_with_source, dpp::message("Only the panel owner can operate this.").set_flags(dpp::m_ephemeral));
		return;
	}

	if (action == "assign") {
		if ((int)sess.selected.size() < sess.num_teams) {
			ev.reply(dpp::ir_channel_message_with_source, dpp::message("Select at least one member per team before assigning.").set_flags(dpp::m_ephemeral));
			return;
		}
		sess.last_teams = tm_.form_teams(sess.selected, sess.num_teams);
		ev.reply(dpp::ir_update_message, build_panel_message(sess));
		return;
	}

	if (action == "reassign") {
		if (sess.selected.empty()) {
			ev.reply(dpp::ir_channel_message_with_source, dpp::message("No participants selected.").set_flags(dpp::m_ephemeral));
			return;
		}
		sess.last_teams = tm_.form_teams(sess.selected, sess.num_teams);
		ev.reply(dpp::ir_update_message, build_panel_message(sess));
		return;
	}

	if (starts_with(action, "win:")) {
		int idx = -1;
		try {
			idx = std::stoi(action.substr(4));
		} catch (...) {
		}
		if (idx < 0 || idx >= (int)sess.last_teams.size()) {
			ev.reply(dpp::ir_channel_message_with_source, dpp::message("Invalid team index.").set_flags(dpp::m_ephemeral));
			return;
		}
		if (auto res = tm_.record_match(sess.last_teams, std::vector<int>{idx}); !res) {
			ev.reply(dpp::ir_channel_message_with_source, dpp::message("❌ " + res.error().message).set_flags(dpp::m_ephemeral));
		}
		else {
			if (auto sres = tm_.save(); !sres) { /* ignore */
			}
			auto m = build_panel_message(sess);
			m.set_content("✅ Winner recorded: Team " + std::to_string(idx));
			ev.reply(dpp::ir_update_message, m);
		}
		return;
	}

	if (action == "end") {
		sess.active = false;
		auto m = build_panel_message(sess);
		m.components.clear(); // disable all
		m.set_content("🔒 Panel ended by <@" + std::to_string((uint64_t)sess.owner_id) + ">");
		ev.reply(dpp::ir_update_message, m);
		sessions_.erase(it);
		return;
	}

	ev.reply(dpp::ir_channel_message_with_source, dpp::message("Unknown panel action").set_flags(dpp::m_ephemeral));
}

void command_handler::on_select(const dpp::select_click_t &ev)
{
	auto cid = ev.custom_id;

	// help topic switch
	if (cid == "help:topic") {
		std::string topic = ev.values.empty() ? "overview" : ev.values.front();
		dpp::message m;
		m.add_embed(help_embed(topic));
		ev.reply(dpp::ir_update_message, m);
		return;
	}

	if (!starts_with(cid, "panel:")) {
		ev.reply(dpp::ir_channel_message_with_source, dpp::message("Unsupported select").set_flags(dpp::m_ephemeral));
		return;
	}

	// panel:<id>:select
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
		ev.reply(dpp::ir_channel_message_with_source, dpp::message("This panel is no longer active.").set_flags(dpp::m_ephemeral));
		return;
	}
	auto &sess = it->second;

	if (!sess.active) {
		ev.reply(dpp::ir_channel_message_with_source, dpp::message("This panel has ended.").set_flags(dpp::m_ephemeral));
		return;
	}
	if (ev.command.usr.id != sess.owner_id) {
		ev.reply(dpp::ir_channel_message_with_source, dpp::message("Only the panel owner can select participants.").set_flags(dpp::m_ephemeral));
		return;
	}

	// values contain snowflake strings
	sess.selected.clear();
	for (const auto &v : ev.values) {
		try {
			sess.selected.push_back(user_id{std::stoull(v)});
		} catch (...) {
		}
	}
	// reset the last teams until "Assign"
	sess.last_teams.clear();
	ev.reply(dpp::ir_update_message, build_panel_message(sess));
}

std::vector<dpp::slashcommand> command_handler::commands(dpp::snowflake bot_id)
{
	using sc = dpp::slashcommand;
	std::vector<sc> cmds;
	cmds.emplace_back("help", "Show help panel", bot_id);

	cmds.emplace_back("adduser", "Add (or update) a user with combat power", bot_id)
			.add_option(dpp::command_option(dpp::co_user, "user", "Discord user", true))
			.add_option(dpp::command_option(dpp::co_integer, "power", "Combat power (>=0)", true));

	cmds.emplace_back("removeuser", "Remove a user", bot_id).add_option(dpp::command_option(dpp::co_user, "user", "Discord user", true));

	cmds.emplace_back("setpower", "Update a user's combat power", bot_id)
			.add_option(dpp::command_option(dpp::co_user, "user", "Discord user", true))
			.add_option(dpp::command_option(dpp::co_integer, "power", "Combat power (>=0)", true));

	cmds.emplace_back("listusers", "List registered users", bot_id);

	// 開啟互動面板：參與者用選單選、按鈕分配
	cmds.emplace_back("formteams", "Open team formation panel", bot_id).add_option(dpp::command_option(dpp::co_integer, "teams", "Number of teams", true));

	// 保留 legacy（不走面板）
	cmds.emplace_back("recordmatch", "Record a match result by winner indices", bot_id)
			.add_option(dpp::command_option(dpp::co_string, "winners", "Comma-separated winner indices, e.g. 0 or 0,2", true));

	cmds.emplace_back("history", "Show recent match history", bot_id).add_option(dpp::command_option(dpp::co_integer, "count", "How many recent matches", false));
	return cmds;
}

// ---------- HELP ----------

dpp::embed command_handler::help_embed(std::string topic) const
{
	dpp::embed e;
	e.set_title("Bot Help");
	if (topic == "users") {
		e.set_description("**User Management**\n"
											"`/adduser <user> <power>` 新增或更新成員戰力\n"
											"`/setpower <user> <power>` 單純更新戰力\n"
											"`/removeuser <user>` 移除成員\n"
											"`/listusers` 顯示目前資料庫\n");
	}
	else if (topic == "panel") {
		e.set_description("**Team Formation Panel**\n"
											"1. `/formteams <teams>` 開啟面板\n"
											"2. 從選單勾選要參與的人\n"
											"3. 按 **分配** 產生隊伍\n"
											"4. 可按 **重新分配** 隨機重抽（相同人員）\n"
											"5. 可按 **Team i 勝** 紀錄勝利隊伍\n"
											"6. **結束** 鎖定面板（面板失效）\n");
	}
	else if (topic == "records") {
		e.set_description("**Match Records**\n"
											"`/history [count]` 查看最近戰績（含隊員）\n"
											"在面板按下「Team i 勝」會自動寫入紀錄\n");
	}
	else {
		e.set_description("選擇主題以查看說明：\n- 使用者管理\n- 分隊面板\n- 戰績紀錄\n"
											"或直接輸入 `/help`");
	}
	return e;
}

void command_handler::cmd_help(const dpp::slashcommand_t &ev)
{
	dpp::message msg;
	msg.add_embed(help_embed("overview"));

	// 選單
	dpp::component row1;
	dpp::component menu;
	menu.set_type(dpp::cot_selectmenu);
	menu.set_id("help:topic");
	menu.set_placeholder("選擇主題…");
	menu.add_select_option(dpp::select_option("總覽", "overview", "Overview"));
	menu.add_select_option(dpp::select_option("使用者管理", "users", "User management"));
	menu.add_select_option(dpp::select_option("分隊面板", "panel", "Team formation panel"));
	menu.add_select_option(dpp::select_option("戰績紀錄", "records", "Match records"));
	row1.add_component(menu);
	msg.add_component(row1);

	// 關閉按鈕
	dpp::component row2;
	row2.add_component(dpp::component().set_type(dpp::cot_button).set_style(dpp::cos_secondary).set_label("關閉").set_id("help:close"));
	msg.add_component(row2);

	ev.reply(msg);
}

// ---------- USER COMMANDS ----------

void command_handler::cmd_adduser(const dpp::slashcommand_t &ev)
{
	dpp::snowflake uid = std::get<dpp::snowflake>(ev.get_parameter("user"));
	int power = static_cast<int>(std::get<int64_t>(ev.get_parameter("power")));

	// 1) 先用 slash 互動的 resolved（最可靠，包含被選到的 user）
	std::string username_snapshot;
	if (auto it = ev.command.resolved.users.find(uid); it != ev.command.resolved.users.end()) {
		const dpp::user &ru = it->second;
		if (!ru.username.empty())
			username_snapshot = ru.username;
		else if (!ru.global_name.empty())
			username_snapshot = ru.global_name;
	}

	// 2) 再退到全域快取
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
	ev.reply(dpp::message("✅ Added/updated user <@" + std::to_string((uint64_t)uid) + "> with power " + std::to_string(power)));
}

void command_handler::cmd_removeuser(const dpp::slashcommand_t &ev)
{
	dpp::snowflake uid = std::get<dpp::snowflake>(ev.get_parameter("user"));
	if (auto res = tm_.remove_user(uid); !res) {
		ev.reply(dpp::message("❌ " + res.error().message).set_flags(dpp::m_ephemeral));
	}
	else {
		if (auto sres = tm_.save(); !sres) { /* ignore */
		}
		ev.reply(dpp::message("🗑️ Removed user <@" + std::to_string((uint64_t)uid) + ">"));
	}
}

void command_handler::cmd_setpower(const dpp::slashcommand_t &ev)
{
	dpp::snowflake uid = std::get<dpp::snowflake>(ev.get_parameter("user"));
	int power = static_cast<int>(std::get<int64_t>(ev.get_parameter("power")));

	std::string username_snapshot;
	// 1) resolved
	if (auto it = ev.command.resolved.users.find(uid); it != ev.command.resolved.users.end()) {
		const dpp::user &ru = it->second;
		if (!ru.username.empty())
			username_snapshot = ru.username;
		else if (!ru.global_name.empty())
			username_snapshot = ru.global_name;
	}
	// 2) cache
	if (username_snapshot.empty()) {
		if (auto u = dpp::find_user(uid)) {
			if (!u->username.empty())
				username_snapshot = u->username;
			else if (!u->global_name.empty())
				username_snapshot = u->global_name;
		}
	}
	// 3) keep existing snapshot if any
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
	ev.reply(dpp::message("🔧 Set power of <@" + std::to_string((uint64_t)uid) + "> to " + std::to_string(power)).set_flags(dpp::m_ephemeral));
}

void command_handler::cmd_listusers(const dpp::slashcommand_t &ev)
{
	auto users = tm_.list_users(user_sort::by_power_desc);
	ev.reply(dpp::message().add_embed(users_embed(users, "Registered Users (by power)")));
}

// ---------- PANEL ----------

dpp::message command_handler::build_panel_message(const selection_session &s) const
{
	dpp::message msg;
	dpp::embed e;
	e.set_title("Team Formation Panel");

	// 取一次 DB，供下拉選/隊伍成員用 username
	auto db_users = tm_.list_users(user_sort::by_name_asc);
	std::ostringstream body;
	body << "Teams: **" << s.num_teams << "**\n";

	// Participants 行用「tag」
	if (!s.selected.empty()) {
		body << "Participants (" << s.selected.size() << "): ";
		for (auto id : s.selected)
			body << "<@" << static_cast<uint64_t>(id) << "> ";
		body << "\n\n";
	}
	else {
		body << "_Select participants in the menu below._\n";
	}

	// 分隊結果：隊員名稱用 DB 的 username（沒有才退回 mention）
	if (!s.last_teams.empty()) {
		for (size_t i = 0; i < s.last_teams.size(); ++i) {
			body << "隊伍 " << (i + 1) << " (" << s.last_teams[i].total_power << " CP)：";
			bool first = true;
			for (const auto &m : s.last_teams[i].members) {
				if (!first)
					body << "、";
				body << "<@" << static_cast<uint64_t>(m.id) << "> (" << m.combat_power << " CP)";
				first = false;
			}
			body << "\n";
		}
	}
	e.set_description(body.str());
	msg.add_embed(e);

	// row1: 下拉選單（label 顯示 username）
	dpp::component row1;
	dpp::component menu;
	menu.set_type(dpp::cot_selectmenu);
	menu.set_id("panel:" + s.panel_id + ":select");
	menu.set_placeholder("選擇參與成員 (可複選)");
	size_t max_opts = std::min<size_t>(db_users.size(), 25);
	std::unordered_set<uint64_t> chosen;
	for (auto id : s.selected)
		chosen.insert((uint64_t)id);
	for (size_t i = 0; i < max_opts; ++i) {
		const auto &u = db_users[i];
		bool def = chosen.contains((uint64_t)u.id);
		std::string label = u.username.empty() ? ("<@" + std::to_string((uint64_t)u.id) + ">") : u.username;
		dpp::select_option opt(label + " (" + std::to_string(u.combat_power) + ")", std::to_string((uint64_t)u.id), "已註冊成員");
		if (def)
			opt.set_default(true);
		menu.add_select_option(std::move(opt));
	}
	menu.set_min_values(0);
	menu.set_max_values((int)max_opts);
	row1.add_component(menu);
	msg.add_component(row1);

	dpp::component row2;
	bool can_assign = (int)s.selected.size() >= s.num_teams;
	row2.add_component(dpp::component()
												 .set_type(dpp::cot_button)
												 .set_style(dpp::cos_primary)
												 .set_label("分配隊伍")
												 .set_id("panel:" + s.panel_id + ":assign")
												 .set_disabled(!can_assign));
	row2.add_component(dpp::component().set_type(dpp::cot_button).set_style(dpp::cos_danger).set_label("結束").set_id("panel:" + s.panel_id + ":end"));
	msg.add_component(row2);

	// 勝方按鈕（分配後才會出現）
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
														.set_label("Team " + std::to_string(i) + " 勝")
														.set_id("panel:" + s.panel_id + ":win:" + std::to_string(i)));
			++in_row;
		}
		if (in_row)
			msg.add_component(row);
	}

	return msg;
}

void command_handler::cmd_formteams(const dpp::slashcommand_t &ev)
{
	int n = static_cast<int>(std::get<int64_t>(ev.get_parameter("teams")));
	if (n <= 0) {
		ev.reply(dpp::message("Number of teams must be > 0").set_flags(dpp::m_ephemeral));
		return;
	}
	auto all = tm_.list_users(user_sort::by_name_asc);
	if (all.empty()) {
		ev.reply(dpp::message("目前沒有註冊的使用者，先用 `/adduser` 新增。").set_flags(dpp::m_ephemeral));
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
	msg.set_content("👑 面板擁有者：<@" + std::to_string((uint64_t)s.owner_id) + "> — 只有擁有者可以操作");
	ev.reply(msg);
}

// ---------- HISTORY & LEGACY ----------

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
		os << "_(no matches yet)_";
	}
	else {
		int idx = 1;
		for (const auto &m : recents) {
			// 比賽標題（哪隊獲勝）
			std::string winners;
			if (!m.winning_teams.empty()) {
				for (size_t i = 0; i < m.winning_teams.size(); ++i) {
					if (i)
						winners += "、";
					winners += "隊伍" + std::to_string(m.winning_teams[i]);
				}
				winners += " 勝";
			}
			else {
				winners = "未紀錄勝方";
			}

			os << "比賽 #" << idx++ << "（" << winners << "）\n";
			os << format_timestamp(m.when) << "\n";

			// 隊伍：只列 tag，用頓號「、」分隔
			for (size_t ti = 0; ti < m.teams.size(); ++ti) {
				os << "隊伍" << ti << "：";
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

void command_handler::cmd_recordmatch(const dpp::slashcommand_t &ev)
{
	std::string winners_str = std::get<std::string>(ev.get_parameter("winners"));
	std::vector<int> winners;
	std::stringstream ss(winners_str);
	std::string tok;
	while (std::getline(ss, tok, ',')) {
		try {
			winners.push_back(std::stoi(tok));
		} catch (...) {
		}
	}
	if (winners.empty()) {
		ev.reply(dpp::message("Provide winner indices like `0` or `0,2`").set_flags(dpp::m_ephemeral));
		return;
	}
	std::vector<team> teams; // legacy: 沒有面板資訊時，只紀錄勝者索引
	if (auto res = tm_.record_match(std::move(teams), std::move(winners)); !res) {
		ev.reply(dpp::message("❌ " + res.error().message).set_flags(dpp::m_ephemeral));
	}
	else {
		if (auto sres = tm_.save(); !sres) { /* ignore */
		}
		ev.reply(dpp::message("✅ Recorded match result").set_flags(dpp::m_ephemeral));
	}
}

} // namespace terry::bot
