/**
 * @brief
 *  Responsibilities:
 *    - Persist/load users and matches from JSON files
 *    - Manage user registry (upsert, remove, list, find)
 *    - Form balanced teams using a greedy pass + light randomization swaps
 *    - Record matches, update per-user W/L stats, and provide recent history
 *  Notes:
 *    - All public functions return std::expected for explicit error handling
 *    - `form_teams` currently balances by total power; you can enforce equal
 *      team sizes by comparing (size, power) when choosing the next team.
 */

#pragma once

#include "models.hpp"

#include <expected>
#include <optional>
#include <random>
#include <unordered_map>
#include <vector>

namespace terry::bot {

enum class user_sort { by_power_desc, by_power_asc, by_name_asc };

/**
 * @class team_manager
 * @brief Core state/persistence layer for users and match history.
 */
class team_manager {
public:
	team_manager() = default;

	/**
	 * @brief Load users and matches from disk.
	 * @return ok_t on success; error with message if the JSON file is missing or invalid.
	 */
	[[nodiscard]] std::expected<ok_t, error> load();

	/**
	 * @brief Save users and matches to disk.
	 * @return ok_t on success; error if writing fails.
	 */
	[[nodiscard]] std::expected<ok_t, error> save() const;

	/**
	 * @brief Check whether a user exists.
	 * @param id Discord snowflake ID.
	 * @return true if present; false otherwise.
	 */
	[[nodiscard]] bool has_user(user_id id) const noexcept;
	[[nodiscard]] const user *find_user(user_id id) const noexcept;
	[[nodiscard]] user *find_user(user_id id) noexcept;

	[[nodiscard]] std::expected<ok_t, error> upsert_user(user_id id, std::string username, int combat_power);
	/**
	 * @brief Remove a user from the registry.
	 * @param id Discord snowflake ID.
	 * @return ok_t on success; error if the user does not exist.
	 */
	[[nodiscard]] std::expected<ok_t, error> remove_user(user_id id);
	[[nodiscard]] std::vector<user> list_users(user_sort sort = user_sort::by_power_desc) const;

	/**
	 * @brief Form balanced teams from participants.
	 * @param participants Input player set.
	 * @param team_count Number of teams (>= 2).
	 * @param rng Random engine for controlled randomization.
	 * @return Teams with roughly balanced total power; size may be uneven unless caller enforces it.
	 */
	[[nodiscard]] std::vector<team> form_teams(std::span<const user_id> participant_ids, int num_teams, std::optional<uint64_t> seed = std::nullopt) const;

	/**
	 * @brief Record a finished match and update per-user W/L statistics.
	 * @param teams Teams that participated.
	 * @param winners Optional indices of winning teams (when known).
	 * @return ok_t on success; error on validation failure.
	 */
	[[nodiscard]] std::expected<ok_t, error> record_match(std::vector<team> teams, std::vector<int> winning_teams,
																												timestamp when = std::chrono::time_point_cast<timestamp::duration>(std::chrono::system_clock::now()));

	/**
	 * @brief Retrieve the most recent matches (newest first).
	 * @param count Maximum number of entries to return.
	 * @return A vector of match records in reverse chronological order.
	 */
	[[nodiscard]] std::vector<match_record> recent_matches(int count) const;

private:
	std::unordered_map<uint64_t, user> users_;
	std::vector<match_record> history_;

	static constexpr const char *USERS_FILE = "users.json";
	static constexpr const char *MATCHES_FILE = "matches.json";

	/**
	 * @brief Map a list of user IDs to known users.
	 * @param ids IDs to look up.
	 * @return Vector of users found (unknown IDs are silently skipped).
	 */
	[[nodiscard]] std::vector<user> participants_from_ids(std::span<const user_id> ids) const;
};

} // namespace terry::bot
