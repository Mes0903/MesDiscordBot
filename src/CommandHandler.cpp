#include "CommandHandler.hpp"

#include <algorithm>
#include <sstream>

CommandHandler::CommandHandler(TeamManager &manager) : team_manager(manager) {}

void CommandHandler::handle_command(const dpp::slashcommand_t &event)
{
	std::string command_name = event.command.get_command_name();

	if (command_name == "adduser") {
		handle_add_user(event);
	}
	else if (command_name == "removeuser") {
		handle_remove_user(event);
	}
	else if (command_name == "updatepower") {
		handle_update_power(event);
	}
	else if (command_name == "listusers") {
		handle_list_users(event);
	}
	else if (command_name == "createteams") {
		handle_create_teams(event);
	}
	else if (command_name == "history") {
		handle_match_history(event);
	}
	else if (command_name == "help") {
		handle_help(event);
	}
}

void CommandHandler::handle_add_user(const dpp::slashcommand_t &event)
{
	auto user_option = event.get_parameter("user");
	auto power_option = event.get_parameter("combat_power");

	if (std::holds_alternative<dpp::snowflake>(user_option) && std::holds_alternative<int64_t>(power_option)) {
		dpp::snowflake user_id = std::get<dpp::snowflake>(user_option);
		int combat_power = static_cast<int>(std::get<int64_t>(power_option));

		if (combat_power < 0) {
			event.reply("戰力不能為負數");
			return;
		}

		// Try to get the actual username from the interaction
		std::string username = "User_" + std::to_string(static_cast<uint64_t>(user_id));

		// Check if we can get the username from the resolved users
		if (event.command.resolved.users.find(user_id) != event.command.resolved.users.end()) {
			username = event.command.resolved.users.at(user_id).username;
		}

		team_manager.add_user(user_id, username, combat_power);

		// Get updated user list
		auto all_users = team_manager.get_all_users();
		int total_power = 0;
		for (const auto &user : all_users) {
			total_power += user.combat_power;
		}

		dpp::embed embed = dpp::embed()
													 .set_color(0x00ff00)
													 .set_title("新增使用者成功")
													 .add_field("新增的使用者", "<@" + std::to_string(user_id) + "> (" + std::to_string(combat_power) + " CP)", false);

		// Add current user list
		std::string user_list;
		for (const auto &user : all_users) {
			user_list += "<@" + std::to_string(user.discord_id) + "> (" + std::to_string(user.combat_power) + " CP)\n";
		}

		embed.add_field("目前所有成員 (" + std::to_string(all_users.size()) + "人)", user_list, false);
		embed.add_field("總戰力", std::to_string(total_power), true);
		embed.add_field("平均戰力", std::to_string(all_users.empty() ? 0 : total_power / static_cast<int>(all_users.size())), true);

		event.reply(dpp::message().add_embed(embed));
	}
	else {
		event.reply("Invalid parameters. Please provide a valid user and combat power.");
	}
}

void CommandHandler::handle_remove_user(const dpp::slashcommand_t &event)
{
	auto user_option = event.get_parameter("user");

	if (std::holds_alternative<dpp::snowflake>(user_option)) {
		dpp::snowflake user_id = std::get<dpp::snowflake>(user_option);

		if (team_manager.remove_user(user_id)) {
			// Get updated user list
			auto all_users = team_manager.get_all_users();
			int total_power = 0;
			for (const auto &user : all_users) {
				total_power += user.combat_power;
			}

			dpp::embed embed = dpp::embed().set_color(0xff0000).set_title("移除使用者成功").add_field("已移除的使用者", "<@" + std::to_string(user_id) + ">", false);

			if (all_users.empty()) {
				embed.add_field("目前狀態", "系統中沒有任何使用者", false);
			}
			else {
				// Add current user list
				std::string user_list;
				for (const auto &user : all_users) {
					user_list += "<@" + std::to_string(user.discord_id) + "> (" + std::to_string(user.combat_power) + " CP)\n";
				}

				embed.add_field("目前所有成員 (" + std::to_string(all_users.size()) + "人)", user_list, false);
				embed.add_field("總戰力", std::to_string(total_power), true);
				embed.add_field("平均戰力", std::to_string(total_power / static_cast<int>(all_users.size())), true);
			}

			event.reply(dpp::message().add_embed(embed));
		}
		else {
			event.reply("找不到該使用者");
		}
	}
	else {
		event.reply("無效的使用者參數");
	}
}

