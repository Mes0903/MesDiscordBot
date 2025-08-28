/**
 * @brief
 * Boot sequence:
 *   - Read bot token from .bot_token
 *   - Create dpp::cluster and wire handlers (slash, button, select, ready, log)
 *   - Load persistent data via team_manager::load()
 *   - On ready, register (or upsert) guild commands once
 *   - Start the event loop
 *   - On exit, save state via team_manager::save()
 */

#include "command_handler.hpp"
#include "team_manager.hpp"
#include <dpp/dpp.h>

#include <fstream>
#include <iostream>

using namespace terry::bot;

static constexpr dpp::snowflake GUILD_ID = 1038042178439614505;

int main()
{
	std::string token;
	if (!(std::ifstream(".bot_token") >> token)) {
		std::cerr << "Failed to read .bot_token\n";
		return 1;
	}

	dpp::cluster bot(token);

	team_manager tm;
	if (auto res = tm.load(); !res) {
		std::cerr << "Warning: " << res.error().message << "\n";
	}

	command_handler ch{tm};

	bot.on_slashcommand([&](const dpp::slashcommand_t &ev) {
		try {
			ch.on_slash(ev);
		} catch (const std::exception &e) {
			reply_err(ev, e.what());
		}
	});

	bot.on_button_click([&](const dpp::button_click_t &ev) {
		try {
			ch.on_button(ev);
		} catch (const std::exception &e) {
			reply_err(ev, e.what());
		}
	});

	bot.on_select_click([&](const dpp::select_click_t &ev) {
		try {
			ch.on_select(ev);
		} catch (const std::exception &e) {
			reply_err(ev, e.what());
		}
	});

	bot.on_ready([&](const dpp::ready_t &) {
		bot.global_bulk_command_create({});

		auto cmds = command_handler::commands(bot.me.id);
		bot.guild_bulk_command_create(cmds, GUILD_ID, [](auto) { /* ignore callback */ });
	});

	bot.on_log(dpp::utility::cout_logger());

	bot.start(dpp::st_wait);

	if (auto res = tm.save(); !res) {
		std::cerr << "Save error: " << res.error().message << "\n";
	}

	return 0;
}