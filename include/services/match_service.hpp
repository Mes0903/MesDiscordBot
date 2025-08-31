#pragma once

#include "core/utils.hpp"
#include "models/match.hpp"
#include "models/user.hpp"
#include "services/persistence_service.hpp"

#include <memory>
#include <span>
#include <vector>

namespace terry {

class match_service {
public:
	explicit match_service(std::shared_ptr<persistence_service> persistence);

	// User management
	[[nodiscard]] auto find_user(dpp::snowflake id) const -> std::optional<std::reference_wrapper<const user>>;
	[[nodiscard]] auto upsert_user(dpp::snowflake id, std::string username, double point) -> std::expected<std::monostate, type::error>;
	[[nodiscard]] auto remove_user(dpp::snowflake id) -> std::expected<std::monostate, type::error>;
	[[nodiscard]] auto list_users(bool sort_by_point = true) const -> std::vector<user>;

	// Match management
	[[nodiscard]] auto add_match(std::vector<team> teams,
															 type::timestamp when = std::chrono::time_point_cast<type::timestamp::duration>(std::chrono::system_clock::now()))
			-> std::expected<std::size_t, type::error>;
	[[nodiscard]] auto set_match_winner(std::size_t index, std::vector<int> winning_teams) -> std::expected<std::monostate, type::error>;
	[[nodiscard]] auto delete_match(std::size_t index) -> std::expected<std::monostate, type::error>;
	[[nodiscard]] auto recent_matches(int count) const -> std::vector<match_record>;
	[[nodiscard]] auto match_by_index(std::size_t index) const -> std::optional<match_record>;

	// Persistence
	[[nodiscard]] auto load() -> std::expected<std::monostate, type::error>;
	[[nodiscard]] auto save() const -> std::expected<std::monostate, type::error>;

	// Rating calculation
	[[nodiscard]] auto recompute_ratings() -> std::expected<std::monostate, type::error>;

private:
	std::shared_ptr<persistence_service> persistence_;
	std::unordered_map<std::uint64_t, user> users_;
	std::vector<match_record> history_;

	[[nodiscard]] auto hydrate_match(const match_record &mr) const -> match_record;

	// ELO calculation helpers
	[[nodiscard]] auto apply_match_effect(std::span<const team> teams, std::span<const int> winners) -> std::expected<std::monostate, type::error>;
};

} // namespace terry
