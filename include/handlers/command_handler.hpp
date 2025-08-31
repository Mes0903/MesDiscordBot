#pragma once

#include "core/utils.hpp"
#include "handlers/session_manager.hpp"
#include "services/match_service.hpp"
#include "services/team_service.hpp"
#include "ui/panel_builder.hpp"
#include <dpp/dpp.h>

#include <memory>

namespace terry {

class command_handler {
public:
	explicit command_handler(std::shared_ptr<team_service> team_svc, std::shared_ptr<match_service> match_svc, std::shared_ptr<session_manager> session_mgr,
													 std::shared_ptr<panel_builder> panel_bld);

	// Command dispatch
	auto on_slash(const dpp::slashcommand_t &ev) -> void;

	// Get command definitions for registration
	[[nodiscard]] static auto commands(dpp::snowflake bot_id) -> std::vector<dpp::slashcommand>;

private:
	std::shared_ptr<team_service> team_svc_;
	std::shared_ptr<match_service> match_svc_;
	std::shared_ptr<session_manager> session_mgr_;
	std::shared_ptr<panel_builder> panel_bld_;

	// Command implementations
	auto cmd_help(const dpp::slashcommand_t &ev) -> void;
	auto cmd_adduser(const dpp::slashcommand_t &ev) -> void;
	auto cmd_removeuser(const dpp::slashcommand_t &ev) -> void;
	auto cmd_listusers(const dpp::slashcommand_t &ev) -> void;
	auto cmd_formteams(const dpp::slashcommand_t &ev) -> void;
	auto cmd_history(const dpp::slashcommand_t &ev) -> void;
	auto cmd_sethistory(const dpp::slashcommand_t &ev) -> void;
};

} // namespace terry