void CommandHandler::handle_update_power(const dpp::slashcommand_t &event)
{
	auto user_option = event.get_parameter("user");
	auto power_option = event.get_parameter("new_power");

	if (std::holds_alternative<dpp::snowflake>(user_option) && std::holds_alternative<int64_t>(power_option)) {

		dpp::snowflake user_id = std::get<dpp::snowflake>(user_option);
		int new_power = static_cast<int>(std::get<int64_t>(power_option));

		if (new_power < 0) {
			event.reply("戰力不能為負數");
			return;
		}

		// Get the old power for comparison
		auto *existing_user = team_manager.get_user(user_id);
		int old_power = existing_user ? existing_user->combat_power : 0;

		if (team_manager.update_combat_power(user_id, new_power)) {
			// Get updated user list
			auto all_users = team_manager.get_all_users();
			int total_power = 0;
			for (const auto &user : all_users) {
				total_power += user.combat_power;
			}

			dpp::embed embed = dpp::embed()
														 .set_color(0x0099ff)
														 .set_title("更新戰力成功")
														 .add_field("更新的使用者", "<@" + std::to_string(user_id) + ">", true)
														 .add_field("戰力變化", std::to_string(old_power) + " → " + std::to_string(new_power), true)
														 .add_field("變化量", (new_power > old_power ? "+" : "") + std::to_string(new_power - old_power), true);

			// Add current user list
			std::string user_list;
			for (const auto &user : all_users) {
				user_list += "<@" + std::to_string(user.discord_id) + "> (" + std::to_string(user.combat_power) + " CP)\n";
			}

			embed.add_field("目前所有成員 (" + std::to_string(all_users.size()) + "人)", user_list, false);
			embed.add_field("總戰力", std::to_string(total_power), true);
			embed.add_field("平均戰力", std::to_string(all_users.empty() ? 0 : total_power / static_cast<int>(all_users.size())), true);

			event.reply(dpp::message().add_embed(embed));
		}
		else {
			event.reply("找不到該使用者");
		}
	}
	else {
		event.reply("無效的參數");
	}
}

void CommandHandler::handle_list_users(const dpp::slashcommand_t &event)
{
	auto users = team_manager.get_all_users();

	if (users.empty()) {
		event.reply("系統中沒有使用者");
		return;
	}

	dpp::embed embed = dpp::embed().set_color(0x0099ff).set_title("使用者清單");

	std::string user_list;
	int total_power = 0;

	for (const auto &user : users) {
		user_list += "<@" + std::to_string(user.discord_id) + "> (" + std::to_string(user.combat_power) + " CP)\n";
		total_power += user.combat_power;
	}

	embed.add_field("使用者 (" + std::to_string(users.size()) + "人)", user_list, false);
	embed.add_field("總戰力", std::to_string(total_power), true);
	embed.add_field("平均戰力", std::to_string(users.empty() ? 0 : total_power / static_cast<int>(users.size())), true);

	event.reply(dpp::message().add_embed(embed));
}

void CommandHandler::handle_create_teams(const dpp::slashcommand_t &event)
{
	auto teams_option = event.get_parameter("num_teams");

	if (std::holds_alternative<int64_t>(teams_option)) {
		int num_teams = static_cast<int>(std::get<int64_t>(teams_option));

		if (num_teams < 2 || num_teams > 10) {
			event.reply("隊伍數量必須在 2 到 10 之間");
			return;
		}

		auto all_users = team_manager.get_all_users();
		if (all_users.empty()) {
			event.reply("系統中沒有使用者");
			return;
		}

		if (static_cast<int>(all_users.size()) < num_teams) {
			event.reply("人數不夠分 " + std::to_string(num_teams) + "組隊伍，至少需要 " + std::to_string(num_teams) + " 人");
			return;
		}

		// Create user selection UI
		create_user_selection_interface(event, num_teams, all_users);
	}
	else {
		event.reply("無效的隊伍數量參數");
	}
}

void CommandHandler::handle_match_history(const dpp::slashcommand_t &event)
{
	auto count_option = event.get_parameter("count");
	int count = 5; // default

	if (std::holds_alternative<int64_t>(count_option)) {
		count = static_cast<int>(std::get<int64_t>(count_option));
		count = std::max(1, std::min(count, 20)); // limit between 1-20
	}

	auto recent_matches = team_manager.get_recent_matches(count);

	if (recent_matches.empty()) {
		event.reply("未找到比賽歷史紀錄");
		return;
	}

	dpp::embed embed = dpp::embed().set_title("最近的對戰紀錄");

	for (size_t i = 0; i < recent_matches.size(); ++i) {
		const auto &match = recent_matches[i];

		std::string match_info = "**日期:** " + format_timestamp(match.timestamp) + "\n";
		// match_info += "**隊伍數量：** " + std::to_string(match.teams.size());

		for (size_t t = 0; t < match.teams.size(); ++t) {
			bool is_winner = std::find(match.winning_teams.begin(), match.winning_teams.end(), static_cast<int>(t)) != match.winning_teams.end();

			match_info += "• **隊伍 " + std::to_string(t + 1);
			if (is_winner) {
				match_info += " 🏆** (獲勝)：";
			}
			else {
				match_info += "**：";
			}

			// Add team members
			for (size_t m = 0; m < match.teams[t].members.size(); ++m) {
				const auto &member = match.teams[t].members[m];
				match_info += "<@" + std::to_string(static_cast<uint64_t>(member.discord_id)) + ">" + (m < match.teams[t].members.size() - 1 ? "、" : "\n");
			}
		}

		embed.add_field("\n\n比賽 #" + std::to_string(recent_matches.size() - i), match_info, false);
	}

	event.reply(dpp::message().add_embed(embed));
}

