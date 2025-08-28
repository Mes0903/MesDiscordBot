/**
 * @brief
 * Boot sequence:
 *   - Read bot token from .bot_token
 *   - Create dpp::cluster and wire handlers (slash, button, select, ready, log)
 *   - Load persistent data via team_manager::load()
 *   - On ready, register global commands once
 *   - Start the event loop
 *   - On exit, save state via team_manager::save()
 */

#include "command_handler.hpp"
#include "team_manager.hpp"
#include <dpp/dpp.h>

#include <fstream>
#include <iostream>

using namespace terry::bot;

int main()
{
	std::string token;
	if (!(std::ifstream(".bot_token") >> token)) [[unlikely]] {
		std::cerr << "Failed to read .bot_token\n";
		return 1;
	}

	dpp::cluster bot(token);

	team_manager tm;
	if (auto res = tm.load(); !res) [[unlikely]] {
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
		auto cmds = command_handler::commands(bot.me.id);
		bot.global_bulk_command_create(cmds, [](const auto &cb) {
			if (cb.is_error()) [[unlikely]] {
				std::cerr << "Global command upsert failed: " << cb.get_error().message << "\n";
			}
		});
	});

	bot.on_log(dpp::utility::cout_logger());

	bot.start(dpp::st_wait);

	if (auto res = tm.save(); !res) [[unlikely]] {
		std::cerr << "Save error: " << res.error().message << "\n";
	}

	return 0;
}
