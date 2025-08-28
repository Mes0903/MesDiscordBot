/**
 * @brief
 * Responsibilities:
 * 	 - Persist/load users and matches from JSON files
 * 	 - Manage user registry (upsert, remove, list, find)
 * 	 - Form balanced teams using a greedy pass + light randomization swaps
 * 	 - Record matches, update per-user W/L stats, and provide recent history
 * Notes:
 *   - All public functions return std::expected for explicit error handling
 *   - `form_teams` currently balances by total power; you can enforce equal
 *     team sizes by comparing (size, power) when choosing the next team.
 */

#include "team_manager.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <numeric>
#include <unordered_set>

namespace terry::bot {

using json = nlohmann::json;

/**
 * @brief Load users and matches from disk; returns error if JSON is invalid.
 */
std::expected<ok_t, error> team_manager::load()
{
	// users
	if (std::ifstream uf{USERS_FILE}) {
		try {
			json arr;
			uf >> arr;
			for (const auto &uj : arr) {
				auto u = user::from_json(uj);
				users_.emplace(static_cast<uint64_t>(u.id), std::move(u));
			}
		} catch (const std::exception &e) {
			return std::unexpected(error{std::string{"載入使用者失敗："} + e.what()});
		}
	}

	// matches
	if (std::ifstream mf{MATCHES_FILE}) {
		try {
			json arr;
			mf >> arr;
			for (const auto &mj : arr)
				history_.push_back(match_record::from_json(mj));
		} catch (const std::exception &e) {
			return std::unexpected(error{std::string{"載入對戰紀錄失敗："} + e.what()});
		}
	}
	return ok_t{};
}

/**
 * @brief Save users and matches to disk.
 */
std::expected<ok_t, error> team_manager::save() const
{
	try {
		// users
		if (std::ofstream uf{USERS_FILE}) {
			json arr = json::array();
			for (const auto &[id, u] : users_)
				arr.push_back(u.to_json());
			uf << arr.dump(2);
		}

		// matches
		if (std::ofstream mf{MATCHES_FILE}) {
			json arr = json::array();
			for (const auto &m : history_)
				arr.push_back(m.to_json());
			mf << arr.dump(2);
		}
	} catch (const std::exception &e) {
		return std::unexpected(error{std::string{"儲存失敗："} + e.what()});
	}
	return ok_t{};
}

const user *team_manager::find_user(user_id id) const noexcept
{
	auto it = users_.find(static_cast<uint64_t>(id));
	return it == users_.end() ? nullptr : &it->second;
}

user *team_manager::find_user(user_id id) noexcept
{
	auto it = users_.find(static_cast<uint64_t>(id));
	return it == users_.end() ? nullptr : &it->second;
}

/**
 * @brief Insert or update a user (requires combat_power >= 0).
 */
std::expected<ok_t, error> team_manager::upsert_user(user_id id, std::string username, double combat_power)
{
	if (combat_power < 0)
		return std::unexpected(error{"戰力必須 >= 0"});
	auto &u = users_[static_cast<uint64_t>(id)];
	u.id = id;
	u.username = std::move(username);
	u.combat_power = combat_power;
	return ok_t{};
}

/**
 * @brief Remove a user by ID; error if not found.
 */
std::expected<ok_t, error> team_manager::remove_user(user_id id)
{
	auto it = users_.find(static_cast<uint64_t>(id));
	if (it == users_.end())
		return std::unexpected(error{"該使用者不存在"});
	users_.erase(it);
	return ok_t{};
}

/**
 * @brief Return a copy of users sorted by power/name.
 */
std::vector<user> team_manager::list_users(user_sort sort) const
{
	auto v = users_ | std::views::values | std::ranges::to<std::vector>();
	switch (sort) {
	case user_sort::by_power_desc:
		std::ranges::sort(v, std::greater{}, &user::combat_power);
		break;
	case user_sort::by_power_asc:
		std::ranges::sort(v, std::less{}, &user::combat_power);
		break;
	case user_sort::by_name_asc:
		std::ranges::sort(v, {}, &user::username);
		break;
	}
	return v;
}

/**
 * @brief Form teams: greedy by lowest total power, then random swaps with tolerance.
 */
std::expected<std::vector<team>, error> team_manager::form_teams(std::span<const user_id> participant_ids, int num_teams, std::optional<uint64_t> seed) const
{
	// Allow uneven team sizes; only require at least one member per team.
	if (num_teams <= 0)
		return std::unexpected(error{"隊伍數須為正整數"});

	// Gather current user snapshots.
	std::vector<user> players;
	players.reserve(participant_ids.size());
	for (auto id : participant_ids) {
		if (auto it = users_.find(static_cast<uint64_t>(id)); it != users_.end())
			players.push_back(it->second);
	}

	const int P = static_cast<int>(players.size());
	const int T = num_teams;

	// Infeasible if fewer participants than teams.
	if (P == 0)
		return std::unexpected(error{"沒有參與者"});
	if (T > P)
		return std::unexpected(error{"隊伍數大於參與者數"});

	// Randomize input order to keep results varied run-to-run.
	std::mt19937_64 rng(seed.value_or(std::random_device{}()));
	std::ranges::shuffle(players, rng);

	// Prepare T empty teams; no capacity limit (uneven allowed).
	std::vector<team> teams(T);
	std::vector<double> totals(T, 0.0); // keep running total power per team

	auto projected_spread = [&](size_t place_idx, double add_power) {
		// Compute max(total) - min(total) if we assign 'add_power' to team 'place_idx'.
		double maxv = -std::numeric_limits<double>::infinity();
		double minv = std::numeric_limits<double>::infinity();
		for (size_t j = 0; j < totals.size(); ++j) {
			const double tj = (j == place_idx) ? (totals[j] + add_power) : totals[j];
			if (tj > maxv)
				maxv = tj;
			if (tj < minv)
				minv = tj;
		}
		return maxv - minv;
	};

	// Greedy assignment: for each player, put them where the total-power spread is minimized.
	for (const auto &u : players) {
		size_t best_i = 0;
		double best_cost = std::numeric_limits<double>::infinity();

		for (size_t i = 0; i < teams.size(); ++i) {
			const double cost = projected_spread(i, u.combat_power);
			const bool better = (cost < best_cost) || (cost == best_cost && std::uniform_int_distribution<int>(0, 1)(rng));
			if (better) {
				best_cost = cost;
				best_i = i;
			}
		}
		teams[best_i].add_member(u);
		totals[best_i] += u.combat_power;
	}

	return teams;
}

/**
 * @brief Record a match, update per-user W/L stats, and append to history.
 */
std::expected<ok_t, error> team_manager::record_match(std::vector<team> teams, std::vector<int> winning_teams, timestamp when)
{
	// validate winners
	for (int w : winning_teams) {
		if (w < 0 || w >= static_cast<int>(teams.size())) {
			return std::unexpected(error{"無效的勝方隊伍索引"});
		}
	}

	// update per-user stats and hidden rating if teams are provided
	if (!teams.empty()) {
		// Hidden rating adjustment
		// Denominator floor to avoid INF and huge swings when power is ~0
		constexpr double kDenomFloor = 1.0;
		constexpr double kMinPower = 0.0;

		std::vector<double> team_sum(teams.size()), team_cnt(teams.size());
		for (size_t i = 0; i < teams.size(); ++i) {
			double s = 0.0;
			for (const auto &m : teams[i].members)
				s += m.combat_power;
			team_sum[i] = s;
			team_cnt[i] = static_cast<double>(teams[i].members.size());
		}

		// Opponents' size-weighted average for each team
		std::vector<double> opp_avg(teams.size());
		const double total_sum = std::accumulate(team_sum.begin(), team_sum.end(), 0.0);
		const double total_cnt = std::accumulate(team_cnt.begin(), team_cnt.end(), 0.0);
		for (size_t i = 0; i < teams.size(); ++i) {
			const double sum_excl = total_sum - team_sum[i];
			const double cnt_excl = std::max(total_cnt - team_cnt[i], 1.0);
			opp_avg[i] = sum_excl / cnt_excl;
		}

		std::unordered_set<int> winset(winning_teams.begin(), winning_teams.end());

		for (size_t ti = 0; ti < teams.size(); ++ti) {
			const bool winner = winset.contains(static_cast<int>(ti));
			for (const auto &m : teams[ti].members) {
				if (auto *u = find_user(m.id)) {
					const double p_raw = u->combat_power;
					const double oa = opp_avg[ti];

					// Stabilized denominators
					const double p_den = std::max(p_raw, kDenomFloor);
					const double oa_den = std::max(oa, kDenomFloor);

					// Raw delta from your original formula
					double delta = winner ? (k_factor_ * (oa_den / p_den)) : (-k_factor_ * (p_den / oa_den));

					// Hard-cap the absolute change based on opponent strength
					// cap grows sublinearly with opponent strength (sqrt), scaled by k and a config knob.
					const double cap = std::max(0.0, delta_cap_scale_ * k_factor_ * std::sqrt(oa_den));
					if (cap > 0.0) {
						if (delta > cap)
							delta = cap;
						if (delta < -cap)
							delta = -cap;
					}

					// Exponential smoothing: only apply a fraction alpha of the delta
					const double alpha = rating_alpha_; // in [0,1]
					double np = p_raw + alpha * delta;

					// Safety: avoid NaN/INF and enforce lower bound
					if (!std::isfinite(np))
						np = p_raw;
					u->combat_power = std::max(kMinPower, np);
				}
			}
		}

		// W/L stats
		std::unordered_set<uint64_t> winner_ids;
		for (int wi : winning_teams) {
			for (const auto &m : teams[wi].members) {
				winner_ids.insert(static_cast<uint64_t>(m.id));
			}
		}
		for (const auto &t : teams) {
			for (const auto &m : t.members) {
				auto *u = find_user(m.id);
				if (!u)
					continue;
				u->games++;
				if (winner_ids.contains(static_cast<uint64_t>(m.id))) {
					u->wins++;
				}
			}
		}
	}

	match_record mr;
	mr.when = when;
	mr.teams = std::move(teams); // persisted as id-only (models.cpp)
	mr.winning_teams = std::move(winning_teams);
	history_.push_back(std::move(mr));
	return ok_t{};
}

/**
 * @brief Return the last `count` matches in reverse chronological order.
 */
std::vector<match_record> team_manager::recent_matches(int count) const
{
	if (count <= 0)
		return {};
	return history_ | std::views::reverse | std::views::take(static_cast<size_t>(count)) | std::ranges::to<std::vector>();
}

} // namespace terry::bot