void CommandHandler::handle_help(const dpp::slashcommand_t &event)
{
	dpp::embed embed =
			dpp::embed().set_color(0x00d4ff).set_title("🤖 何一萬 AOE 小幫手：指令說明").set_description("AOE2 Discord 分組機器人 - 讓何一萬來幫你平衡的分配隊伍");

	// 使用者管理指令
	embed.add_field("👥 **使用者管理**",
									"• `/adduser <使用者> <戰力>` - 新增使用者到系統\n"
									"• `/removeuser <使用者>` - 從系統中移除使用者\n"
									"• `/updatepower <使用者> <新戰力>` - 更新使用者的戰力值\n"
									"• `/listusers` - 列出所有註冊的使用者",
									false);

	// 分組功能
	embed.add_field("⚔️ **分組功能**",
									"• `/createteams <隊伍數量>` - 開始互動式分組\n"
									"  └ 使用按鈕選擇參與者，系統會自動平衡戰力\n"
									"  └ 支援 2-10 組隊伍，最多顯示 25 位成員",
									false);

	// 歷史紀錄
	embed.add_field("📊 **歷史紀錄**",
									"• `/history [數量]` - 查看最近的比賽記錄\n"
									"  └ 預設顯示 5 場，最多可顯示 20 場",
									false);

	// 分組流程說明
	embed.add_field("🎮 **分組流程**",
									"1️⃣ 使用 `/createteams` 開始分組\n"
									"2️⃣ 點擊按鈕選擇參與的成員\n"
									"3️⃣ 點擊「開始分組」執行分配\n"
									"4️⃣ 查看結果，可重新選擇並「重新分組」",
									false);

	// 按鈕說明
	embed.add_field("🔘 **按鈕說明**",
									"• ⬜ 灰色按鈕 = 未選擇\n"
									"• ✅ 綠色按鈕 = 已選擇\n"
									"• ⚔️ 開始分組 / 🔄 重新分組\n"
									"• ✅ 全選 • ❌ 清除選擇",
									false);

	embed.set_footer(dpp::embed_footer().set_text("💡 提示：戰力值建議範圍 0-9999，可依據玩家實力設定"));

	event.reply(dpp::message().add_embed(embed));
}

void CommandHandler::create_user_selection_interface(const dpp::slashcommand_t &event, int num_teams, const std::vector<User> &users)
{
	auto session_id = selection_manager.create_session(num_teams, users);

	dpp::embed embed = dpp::embed()
												 .set_color(0x0099ff)
												 .set_title("選擇參加分組的成員")
												 .set_description("點擊下方按鈕選擇要參加 " + std::to_string(num_teams) + " 組隊伍分配的成員\n點擊已選成員可取消選擇");

	std::string user_list;
	int total_power = 0;

	for (const auto &user : users) {
		user_list += "⬜ <@" + std::to_string(user.discord_id) + "> (" + std::to_string(user.combat_power) + " CP)\n";
		total_power += user.combat_power;
	}

	embed.add_field("可選成員 (" + std::to_string(users.size()) + "人)", user_list, false);
	embed.add_field("總戰力", std::to_string(total_power), true);
	embed.add_field("已選成員", "0人", true);
	embed.add_field("已選戰力", "0", true);

	dpp::message msg;
	msg.add_embed(embed);

	constexpr size_t MAX_BUTTONS_PER_ROW = 5;
	constexpr size_t MAX_ROWS = 5;
	constexpr size_t MAX_BUTTONS_PER_MESSAGE = MAX_BUTTONS_PER_ROW * MAX_ROWS;

	size_t users_to_show = std::min(users.size(), MAX_BUTTONS_PER_MESSAGE);

	for (size_t row = 0; row < MAX_ROWS && row * MAX_BUTTONS_PER_ROW < users_to_show; ++row) {
		dpp::component button_row;
		button_row.set_type(dpp::cot_action_row);

		for (size_t col = 0; col < MAX_BUTTONS_PER_ROW; ++col) {
			size_t user_idx = row * MAX_BUTTONS_PER_ROW + col;
			if (user_idx >= users_to_show)
				break;

			const auto &user = users[user_idx];

			button_row.add_component(dpp::component()
																	 .set_type(dpp::cot_button)
																	 .set_id("toggle_user_" + session_id + "_" + std::to_string(static_cast<uint64_t>(user.discord_id)))
																	 .set_label(user.username + " (" + std::to_string(user.combat_power) + ")")
																	 .set_style(dpp::cos_secondary)
																	 .set_emoji("⬜"));
		}

		msg.add_component(button_row);
	}

	if (users.size() > MAX_BUTTONS_PER_MESSAGE) {
		embed.set_footer(
				dpp::embed_footer().set_text("注意：只顯示前 " + std::to_string(MAX_BUTTONS_PER_MESSAGE) + " 位成員，共 " + std::to_string(users.size()) + " 位成員"));
	}

	dpp::component control_row;
	control_row.set_type(dpp::cot_action_row);

	control_row.add_component(
			dpp::component().set_type(dpp::cot_button).set_id("create_teams_" + session_id).set_label("開始分組").set_style(dpp::cos_primary).set_emoji("⚔️"));

	control_row.add_component(
			dpp::component().set_type(dpp::cot_button).set_id("select_all_users_" + session_id).set_label("全選").set_style(dpp::cos_success).set_emoji("✅"));

	control_row.add_component(
			dpp::component().set_type(dpp::cot_button).set_id("clear_selection_" + session_id).set_label("清除選擇").set_style(dpp::cos_danger).set_emoji("❌"));

	msg.add_component(control_row);
	event.reply(msg);
}

