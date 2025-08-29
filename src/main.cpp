#include "handlers/command_handler.hpp"
#include "handlers/interaction_handler.hpp"
#include "handlers/session_manager.hpp"
#include "services/match_service.hpp"
#include "services/persistence_service.hpp"
#include "services/team_service.hpp"
#include "ui/message_builder.hpp"
#include "ui/panel_builder.hpp"
#include <dpp/dpp.h>

#include <fstream>
#include <iostream>
#include <memory>

using namespace terry;

static constexpr dpp::snowflake guild_id = 1038042178439614505;

int main()
{
	// Read bot token
	std::string token;
	if (!(std::ifstream(".bot_token") >> token)) {
		std::cerr << "載入 .bot_token 失敗\n";
		return 1;
	}

	// Initialize services
	auto persistence = std::make_shared<persistence_service>();
	auto team_svc = std::make_shared<team_service>();
	auto match_svc = std::make_shared<match_service>(persistence);
	auto session_mgr = std::make_shared<session_manager>();
	auto panel_bld = std::make_shared<panel_builder>();

	// Load data
	if (auto res = match_svc->load(); !res) {
		std::cerr << "Load data warning: " << res.error().what() << "\n";
	}

	// Create handlers
	auto cmd_handler = std::make_shared<command_handler>(team_svc, match_svc, session_mgr, panel_bld);
	auto int_handler = std::make_shared<interaction_handler>(team_svc, match_svc, session_mgr, panel_bld);

	// Create bot
	dpp::cluster bot(token);

	// Wire events
	bot.on_slashcommand([cmd_handler](const dpp::slashcommand_t &ev) {
		try {
			cmd_handler->on_slash(ev);
		} catch (const std::exception &e) {
			ui::message_builder::reply_error(ev, e.what());
		}
	});

	bot.on_button_click([int_handler](const dpp::button_click_t &ev) {
		try {
			int_handler->on_button(ev);
		} catch (const std::exception &e) {
			ui::message_builder::reply_error(ev, e.what());
		}
	});

	bot.on_select_click([int_handler](const dpp::select_click_t &ev) {
		try {
			int_handler->on_select(ev);
		} catch (const std::exception &e) {
			ui::message_builder::reply_error(ev, e.what());
		}
	});

	bot.on_ready([&](const dpp::ready_t &) {
		// Clear global commands
		bot.global_bulk_command_create({});

		// Register guild commands
		auto cmds = command_handler::commands(bot.me.id);
		bot.guild_bulk_command_create(cmds, guild_id);
	});

	bot.on_log(dpp::utility::cout_logger());

	// Start bot
	bot.start(dpp::st_wait);

	// Save on exit
	if (auto res = match_svc->save(); !res) {
		std::cerr << "Save error: " << res.error().what() << "\n";
	}

	return 0;
}
