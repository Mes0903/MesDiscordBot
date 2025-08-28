/**
 * @brief
 * Responsibilities:
 * 	 - Persist/load users and matches from JSON files
 * 	 - Manage user registry (upsert, remove, list, find)
 * 	 - Form balanced teams using a greedy pass + light randomization swaps
 * 	 - Record matches, update per-user W/L stats, and provide recent history
 * Notes:
 *   - All public functions return std::expected for explicit error handling
 *   - `form_teams` currently balances by total point; you can enforce equal
 *     team sizes by comparing (size, point) when choosing the next team.
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
		try { // the try block is for nlohmann::json
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
		try { // the try block is for nlohmann::json
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
	try { // the try block is for nlohmann::json
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
		return std::unexpected(error{std::string{"json 存檔失敗，請截圖回報給開發者："} + e.what()});
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
 * @brief Insert or update a user (requires point >= 0).
 */
std::expected<ok_t, error> team_manager::upsert_user(user_id id, std::string username, double point)
{
	if (point < 0) [[unlikely]]
		return std::unexpected(error{"分數必須 >= 0"});
	auto &u = users_[static_cast<uint64_t>(id)];
	u.id = id;
	u.username = std::move(username);
	u.point = point;
	return ok_t{};
}

/**
 * @brief Remove a user by ID; error if not found.
 */
std::expected<ok_t, error> team_manager::remove_user(user_id id)
{
	auto it = users_.find(static_cast<uint64_t>(id));
	if (it == users_.end()) [[unlikely]]
		return std::unexpected(error{"該使用者不存在"});
	users_.erase(it);
	return ok_t{};
}

/**
 * @brief Return a copy of users sorted by point/name.
 */
std::vector<user> team_manager::list_users(user_sort sort) const
{
	auto v = users_ | std::views::values | std::ranges::to<std::vector>();
	switch (sort) {
	case user_sort::by_point_desc:
		std::ranges::sort(v, std::greater{}, &user::point);
		break;
	case user_sort::by_point_asc:
		std::ranges::sort(v, std::less{}, &user::point);
		break;
	case user_sort::by_name_asc:
		std::ranges::sort(v, {}, &user::username);
		break;
	}
	return v;
}

/**
 * @brief Partition selected participants into the specified number of teams
 *        by solving a balanced team assignment problem. Uses an exact
 *        branch-and-bound search (ILP-style) to minimize the spread between
 *        the strongest and weakest team (max(total) - min(total)). Ensures
 *        every team has at least one member. Returns the optimal team split
 *        or an error if infeasible.
 */