void CommandHandler::handle_button_click(const dpp::button_click_t &event)
{
	const std::string &custom_id = event.custom_id;

	if (custom_id.starts_with("create_teams_")) {
		auto session_id_opt = extract_session_id(custom_id, "create_teams_");
		if (session_id_opt) {
			handle_create_teams_button_interaction(event, *session_id_opt);
		}
	}
	else if (custom_id.starts_with("select_all_users_")) {
		auto session_id_opt = extract_session_id(custom_id, "select_all_users_");
		if (session_id_opt) {
			handle_select_all_interaction(event, *session_id_opt);
		}
	}
	else if (custom_id.starts_with("clear_selection_")) {
		auto session_id_opt = extract_session_id(custom_id, "clear_selection_");
		if (session_id_opt) {
			handle_clear_selection_interaction(event, *session_id_opt);
		}
	}
	else if (custom_id.starts_with("toggle_user_")) {
		handle_toggle_user_interaction(event);
	}
	else if (custom_id.starts_with("record_victory_")) {
		handle_record_victory_interaction(event);
	}
	else if (custom_id.starts_with("record_match_")) {
		handle_record_match_interaction(event);
	}
}

void CommandHandler::handle_select_click(const dpp::select_click_t &event)
{
	const std::string &custom_id = event.custom_id;

	if (custom_id.starts_with("user_select_")) {
		size_t first_underscore = custom_id.find('_', 12);
		size_t second_underscore = custom_id.find('_', first_underscore + 1);

		if (first_underscore != std::string::npos && second_underscore != std::string::npos) {
			std::string session_id = custom_id.substr(first_underscore + 1, second_underscore - first_underscore - 1);

			auto *session = selection_manager.get_session(session_id);
			if (session) {
				session->update_selection(event.values);
				event.reply(dpp::ir_update_message, create_selection_message(*session));
			}
		}
	}
	else if (custom_id.starts_with("select_winner_")) {
		handle_select_winner_interaction(event);
	}
}

void CommandHandler::handle_user_selection_interaction(const dpp::button_click_t &event)
{
	event.reply(dpp::ir_channel_message_with_source, dpp::message("此功能已更新，請使用新的選擇界面").set_flags(dpp::m_ephemeral));
}

void CommandHandler::handle_user_selection_interaction(const dpp::select_click_t &event)
{
	event.reply(dpp::ir_channel_message_with_source, dpp::message("此功能已更新，請使用新的選擇界面").set_flags(dpp::m_ephemeral));
}

void CommandHandler::handle_toggle_user_interaction(const dpp::button_click_t &event)
{
	const std::string &custom_id = event.custom_id;

	size_t prefix_len = std::string("toggle_user_").length();
	size_t first_underscore = custom_id.find('_', prefix_len);

	if (first_underscore == std::string::npos) {
		event.reply(dpp::ir_channel_message_with_source, dpp::message("無效的按鈕ID - 找不到會話分隔符").set_flags(dpp::m_ephemeral));
		return;
	}

	std::string session_id = custom_id.substr(prefix_len, first_underscore - prefix_len);
	std::string user_id_str = custom_id.substr(first_underscore + 1);

	if (session_id.empty() || user_id_str.empty()) {
		event.reply(dpp::ir_channel_message_with_source, dpp::message("無效的按鈕ID - 會話ID或用戶ID為空").set_flags(dpp::m_ephemeral));
		return;
	}

	auto *session = selection_manager.get_session(session_id);
	if (!session) {
		event.reply(dpp::ir_channel_message_with_source, dpp::message("會話已過期，請重新開始分組").set_flags(dpp::m_ephemeral));
		return;
	}

	try {
		uint64_t user_id = std::stoull(user_id_str);
		session->toggle_user_selection(user_id);
		event.reply(dpp::ir_update_message, create_button_selection_message(*session));
	} catch (const std::exception &e) {
		event.reply(dpp::ir_channel_message_with_source, dpp::message("無效的用戶ID: " + user_id_str).set_flags(dpp::m_ephemeral));
	}
}

void CommandHandler::handle_create_teams_button_interaction(const dpp::button_click_t &event, const std::string &session_id)
{
	auto *session = selection_manager.get_session(session_id);
	if (!session) {
		event.reply(dpp::ir_channel_message_with_source, dpp::message("會話已過期，請重新開始分組").set_flags(dpp::m_ephemeral));
		return;
	}

	auto selected_users = session->get_selected_users();
	if (selected_users.empty()) {
		event.reply(dpp::ir_channel_message_with_source, dpp::message("請至少選擇一位成員！").set_flags(dpp::m_ephemeral));
		return;
	}

	int num_teams = session->get_num_teams();
	if (static_cast<int>(selected_users.size()) < num_teams) {
		event.reply(dpp::ir_channel_message_with_source,
								dpp::message("選擇的人數不足以分 " + std::to_string(num_teams) + " 組隊伍，至少需要 " + std::to_string(num_teams) + " 人，目前只選了 " +
														 std::to_string(selected_users.size()) + " 人")
										.set_flags(dpp::m_ephemeral));
		return;
	}

	std::vector<dpp::snowflake> selected_user_ids;
	for (const auto &user : selected_users) {
		selected_user_ids.push_back(user.discord_id);
	}

	auto teams = team_manager.create_balanced_teams(selected_user_ids, num_teams);

	if (teams.empty()) {
		event.reply(dpp::ir_channel_message_with_source, dpp::message("生成隊伍失敗").set_flags(dpp::m_ephemeral));
		return;
	}

	// Store the teams in the session for later match recording
	session->set_current_teams(teams);

	// Create result message with teams and keep the selection interface
	dpp::message result_msg = create_teams_result_with_selection(*session, teams);

	event.reply(dpp::ir_update_message, result_msg);
}

