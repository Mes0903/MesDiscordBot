#pragma once

#include "core/utils.hpp"
#include "handlers/session_manager.hpp"
#include "models/user.hpp"
#include <dpp/dpp.h>

#include <span>

namespace terry {

class panel_builder {
public:
	// Build team formation panel
	[[nodiscard]] auto build_formteams_panel(const panel_session &sess, std::span<const user> all_users) const -> dpp::message;

	// Build winner selection panel
	[[nodiscard]] auto build_setwinner_panel(const panel_session &sess, std::span<const std::pair<std::size_t, match_record>> recent_matches) const
			-> dpp::message;

private:
	// Helper methods
	[[nodiscard]] auto create_user_select_menu(const std::string &panel_id, std::span<const user> users, std::span<const dpp::snowflake> selected) const
			-> dpp::component;

	[[nodiscard]] auto create_match_select_menu(const std::string &panel_id, std::span<const std::pair<std::size_t, match_record>> matches,
																							std::optional<std::size_t> selected) const -> dpp::component;
};

} // namespace terry
