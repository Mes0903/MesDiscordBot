#pragma once

#include "models/match.hpp"
#include "models/user.hpp"
#include <dpp/dpp.h>

#include <span>

namespace terry::ui {

class embed_builder {
public:
	// Build help embed
	[[nodiscard]] static auto build_help() -> dpp::embed;

	// Build user list embed
	[[nodiscard]] static auto build_user_list(std::span<const user> users) -> dpp::embed;

	// Build match history embed
	[[nodiscard]] static auto build_history(std::span<const match_record> matches) -> dpp::embed;

	// Build team formation result embed
	[[nodiscard]] static auto build_teams(std::span<const team> teams) -> dpp::embed;

private:
	// Format helpers
	[[nodiscard]] static auto format_team_members(const team &t) -> std::string;
};

} // namespace terry::ui