void CommandHandler::handle_select_all_interaction(const dpp::button_click_t &event, const std::string &session_id)
{
	auto *session = selection_manager.get_session(session_id);
	if (!session) {
		event.reply(dpp::ir_channel_message_with_source, dpp::message("會話已過期，請重新開始分組").set_flags(dpp::m_ephemeral));
		return;
	}

	session->select_all();
	event.reply(dpp::ir_update_message, create_button_selection_message(*session));
}

void CommandHandler::handle_clear_selection_interaction(const dpp::button_click_t &event, const std::string &session_id)
{
	auto *session = selection_manager.get_session(session_id);
	if (!session) {
		event.reply(dpp::ir_channel_message_with_source, dpp::message("會話已過期，請重新開始分組").set_flags(dpp::m_ephemeral));
		return;
	}

	session->clear_selection();
	event.reply(dpp::ir_update_message, create_button_selection_message(*session));
}

void CommandHandler::handle_record_victory_interaction(const dpp::button_click_t &event)
{
	const std::string &custom_id = event.custom_id;

	size_t prefix_len = std::string("record_victory_").length();
	size_t first_underscore = custom_id.find('_', prefix_len);

	if (first_underscore == std::string::npos) {
		event.reply(dpp::ir_channel_message_with_source, dpp::message("無效的按鈕ID").set_flags(dpp::m_ephemeral));
		return;
	}

	std::string session_id = custom_id.substr(prefix_len, first_underscore - prefix_len);
	std::string team_index_str = custom_id.substr(first_underscore + 1);

	auto *session = selection_manager.get_session(session_id);
	if (!session) {
		event.reply(dpp::ir_channel_message_with_source, dpp::message("會話已過期").set_flags(dpp::m_ephemeral));
		return;
	}

	try {
		int team_index = std::stoi(team_index_str);

		auto teams = session->get_current_teams();
		if (teams.empty() || team_index >= static_cast<int>(teams.size())) {
			event.reply(dpp::ir_channel_message_with_source, dpp::message("無效的隊伍索引").set_flags(dpp::m_ephemeral));
			return;
		}

		std::vector<int> winning_teams = {team_index};
		team_manager.record_match(teams, winning_teams);

		dpp::embed embed = dpp::embed()
													 .set_color(0x00ff00)
													 .set_title("🏆 比賽結果已記錄！")
													 .set_description("隊伍 " + std::to_string(team_index + 1) + " 獲勝")
													 .add_field("記錄時間", format_timestamp(std::chrono::system_clock::now()), false);

		event.reply(dpp::ir_channel_message_with_source, dpp::message().add_embed(embed).set_flags(dpp::m_ephemeral));

	} catch (const std::exception &) {
		event.reply(dpp::ir_channel_message_with_source, dpp::message("無效的隊伍索引").set_flags(dpp::m_ephemeral));
	}
}

void CommandHandler::handle_record_match_interaction(const dpp::button_click_t &event)
{
	const std::string &custom_id = event.custom_id;

	size_t prefix_len = std::string("record_match_").length();
	std::string session_id = custom_id.substr(prefix_len);

	auto *session = selection_manager.get_session(session_id);
	if (!session) {
		event.reply(dpp::ir_channel_message_with_source, dpp::message("會話已過期").set_flags(dpp::m_ephemeral));
		return;
	}

	auto teams = session->get_current_teams();
	if (teams.empty()) {
		event.reply(dpp::ir_channel_message_with_source, dpp::message("沒有找到隊伍資料").set_flags(dpp::m_ephemeral));
		return;
	}

	dpp::embed embed = dpp::embed().set_color(0x0099ff).set_title("記錄比賽結果").set_description("請選擇獲勝的隊伍：");

	dpp::message msg;
	msg.add_embed(embed);

	dpp::component select_menu;
	select_menu.set_type(dpp::cot_selectmenu).set_placeholder("選擇獲勝隊伍").set_min_values(1).set_max_values(1).set_id("select_winner_" + session_id);

	for (size_t i = 0; i < teams.size(); ++i) {
		select_menu.add_select_option(dpp::select_option("隊伍 " + std::to_string(i + 1) + " (戰力: " + std::to_string(teams[i].total_power) + ")",
																										 std::to_string(i), "選擇隊伍 " + std::to_string(i + 1) + " 為獲勝隊伍"));
	}

	msg.add_component(dpp::component().add_component(select_menu));

	event.reply(dpp::ir_channel_message_with_source, msg.set_flags(dpp::m_ephemeral));
}

