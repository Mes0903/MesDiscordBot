#pragma once

#include "core/utils.hpp"
#include "models/team.hpp"

#include <chrono>
#include <optional>
#include <string>
#include <unordered_map>

namespace terry {

enum class panel_type { formteams, sethistory };

struct panel_session {
	std::string panel_id{};
	dpp::snowflake guild_id{};
	dpp::snowflake channel_id{};
	dpp::snowflake owner_id{};

	panel_type type{panel_type::formteams};
	bool active{true};

	// Session data
	int num_teams{2};
	std::vector<dpp::snowflake> selected_users{};
	std::vector<team> formed_teams{};
	std::optional<std::size_t> selected_match_index{};

	// Timestamps
	std::chrono::steady_clock::time_point created_at{std::chrono::steady_clock::now()};
	std::chrono::steady_clock::time_point last_accessed_at{created_at};
};

class session_manager {
public:
	[[nodiscard]] auto create_session(panel_session session) -> std::string;
	[[nodiscard]] auto get_session(std::string_view id) -> std::optional<std::reference_wrapper<panel_session>>;
	[[nodiscard]] auto validate_owner(std::string_view id, dpp::snowflake owner) -> std::expected<std::monostate, type::error>;

	auto remove_session(std::string_view id) -> void;
	auto cleanup_old_sessions(std::size_t max_sessions = 8) -> void;

private:
	std::unordered_map<std::string, panel_session> sessions_;
	[[nodiscard]] static auto generate_token() -> std::string;
};

} // namespace terry