std::expected<std::vector<team>, error> team_manager::form_teams(std::span<const user_id> participant_ids, int num_teams) const
{
	// ---- Sanity checks ------------------------------------------------------
	if (num_teams < 1) [[unlikely]]
		return std::unexpected(error{"隊伍數須為正整數"});

	// Gather current user snapshots in the same way as before.
	std::vector<user> players;
	players.reserve(participant_ids.size());
	for (auto id : participant_ids) {
		if (auto it = users_.find(static_cast<uint64_t>(id)); it != users_.end())
			players.push_back(it->second);
	}

	const int P = static_cast<int>(players.size());
	const int T = num_teams;

	if (P == 0) [[unlikely]]
		return std::unexpected(error{"沒有參與者"});
	if (T > P) [[unlikely]]
		return std::unexpected(error{"隊伍數大於參與者數"});

	// build a local RNG
	auto make_seed = [&]() -> uint64_t {
		// hash participants
		uint64_t h = 1469598103934665603ull; // FNV offset
		std::vector<uint64_t> participant_ids;
		participant_ids.reserve(participant_ids.size());
		for (auto id : participant_ids)
			participant_ids.push_back(static_cast<uint64_t>(id));
		std::ranges::sort(participant_ids);
		for (uint64_t x : participant_ids) {
			h ^= x;
			h *= 1099511628211ull; // FNV prime
		}
		// mix with wall-clock for run-to-run variation
		uint64_t t = static_cast<uint64_t>(std::chrono::system_clock::now().time_since_epoch().count());
		// xorshift mix
		t ^= t >> 33;
		t *= 0xff51afd7ed558ccdULL;
		t ^= t >> 33;
		t *= 0xc4ceb9fe1a85ec53ULL;
		t ^= t >> 33;
		return h ^ t;
	};

	// ---- Preprocess ---------------------------------------------------------
	// Shuffle first, then stable_sort by rating so ties become randomized (seeded).
	std::mt19937_64 rng(make_seed());
	std::ranges::shuffle(players, rng);
	std::ranges::stable_sort(players, std::greater<>{}, &user::point);

	// Precompute suffix sums of remaining points for quick bounds.
	std::vector<double> suf(P + 1, 0.0);
	for (int i = P - 1; i >= 0; --i)
		suf[i] = suf[i + 1] + players[i].point;
	const double TOTAL = suf[0];
	const double TARGET_MEAN = TOTAL / static_cast<double>(T);

	auto spread_of = [](const std::vector<double> &v) {
		auto [mn, mx] = std::minmax_element(v.begin(), v.end());
		return (mx == v.end() || mn == v.end()) ? 0.0 : (*mx - *mn);
	};

	// ---- Upper bound via a quick greedy (random tie-break) ------------------
	std::vector<int> best_asg(P, -1);
	std::vector<double> ub_tot(T, 0.0);
	{
		std::vector<double> totals(T, 0.0);
		for (int k = 0; k < P; ++k) {
			int best_t = 0;
			double best_cost = std::numeric_limits<double>::infinity();
			for (int t = 0; t < T; ++t) {
				double mx = -std::numeric_limits<double>::infinity();
				double mn = std::numeric_limits<double>::infinity();
				for (int j = 0; j < T; ++j) {
					const double tj = (j == t) ? (totals[j] + players[k].point) : totals[j];
					mx = std::max(mx, tj);
					mn = std::min(mn, tj);
				}
				const double cost = mx - mn;
				if (cost < best_cost) {
					best_cost = cost;
					best_t = t;
				}
				else if (cost == best_cost) {
					// random tie-break among equal-cost teams
					if (std::uniform_int_distribution<int>(0, 1)(rng))
						best_t = t;
				}
			}
			totals[best_t] += players[k].point;
			best_asg[k] = best_t;
		}
		ub_tot = totals;
	}
	double best_spread = spread_of(ub_tot); // current upper bound

	// ---- Branch & Bound core (unchanged bounds) -----------------------------
	std::vector<int> cur_asg(P, -1);
	std::vector<double> totals(T, 0.0);
	std::vector<int> counts(T, 0);

	auto team_order = [&](std::vector<int> &out) {
		// deterministic order; we will inject randomness only on end-state replacement
		out.resize(T);
		std::iota(out.begin(), out.end(), 0);
		std::ranges::stable_sort(out, [&](int a, int b) {
			if (totals[a] != totals[b])
				return totals[a] < totals[b];
			return counts[a] < counts[b];
		});
	};

	auto lower_bound = [&](int k) -> double {
		const auto [mn_it, mx_it] = std::minmax_element(totals.begin(), totals.end());
		const double cur_min = *mn_it, cur_max = *mx_it;
		const double lb_mean = std::max(cur_max - TARGET_MEAN, TARGET_MEAN - cur_min);
		const double lb_rem = std::max(0.0, cur_max - (cur_min + suf[k]));
		return std::max(lb_mean, lb_rem);
	};

	auto must_fill_empty = [&](int k) -> bool {
		int empty = 0;
		for (int t = 0; t < T; ++t)
			if (counts[t] == 0)
				++empty;
		const int left = P - k;
		return left == empty && empty > 0;
	};

	std::function<void(int)> dfs = [&](int k) {
		if (k == P) {
			// all assigned & feasible
			for (int t = 0; t < T; ++t)
				if (counts[t] == 0)
					return;
			const double sp = spread_of(totals);

			// accept strictly-better; if equal-best, replace with 50% to diversify
			if (sp < best_spread - 1e-12) {
				best_spread = sp;
				best_asg = cur_asg;
			}
			else if (std::abs(sp - best_spread) <= 1e-12) {
				if (std::uniform_int_distribution<int>(0, 1)(rng)) {
					best_asg = cur_asg; // swap to an alternative optimal solution
				}
			}
			return;
		}

		if (lower_bound(k) >= best_spread - 1e-12)
			return;

		std::vector<int> order;
		if (!must_fill_empty(k)) {
			team_order(order);
		}
		else {
			order.clear();
			for (int t = 0; t < T; ++t)
				if (counts[t] == 0)
					order.push_back(t);
			// optional: randomize the order among empty teams
			std::ranges::shuffle(order, rng);
		}

		for (int t : order) {
			totals[t] += players[k].point;
			counts[t] += 1;
			cur_asg[k] = t;

			dfs(k + 1);

			cur_asg[k] = -1;
			counts[t] -= 1;
			totals[t] -= players[k].point;
		}
	};

	dfs(0);

	// ---- Build result from best_asg (same as before) ------------------------
	std::vector<team> result(T);
	for (int k = 0; k < P; ++k) {
		int t = best_asg[k];
		if (t < 0 || t >= T) {
			int best_t = 0;
			double best_tot = std::numeric_limits<double>::infinity();
			for (int j = 0; j < T; ++j) {
				double s = 0.0;
				for (auto &m : result[j].members)
					s += m.point;
				if (s < best_tot) {
					best_tot = s;
					best_t = j;
				}
			}
			t = best_t;
		}
		result[t].add_member(players[k]);
	}

	return result;
}