void CommandHandler::handle_select_winner_interaction(const dpp::select_click_t &event)
{
	const std::string &custom_id = event.custom_id;

	size_t prefix_len = std::string("select_winner_").length();
	std::string session_id = custom_id.substr(prefix_len);

	auto *session = selection_manager.get_session(session_id);
	if (!session) {
		event.reply(dpp::ir_channel_message_with_source, dpp::message("會話已過期").set_flags(dpp::m_ephemeral));
		return;
	}

	if (event.values.empty()) {
		event.reply(dpp::ir_channel_message_with_source, dpp::message("請選擇一個隊伍").set_flags(dpp::m_ephemeral));
		return;
	}

	try {
		int team_index = std::stoi(event.values[0]);

		auto teams = session->get_current_teams();
		if (teams.empty() || team_index >= static_cast<int>(teams.size())) {
			event.reply(dpp::ir_channel_message_with_source, dpp::message("無效的隊伍索引").set_flags(dpp::m_ephemeral));
			return;
		}

		std::vector<int> winning_teams = {team_index};
		team_manager.record_match(teams, winning_teams);

		dpp::embed embed = dpp::embed()
													 .set_color(0x00ff00)
													 .set_title("🏆 比賽結果已記錄！")
													 .set_description("隊伍 " + std::to_string(team_index + 1) + " 獲勝")
													 .add_field("記錄時間", format_timestamp(std::chrono::system_clock::now()), false);

		event.reply(dpp::ir_update_message, dpp::message().add_embed(embed));

	} catch (const std::exception &) {
		event.reply(dpp::ir_channel_message_with_source, dpp::message("無效的隊伍索引").set_flags(dpp::m_ephemeral));
	}
}

std::optional<std::string> CommandHandler::extract_session_id(const std::string &custom_id, const std::string &prefix)
{
	if (!custom_id.starts_with(prefix)) {
		return std::nullopt;
	}
	return custom_id.substr(prefix.length());
}

dpp::message CommandHandler::create_selection_message(const UserSelectionSession &session)
{
	const auto &users = session.get_available_users();
	const auto &selected_ids = session.get_selected_user_ids();

	dpp::embed embed = dpp::embed()
												 .set_color(0x0099ff)
												 .set_title("選擇參加分組的成員")
												 .set_description("請選擇要參加 " + std::to_string(session.get_num_teams()) + " 組隊伍分配的成員，然後點擊「開始分組」按鈕");

	std::string user_list;
	int total_power = 0;
	int selected_power = 0;
	int selected_count = 0;

	for (const auto &user : users) {
		bool is_selected = selected_ids.contains(static_cast<uint64_t>(user.discord_id));
		std::string status_icon = is_selected ? "✅ " : "⬜ ";
		user_list += status_icon + "<@" + std::to_string(user.discord_id) + "> (" + std::to_string(user.combat_power) + " CP)\n";
		total_power += user.combat_power;
		if (is_selected) {
			selected_power += user.combat_power;
			selected_count++;
		}
	}

	embed.add_field("可選成員 (" + std::to_string(users.size()) + "人)", user_list, false);
	embed.add_field("已選成員", std::to_string(selected_count) + "人", true);
	embed.add_field("已選戰力", std::to_string(selected_power), true);

	dpp::message msg;
	msg.add_embed(embed);
	return msg;
}

