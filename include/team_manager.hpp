
#pragma once

#include "models.hpp"

#include <expected>
#include <optional>
#include <random>
#include <unordered_map>
#include <vector>

namespace terry::bot {

enum class user_sort { by_power_desc, by_power_asc, by_name_asc };

class team_manager {
public:
	team_manager();

	// persistence
	[[nodiscard]] std::expected<ok_t, error> load();
	[[nodiscard]] std::expected<ok_t, error> save() const;

	// users
	[[nodiscard]] bool has_user(user_id id) const noexcept;
	[[nodiscard]] const user *find_user(user_id id) const noexcept;
	[[nodiscard]] user *find_user(user_id id) noexcept;

	[[nodiscard]] std::expected<ok_t, error> upsert_user(user_id id, std::string username, int combat_power);
	[[nodiscard]] std::expected<ok_t, error> remove_user(user_id id);
	[[nodiscard]] std::vector<user> list_users(user_sort sort = user_sort::by_power_desc) const;

	// team formation
	[[nodiscard]] std::vector<team> form_teams(std::span<const user_id> participant_ids, int num_teams, std::optional<uint64_t> seed = std::nullopt) const;

	// matches
	[[nodiscard]] std::expected<ok_t, error> record_match(std::vector<team> teams, std::vector<int> winning_teams,
																												timestamp when = std::chrono::time_point_cast<timestamp::duration>(std::chrono::system_clock::now()));

	[[nodiscard]] std::vector<match_record> recent_matches(int count) const;

private:
	std::unordered_map<uint64_t, user> users_;
	std::vector<match_record> history_;
	mutable std::mt19937 rng_;

	static constexpr const char *USERS_FILE = "users.json";
	static constexpr const char *MATCHES_FILE = "matches.json";

	// helpers
	[[nodiscard]] std::vector<user> participants_from_ids(std::span<const user_id> ids) const;
};

} // namespace terry::bot