/**
 * @brief Record a match, update per-user W/L stats, and append to history.
 */
std::expected<ok_t, error> team_manager::record_match(std::vector<team> teams, std::vector<int> winning_teams, timestamp when)
{
	// validate winners
	for (int w : winning_teams) {
		if (w < 0 || w >= static_cast<int>(teams.size())) [[unlikely]] {
			return std::unexpected(error{"無效的勝方隊伍索引"});
		}
	}

	// update per-user stats using orthodox Elo (base-10 logistic, scale = 400)
	if (!teams.empty()) [[likely]] {
		using std::accumulate;
		using std::pow;

		constexpr double ELO_SCALE = 400.0; // classic Elo scale
		constexpr double kMinPower = 0.0;
		constexpr double k_factor = 4.0;

		const std::size_t N = teams.size();
		std::vector<double> team_sum(N); // sum of member ratings per team
		// std::vector<std::size_t> team_cnt(N);		// team sizes (for AVG usage)
		std::vector<double> team_rating(N);			// team rating used by Elo (here we use SUM)
		std::vector<double> team_delta(N, 0.0); // accumulated team deltas from pairwise matches

		// 1) Aggregate team ratings (choose SUM or AVG; we use SUM here)
		for (std::size_t i = 0; i < N; ++i) {
			const auto &members = teams[i].members;
			// team_cnt[i] = members.size(); // (for AVG usage)
			team_sum[i] = accumulate(members.begin(), members.end(), 0.0, [](double acc, const user &u) { return acc + u.point; });
			// SUM: team strength = sum of member ratings (popular team-Elo variant)
			// Also can apply AVG here: team_rating[i] = (team_cnt[i] ? team_sum[i] / team_cnt[i] : 0.0);
			team_rating[i] = team_sum[i];
		}

		// 2) Actual results S_ij via pairwise decomposition
		//    - win vs non-winner => 1 / 0
		//    - winner vs winner  => 0.5 / 0.5   (tie among winners)
		//    - non-winner vs non-winner => 0.5 / 0.5   (tie among losers)
		std::unordered_set<int> winset(winning_teams.begin(), winning_teams.end());

		auto expected_vs = [&](double Ra, double Rb) -> double {
			// E[a beats b] = 1 / (1 + 10^((Rb - Ra)/400))
			const double exp_term = pow(10.0, (Rb - Ra) / ELO_SCALE);
			return 1.0 / (1.0 + exp_term);
		};

		// 3) Accumulate team deltas by pairwise Elo updates
		//    For each pair (i, j):
		//      di = K * (S_i - E_i), dj = -di  → zero-sum at pair level
		for (std::size_t i = 0; i < N; ++i) {
			for (std::size_t j = i + 1; j < N; ++j) {
				const double Ei = expected_vs(team_rating[i], team_rating[j]);
				const double Ej = 1.0 - Ei;

				// Actual scores for this pair
				// (i_win && j_win) => tie among winners (0.5/0.5)
				// (!i_win && !j_win) => tie among losers (0.5/0.5)
				const bool i_win = winset.contains(static_cast<int>(i));
				const bool j_win = winset.contains(static_cast<int>(j));
				double Si = 0.5, Sj = 0.5; // default tie
				if (i_win && !j_win) {
					Si = 1.0;
					Sj = 0.0;
				}
				else if (!i_win && j_win) {
					Si = 0.0;
					Sj = 1.0;
				}

				const double di = k_factor * (Si - Ei);
				const double dj = k_factor * (Sj - Ej); // = -di

				team_delta[i] += di;
				team_delta[j] += dj;
			}
		}

		// 4) Distribute each team's delta to its members
		//    Winners: inverse weighting → higher-rated gain less, lower-rated gain more.
		//    Losers : direct  weighting → higher-rated lose more, lower-rated lose less.
		//    We normalize weights so that sum of member deltas equals team_delta[ti].
		for (std::size_t ti = 0; ti < N; ++ti) {
			const double Ts = team_sum[ti];
			const auto &members = teams[ti].members;

			if (members.empty())
				continue;

			// If the team has zero total rating, fall back to equal split.
			const bool zero_team = (Ts <= 0.0);
			const double even_share = 1.0 / static_cast<double>(members.size());

			// Positive team_delta => winners; negative => losers.
			const double td = team_delta[ti];
			if (std::abs(td) == 0.0) // nothing to distribute
				continue;

			// Tunables for intra-team weighting.
			// - kWeightFloor avoids division blow-ups when rating ≈ 0.
			// - alpha controls how strong the inverse/direct effect is (0.6 = linear).
			constexpr double kWeightFloor = 1e-6;
			constexpr double alpha = 0.6;

			// Build weights and normalize.
			std::vector<double> weights;
			weights.reserve(members.size());

			if (zero_team) {
				// Equal split when team total is zero.
				for (const auto &m : members) {
					if (auto *u = find_user(m.id)) {
						double np = u->point + td * even_share;
						if (!std::isfinite(np))
							np = u->point;
						u->point = std::max(kMinPower, np);
					}
				}
				continue;
			}

			// Winners: w_i = 1 / (rating^alpha)  → higher rating → smaller weight
			// Losers : w_i = (rating^alpha)      → higher rating → larger  weight
			const bool winners = (td > 0.0);
			double W = 0.0;
			for (const auto &m : members) {
				double r = kWeightFloor;
				if (const auto *u = find_user(m.id))
					r = std::max(u->point, kWeightFloor);
				double wi = winners ? std::pow(r, -alpha) : std::pow(r, alpha);
				weights.push_back(wi);
				W += wi;
			}
			if (W <= 0.0)
				W = static_cast<double>(members.size()); // safety

			// Apply normalized shares.
			for (std::size_t k = 0; k < members.size(); ++k) {
				const auto &m = members[k];
				if (auto *u = find_user(m.id)) {
					const double share = weights[k] / W;
					double np = u->point + td * share;
					if (!std::isfinite(np))
						np = u->point;										// safety
					u->point = std::max(kMinPower, np); // non-negativity
				}
			}
		}

		// Win/Loss counters
		std::unordered_set<uint64_t> winner_ids;
		for (int wi : winning_teams)
			for (const auto &m : teams[wi].members)
				winner_ids.insert(static_cast<uint64_t>(m.id));

		for (const auto &t : teams) {
			for (const auto &m : t.members) {
				if (auto *u = find_user(m.id)) {
					u->games++;
					if (winner_ids.contains(static_cast<uint64_t>(m.id)))
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
	if (count <= 0) [[unlikely]]
		return {};
	return history_ | std::views::reverse | std::views::take(static_cast<size_t>(count)) | std::ranges::to<std::vector>();
}

} // namespace terry::bot