dpp::message CommandHandler::create_button_selection_message(const UserSelectionSession &session)
{
	const auto &users = session.get_available_users();
	const auto &selected_ids = session.get_selected_user_ids();

	dpp::embed embed = dpp::embed()
												 .set_color(0x0099ff)
												 .set_title("選擇參加分組的成員")
												 .set_description("點擊下方按鈕選擇要參加 " + std::to_string(session.get_num_teams()) + " 組隊伍分配的成員\n點擊已選成員可取消選擇");

	std::string user_list;
	int total_power = 0;
	int selected_power = 0;
	int selected_count = 0;

	for (const auto &user : users) {
		bool is_selected = selected_ids.contains(static_cast<uint64_t>(user.discord_id));
		std::string status_icon = is_selected ? "✅ " : "⬜ ";
		user_list += status_icon + "<@" + std::to_string(user.discord_id) + "> (" + std::to_string(user.combat_power) + " CP)\n";
		total_power += user.combat_power;
		if (is_selected) {
			selected_power += user.combat_power;
			selected_count++;
		}
	}

	embed.add_field("可選成員 (" + std::to_string(users.size()) + "人)", user_list, false);
	embed.add_field("已選成員", std::to_string(selected_count) + "人", true);
	embed.add_field("已選戰力", std::to_string(selected_power), true);
	embed.add_field("總戰力", std::to_string(total_power), true);

	dpp::message msg;
	msg.add_embed(embed);

	constexpr size_t MAX_BUTTONS_PER_ROW = 5;
	constexpr size_t MAX_ROWS = 5;
	constexpr size_t MAX_BUTTONS_PER_MESSAGE = MAX_BUTTONS_PER_ROW * MAX_ROWS;

	size_t users_to_show = std::min(users.size(), MAX_BUTTONS_PER_MESSAGE);

	for (size_t row = 0; row < MAX_ROWS && row * MAX_BUTTONS_PER_ROW < users_to_show; ++row) {
		dpp::component button_row;
		button_row.set_type(dpp::cot_action_row);

		for (size_t col = 0; col < MAX_BUTTONS_PER_ROW; ++col) {
			size_t user_idx = row * MAX_BUTTONS_PER_ROW + col;
			if (user_idx >= users_to_show)
				break;

			const auto &user = users[user_idx];
			bool is_selected = selected_ids.contains(static_cast<uint64_t>(user.discord_id));

			button_row.add_component(dpp::component()
																	 .set_type(dpp::cot_button)
																	 .set_id("toggle_user_" + session.get_session_id() + "_" + std::to_string(static_cast<uint64_t>(user.discord_id)))
																	 .set_label(user.username + " (" + std::to_string(user.combat_power) + ")")
																	 .set_style(is_selected ? dpp::cos_success : dpp::cos_secondary)
																	 .set_emoji(is_selected ? "✅" : "⬜"));
		}

		msg.add_component(button_row);
	}

	if (users.size() > MAX_BUTTONS_PER_MESSAGE) {
		embed.set_footer(
				dpp::embed_footer().set_text("注意：只顯示前 " + std::to_string(MAX_BUTTONS_PER_MESSAGE) + " 位成員，共 " + std::to_string(users.size()) + " 位成員"));
	}

	dpp::component control_row;
	control_row.set_type(dpp::cot_action_row);

	control_row.add_component(dpp::component()
																.set_type(dpp::cot_button)
																.set_id("create_teams_" + session.get_session_id())
																.set_label("開始分組")
																.set_style(dpp::cos_primary)
																.set_emoji("⚔️"));

	control_row.add_component(dpp::component()
																.set_type(dpp::cot_button)
																.set_id("select_all_users_" + session.get_session_id())
																.set_label("全選")
																.set_style(dpp::cos_success)
																.set_emoji("✅"));

	control_row.add_component(dpp::component()
																.set_type(dpp::cot_button)
																.set_id("clear_selection_" + session.get_session_id())
																.set_label("清除選擇")
																.set_style(dpp::cos_danger)
																.set_emoji("❌"));

	msg.add_component(control_row);
	return msg;
}

dpp::message CommandHandler::create_teams_result_with_selection(const UserSelectionSession &session, const std::vector<Team> &teams)
{
	const auto &users = session.get_available_users();
	const auto &selected_ids = session.get_selected_user_ids();

	dpp::embed embed = dpp::embed().set_color(0x00ff00).set_title("🏆 分組結果 - " + std::to_string(teams.size()) + " 組隊伍");

	for (size_t i = 0; i < teams.size(); ++i) {
		std::string team_info;
		for (const auto &member : teams[i].members) {
			team_info += "<@" + std::to_string(static_cast<uint64_t>(member.discord_id)) + "> (" + std::to_string(member.combat_power) + " CP)\n";
		}
		team_info += "**總戰力: " + std::to_string(teams[i].total_power) + "**";

		embed.add_field("隊伍 " + std::to_string(i + 1), team_info, true);
	}

	if (teams.size() >= 2) {
		auto min_max = std::minmax_element(teams.begin(), teams.end(), [](const Team &a, const Team &b) { return a.total_power < b.total_power; });
		int power_difference = min_max.second->total_power - min_max.first->total_power;
		embed.add_field("戰力差", std::to_string(power_difference) + "分", false);
	}

	std::string selected_user_list;
	int selected_power = 0;
	int selected_count = 0;

	for (const auto &user : users) {
		bool is_selected = selected_ids.contains(static_cast<uint64_t>(user.discord_id));
		if (is_selected) {
			selected_user_list += "✅ <@" + std::to_string(user.discord_id) + "> (" + std::to_string(user.combat_power) + " CP)\n";
			selected_power += user.combat_power;
			selected_count++;
		}
	}

	embed.add_field("參與分組的成員 (" + std::to_string(selected_count) + "人)", selected_user_list, false);

	dpp::message msg;
	msg.add_embed(embed);

	constexpr size_t MAX_BUTTONS_PER_ROW = 5;
	constexpr size_t MAX_ROWS = 5;
	constexpr size_t MAX_BUTTONS_PER_MESSAGE = MAX_BUTTONS_PER_ROW * MAX_ROWS;

	size_t users_to_show = std::min(users.size(), MAX_BUTTONS_PER_MESSAGE);

	for (size_t row = 0; row < MAX_ROWS && row * MAX_BUTTONS_PER_ROW < users_to_show; ++row) {
		dpp::component button_row;
		button_row.set_type(dpp::cot_action_row);

		for (size_t col = 0; col < MAX_BUTTONS_PER_ROW; ++col) {
			size_t user_idx = row * MAX_BUTTONS_PER_ROW + col;
			if (user_idx >= users_to_show)
				break;

			const auto &user = users[user_idx];
			bool is_selected = selected_ids.contains(static_cast<uint64_t>(user.discord_id));

			button_row.add_component(dpp::component()
																	 .set_type(dpp::cot_button)
																	 .set_id("toggle_user_" + session.get_session_id() + "_" + std::to_string(static_cast<uint64_t>(user.discord_id)))
																	 .set_label(user.username + " (" + std::to_string(user.combat_power) + ")")
																	 .set_style(is_selected ? dpp::cos_success : dpp::cos_secondary)
																	 .set_emoji(is_selected ? "✅" : "⬜"));
		}

		msg.add_component(button_row);
	}

	dpp::component control_row;
	control_row.set_type(dpp::cot_action_row);

	control_row.add_component(dpp::component()
																.set_type(dpp::cot_button)
																.set_id("create_teams_" + session.get_session_id())
																.set_label("重新分組")
																.set_style(dpp::cos_primary)
																.set_emoji("🔄"));

	control_row.add_component(dpp::component()
																.set_type(dpp::cot_button)
																.set_id("select_all_users_" + session.get_session_id())
																.set_label("全選")
																.set_style(dpp::cos_success)
																.set_emoji("✅"));

	control_row.add_component(dpp::component()
																.set_type(dpp::cot_button)
																.set_id("clear_selection_" + session.get_session_id())
																.set_label("清除選擇")
																.set_style(dpp::cos_danger)
																.set_emoji("❌"));

	msg.add_component(control_row);

	// Add victory recording buttons
	if (teams.size() <= 5) { // Only if we have 5 or fewer teams (button limit)
		dpp::component victory_row;
		victory_row.set_type(dpp::cot_action_row);

		for (size_t i = 0; i < teams.size(); ++i) {
			victory_row.add_component(dpp::component()
																		.set_type(dpp::cot_button)
																		.set_id("record_victory_" + session.get_session_id() + "_" + std::to_string(i))
																		.set_label("隊伍" + std::to_string(i + 1) + "獲勝")
																		.set_style(dpp::cos_success)
																		.set_emoji("🏆"));
		}

		msg.add_component(victory_row);
	}

	// If more than 5 teams, add a general "記錄比賽" button
	if (teams.size() > 5) {
		dpp::component record_row;
		record_row.set_type(dpp::cot_action_row);

		record_row.add_component(dpp::component()
																 .set_type(dpp::cot_button)
																 .set_id("record_match_" + session.get_session_id())
																 .set_label("記錄比賽結果")
																 .set_style(dpp::cos_success)
																 .set_emoji("📝"));

		msg.add_component(record_row);
	}

	return msg;
}

