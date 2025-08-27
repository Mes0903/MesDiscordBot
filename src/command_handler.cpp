#include "command_handler.hpp"

#include <algorithm>
#include <random>
#include <span>
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

	ev.reply(dpp::message("❌ Unknown command").set_flags(dpp::m_ephemeral));
}

void command_handler::on_button(const dpp::button_click_t &ev)
{
	const auto &cid = ev.custom_id;

	// Expect panel actions only
	if (!starts_with(cid, "panel:")) {
		ev.reply(dpp::ir_channel_message_with_source, dpp::message("❌ 不支援的按鈕").set_flags(dpp::m_ephemeral));
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
		ev.reply(dpp::ir_channel_message_with_source, dpp::message("❌ 此面板已失效").set_flags(dpp::m_ephemeral));
		return;
	}
	auto &sess = it->second;

	if (!sess.active) {
		ev.reply(dpp::ir_channel_message_with_source, dpp::message("❌ 此面板已失效").set_flags(dpp::m_ephemeral));
		return;
	}
	if (ev.command.usr.id != sess.owner_id) {
		ev.reply(dpp::ir_channel_message_with_source, dpp::message("❌ 只有面板擁有者才能操作此面板").set_flags(dpp::m_ephemeral));
		return;
	}

	if (action == "assign") {
		if ((int)sess.selected.size() < sess.num_teams) {
			ev.reply(dpp::ir_channel_message_with_source, dpp::message("❌ 請至少選擇一名成員加入每個隊伍").set_flags(dpp::m_ephemeral));
			return;
		}
		// Re/assign teams using the current selection
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
			ev.reply(dpp::ir_channel_message_with_source, dpp::message("❌ 無效的隊伍索引").set_flags(dpp::m_ephemeral));
			return;
		}
		if (auto res = tm_.record_match(sess.last_teams, std::vector<int>{idx}); !res) {
			ev.reply(dpp::ir_channel_message_with_source, dpp::message("❌ " + res.error().message).set_flags(dpp::m_ephemeral));
		}
		else {
			if (auto sres = tm_.save(); !sres) { /* ignore */
			}
			auto m = build_panel_message(sess);
			m.set_content("✅ 已記錄勝利隊伍： 隊伍 " + std::to_string(idx));
			ev.reply(dpp::ir_update_message, m);
		}
		return;
	}

	if (action == "end") {
		sess.active = false;
		auto m = build_panel_message(sess);
		m.components.clear(); // disable all
		m.set_content("🔒 面板已由 <@" + std::to_string((uint64_t)sess.owner_id) + "> 關閉");
		ev.reply(dpp::ir_update_message, m);
		sessions_.erase(it);
		return;
	}

	ev.reply(dpp::ir_channel_message_with_source, dpp::message("❌ 未知的面板操作").set_flags(dpp::m_ephemeral));
}

void command_handler::on_select(const dpp::select_click_t &ev)
{
	const auto &cid = ev.custom_id;

	if (!starts_with(cid, "panel:")) {
		ev.reply(dpp::ir_channel_message_with_source, dpp::message("❌ 不支援的選項").set_flags(dpp::m_ephemeral));
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
		ev.reply(dpp::ir_channel_message_with_source, dpp::message("❌ 此面板已失效").set_flags(dpp::m_ephemeral));
		return;
	}
	auto &sess = it->second;

	if (!sess.active) {
		ev.reply(dpp::ir_channel_message_with_source, dpp::message("❌ 此面板已失效").set_flags(dpp::m_ephemeral));
		return;
	}
	if (ev.command.usr.id != sess.owner_id) {
		ev.reply(dpp::ir_channel_message_with_source, dpp::message("❌ 只有面板擁有者才能操作此面板").set_flags(dpp::m_ephemeral));
		return;
	}

	// Update selection from values
	sess.selected.clear();
	for (const auto &v : ev.values) {
		try {
			sess.selected.push_back(user_id{std::stoull(v)});
		} catch (...) {
		}
	}
	// Clear previous teams until re/assign
	sess.last_teams.clear();
	ev.reply(dpp::ir_update_message, build_panel_message(sess));
}

