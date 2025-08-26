#include "CommandHandler.hpp"
#include "TeamManager.hpp"
#include <dpp/dpp.h>

#include <csignal>
#include <fstream>
#include <iostream>

namespace {
constexpr dpp::snowflake GUILD_ID = 1038042178439614505;

// Global variables for graceful shutdown
TeamManager *g_team_manager = nullptr;

void signal_handler(int signal)
{
	std::cout << "\nReceived signal " << signal << ". Shutting down bot gracefully...\n";

	if (g_team_manager) {
		std::cout << "Saving data...\n";
		g_team_manager->save_data();
	}

	std::cout << "Goodbye!\n";
	std::exit(0);
}
} // namespace

int main()
{
	// Read bot token
	std::string token;
	if (!(std::ifstream(".bot_token") >> token)) {
		std::cerr << "Failed to read bot token from .bot_token file\n";
		std::cerr << "Please create a .bot_token file containing your Discord bot token.\n";
		return 1;
	}

	// Initialize team manager and load existing data
	TeamManager team_manager;
	g_team_manager = &team_manager;

	std::cout << "Loading existing data...\n";
	team_manager.load_data();

	// Initialize command handler
	CommandHandler command_handler(team_manager);

	// Create bot instance
	dpp::cluster bot(token, dpp::i_default_intents | dpp::i_message_content | dpp::i_guild_members);

	// Set up logging
	bot.on_log(dpp::utility::cout_logger());

	// Handle slash commands
	bot.on_slashcommand([&command_handler](const dpp::slashcommand_t &event) {
		try {
			command_handler.handle_command(event);
		} catch (const std::exception &e) {
			std::cerr << "Error handling command '" << event.command.get_command_name() << "': " << e.what() << std::endl;
			event.reply("An error occurred while processing the command.");
		}
	});

	// Handle button clicks
	bot.on_button_click([&command_handler](const dpp::button_click_t &event) {
		try {
			command_handler.handle_button_click(event);
		} catch (const std::exception &e) {
			std::cerr << "Error handling button click '" << event.custom_id << "': " << e.what() << std::endl;
			event.reply(dpp::ir_channel_message_with_source, dpp::message("處理按鈕點擊時發生錯誤").set_flags(dpp::m_ephemeral));
		}
	});

	// Handle select menu interactions
	bot.on_select_click([&command_handler](const dpp::select_click_t &event) {
		try {
			command_handler.handle_select_click(event);
		} catch (const std::exception &e) {
			std::cerr << "Error handling select click '" << event.custom_id << "': " << e.what() << std::endl;
			event.reply(dpp::ir_channel_message_with_source, dpp::message("處理選單互動時發生錯誤").set_flags(dpp::m_ephemeral));
		}
	});

	// Bot ready event - register commands
	bot.on_ready([&bot](const dpp::ready_t &) {
		std::cout << "Bot is ready! Logged in as " << bot.me.username << std::endl;

		if (dpp::run_once<struct register_bot_commands>()) {
			std::cout << "Registering commands...\n";

			auto commands = CommandHandler::create_commands(bot.me.id);

			bot.guild_bulk_command_create(commands, GUILD_ID, [](const dpp::confirmation_callback_t &cc) {
				if (cc.is_error())
					std::cerr << "Bulk register failed: " << cc.get_error().message << '\n';
				else
					std::cout << "Commands registered (bulk) for guild.\n";
			});
		}
	});

	// Handle guild member updates to keep usernames in sync
	bot.on_guild_member_update([&team_manager](const dpp::guild_member_update_t &event) {
		auto *user = team_manager.get_user(event.updated.user_id);
		if (user && user->username != event.updated.get_user()->username) {
			// Update username in our records
			team_manager.add_user(event.updated.user_id, event.updated.get_user()->username, user->combat_power);
		}
	});

	// Set up signal handlers for graceful shutdown
	std::signal(SIGINT, signal_handler);
	std::signal(SIGTERM, signal_handler);

	std::cout << "Starting bot...\n";
	std::cout << "Press Ctrl+C to stop the bot gracefully.\n";

	// Start the bot
	try {
		bot.start(dpp::st_wait);
	} catch (const std::exception &e) {
		std::cerr << "Bot encountered an error: " << e.what() << std::endl;

		// Save data before exiting
		std::cout << "Saving data before exit...\n";
		team_manager.save_data();

		return 1;
	}

	// This should never be reached due to st_wait, but just in case
	std::cout << "Bot stopped. Saving data...\n";
	team_manager.save_data();

	return 0;
}