dpp::message CommandHandler::create_teams_result_message(const std::vector<Team> &teams)
{
	dpp::embed embed = dpp::embed().set_color(0x00ff00).set_title("生成了 " + std::to_string(teams.size()) + " 組隊伍");

	for (size_t i = 0; i < teams.size(); ++i) {
		std::string team_info;
		for (const auto &member : teams[i].members) {
			team_info += "<@" + std::to_string(static_cast<uint64_t>(member.discord_id)) + "> (" + std::to_string(member.combat_power) + " CP)\n";
		}
		team_info += "**總戰力: " + std::to_string(teams[i].total_power) + "**";

		embed.add_field("隊伍 " + std::to_string(i + 1), team_info, true);
	}

	if (teams.size() >= 2) {
		auto min_max = std::minmax_element(teams.begin(), teams.end(), [](const Team &a, const Team &b) { return a.total_power < b.total_power; });
		int power_difference = min_max.second->total_power - min_max.first->total_power;
		embed.add_field("戰力差", std::to_string(power_difference) + "分", false);
	}

	dpp::message msg;
	msg.add_embed(embed);
	return msg;
}

std::vector<dpp::slashcommand> CommandHandler::create_commands(dpp::snowflake bot_id)
{
	return {dpp::slashcommand("help", "顯示所有可用指令的說明", bot_id),

					dpp::slashcommand("adduser", "新增一個使用者，要給他一個戰力分數", bot_id)
							.add_option(dpp::command_option(dpp::co_user, "user", "要新增的使用者", true))
							.add_option(dpp::command_option(dpp::co_integer, "combat_power", "使用者的戰力 (0-9999)", true).set_min_value(0).set_max_value(9999)),

					dpp::slashcommand("removeuser", "移除系統中的使用者", bot_id).add_option(dpp::command_option(dpp::co_user, "user", "要移除的使用者", true)),

					dpp::slashcommand("updatepower", "更新使用者的戰力", bot_id)
							.add_option(dpp::command_option(dpp::co_user, "user", "要更新的使用者", true))
							.add_option(dpp::command_option(dpp::co_integer, "new_power", "新的使用者戰力 (0-9999)", true).set_min_value(0).set_max_value(9999)),

					dpp::slashcommand("listusers", "列出目前所有的玩家", bot_id),

					dpp::slashcommand("createteams", "創建平衡的隊伍", bot_id)
							.add_option(dpp::command_option(dpp::co_integer, "num_teams", "隊伍數量 (2-10)", true).set_min_value(2).set_max_value(10)),

					dpp::slashcommand("history", "查看比賽歷史", bot_id)
							.add_option(dpp::command_option(dpp::co_integer, "count", "要顯示近幾場比賽 (1-20)", false).set_min_value(1).set_max_value(20))};
}