std::vector<dpp::slashcommand> command_handler::commands(dpp::snowflake bot_id)
{
	using sc = dpp::slashcommand;
	std::vector<sc> cmds;
	cmds.emplace_back("help", "顯示指令清單與說明", bot_id);

	cmds.emplace_back("adduser", "新增或更新使用者的戰力", bot_id)
			.add_option(dpp::command_option(dpp::co_user, "user", "Discord 使用者", true))
			.add_option(dpp::command_option(dpp::co_integer, "power", "戰力 (>=0)", true));

	cmds.emplace_back("removeuser", "移除使用者", bot_id).add_option(dpp::command_option(dpp::co_user, "user", "Discord 使用者", true));

	cmds.emplace_back("listusers", "顯示已註冊的使用者", bot_id);

	cmds.emplace_back("formteams", "分配隊伍", bot_id).add_option(dpp::command_option(dpp::co_integer, "teams", "隊伍數量", true));

	cmds.emplace_back("history", "顯示近期對戰紀錄", bot_id).add_option(dpp::command_option(dpp::co_integer, "count", "顯示幾場比賽", false));
	return cmds;
}

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
							"• 於下拉選單勾選參與者（選單顯示 username）\n"
							"• 按 **分配** 產生/重抽隊伍\n"
							"• 按 **隊伍 i 勝** 紀錄勝方\n"
							"• 按 **結束** 結束面板（面板失效）",
							false);

	// Records
	e.add_field("戰績紀錄", "• `/history [count]` 顯示最近戰績\n", false);

	ev.reply(dpp::message().add_embed(e));
}

void command_handler::cmd_adduser(const dpp::slashcommand_t &ev)
{
	dpp::snowflake uid = std::get<dpp::snowflake>(ev.get_parameter("user"));
	int power = static_cast<int>(std::get<int64_t>(ev.get_parameter("power")));

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
	ev.reply(dpp::message("✅ 新增/更新使用者 <@" + std::to_string((uint64_t)uid) + "> 的戰力為 " + std::to_string(power)));
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
		ev.reply(dpp::message("🗑️ 移除使用者 <@" + std::to_string((uint64_t)uid) + ">"));
	}
}

void command_handler::cmd_listusers(const dpp::slashcommand_t &ev)
{
	dpp::embed e;
	e.set_title("使用者清單");

	std::ostringstream os;
	auto users = tm_.list_users(user_sort::by_power_desc);
	for (const auto &u : users) {
		int rate = (u.games > 0) ? (u.wins * 100 + u.games / 2) / u.games : 0;
		os << "<@" << static_cast<uint64_t>(u.id) << "> ** (" << u.combat_power << " CP)**"
			 << " — 勝率 " << rate << "% (" << u.wins << "/" << u.games << ")\n";
	}

	if (users.empty()) {
		ev.reply(dpp::message("❌ 尚無使用者").set_flags(dpp::m_ephemeral));
		return;
	}

	e.set_description(os.str());
	ev.reply(dpp::message().add_embed(e));
}

dpp::message command_handler::build_panel_message(const selection_session &s) const
{
	dpp::message msg;
	dpp::embed e;
	e.set_title("分配隊伍面板");

	auto db_users = tm_.list_users(user_sort::by_name_asc);
	std::ostringstream body;
	body << "隊伍數量： **" << s.num_teams << "**\n";

	// Participants (as tags)
	if (!s.selected.empty()) {
		body << "參與者 (" << s.selected.size() << ")： ";
		for (auto id : s.selected)
			body << "<@" << static_cast<uint64_t>(id) << "> ";
		body << "\n\n";
	}
	else {
		body << "*於底下的清單中選取要參與隊伍分配的使用者*\n";
	}

	if (!s.last_teams.empty()) {
		int minp = std::numeric_limits<int>::max();
		int maxp = std::numeric_limits<int>::min();

		for (const auto &t : s.last_teams) {
			minp = std::min(minp, t.total_power);
			maxp = std::max(maxp, t.total_power);
		}

		for (size_t i = 0; i < s.last_teams.size(); ++i) {
			const auto &team = s.last_teams[i];
			body << "隊伍 " << (i + 1) << "（總戰力 " << team.total_power << " CP）：";
			bool first = true;
			for (const auto &m : team.members) {
				if (!first)
					body << "、";
				body << "<@" << static_cast<uint64_t>(m.id) << ">";
				first = false;
			}
			body << "\n";
		}
		body << "最大戰力差：" << (maxp - minp) << " CP\n";
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
														.set_label("隊伍 " + std::to_string(i) + " 勝")
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
		ev.reply(dpp::message("❌ 隊伍數量需大於 0").set_flags(dpp::m_ephemeral));
		return;
	}
	auto all = tm_.list_users(user_sort::by_name_asc);
	if (all.empty()) {
		ev.reply(dpp::message("❌ 目前沒有註冊的使用者，請先用 `/adduser` 新增").set_flags(dpp::m_ephemeral));
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
		ev.reply(dpp::message("❌ 尚無對戰紀錄").set_flags(dpp::m_ephemeral));
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
			const std::string TROPHY_PREFIX = "🏆 ";
			const std::string LOSE_PREFIX = "🥈 ";

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

} // namespace terry::bot
