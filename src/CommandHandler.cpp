#include "CommandHandler.hpp"

#include <algorithm>

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

		// For now, we'll use a placeholder username and update it later
		// In a future version, we can implement proper username fetching
		std::string username = "User_" + std::to_string(static_cast<uint64_t>(user_id));

		team_manager.add_user(user_id, username, combat_power);

		dpp::embed embed = dpp::embed()
													 .set_color(0x00ff00)
													 .set_title("新增使用者")
													 .add_field("使用者", "<@" + std::to_string(user_id) + ">", true)
													 .add_field("戰力", std::to_string(combat_power), true);

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
			dpp::embed embed = dpp::embed().set_color(0xff0000).set_title("使用者已移除").add_field("使用者", "<@" + std::to_string(user_id) + ">", true);

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

		if (team_manager.update_combat_power(user_id, new_power)) {
			dpp::embed embed = dpp::embed()
														 .set_color(0x0099ff)
														 .set_title("更新戰力分數")
														 .add_field("使用者", "<@" + std::to_string(user_id) + ">", true)
														 .add_field("新戰力分數", std::to_string(new_power), true);

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
		user_list += "<@" + std::to_string(user.discord_id) + "> " + " (" + std::to_string(user.combat_power) + " CP)\n";
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

		// For now, use all registered users. In a future version,
		// you could add a way to specify participants
		auto all_users = team_manager.get_all_users();
		std::vector<dpp::snowflake> participant_ids;

		for (const auto &user : all_users) {
			participant_ids.push_back(user.discord_id);
		}

		if (participant_ids.empty()) {
			event.reply("系統中沒有使用者");
			return;
		}

		if (static_cast<int>(participant_ids.size()) < num_teams) {
			event.reply("人數不夠分 " + std::to_string(num_teams) + " 組隊伍，至少需要 " + std::to_string(num_teams) + " 人");
			return;
		}

		auto teams = team_manager.create_balanced_teams(participant_ids, num_teams);

		if (teams.empty()) {
			event.reply("生成隊伍失敗");
			return;
		}

		dpp::embed embed = dpp::embed().set_color(0x00ff00).set_title("生成了 " + std::to_string(num_teams) + " 組隊伍");

		for (size_t i = 0; i < teams.size(); ++i) {
			std::string team_info;
			for (const auto &member : teams[i].members) {
				team_info += "<@" + std::to_string(static_cast<uint64_t>(member.discord_id)) + ">" + " (" + std::to_string(member.combat_power) + " CP)\n";
			}
			team_info += "**總戰力: " + std::to_string(teams[i].total_power) + "**";

			embed.add_field("隊伍 " + std::to_string(i + 1), team_info, true);
		}

		// Add balance information
		if (teams.size() >= 2) {
			auto min_max = std::minmax_element(teams.begin(), teams.end(), [](const Team &a, const Team &b) { return a.total_power < b.total_power; });

			int power_difference = min_max.second->total_power - min_max.first->total_power;
			embed.add_field("戰力差", std::to_string(power_difference) + "分", false);
		}

		event.reply(dpp::message().add_embed(embed));
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

	dpp::embed embed = dpp::embed().set_color(0x9932cc).set_title("最近的對戰紀錄");

	for (size_t i = 0; i < recent_matches.size(); ++i) {
		const auto &match = recent_matches[i];

		std::string match_info = "**日期:** " + format_timestamp(match.timestamp) + "\n";
		match_info += "**隊伍：** " + std::to_string(match.teams.size()) + "\n";

		for (size_t t = 0; t < match.teams.size(); ++t) {
			match_info += "隊伍 " + std::to_string(t + 1) + " (" + std::to_string(match.teams[t].total_power) + " CP)";

			if (std::find(match.winning_teams.begin(), match.winning_teams.end(), static_cast<int>(t)) != match.winning_teams.end()) {
				match_info += " 🏆";
			}
			match_info += "\n";
		}

		embed.add_field("Match #" + std::to_string(recent_matches.size() - i), match_info, false);
	}

	event.reply(dpp::message().add_embed(embed));
}

std::vector<dpp::slashcommand> CommandHandler::create_commands(dpp::snowflake bot_id)
{
	return {dpp::slashcommand("adduser", "新增一個使用者，要給他一個戰力分數", bot_id)
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
