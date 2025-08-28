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
#include <utility>

namespace {
/**
 * @brief canonical signature: multiset of team-membership sets (user_id sorted)
 */
auto canon(std::span<const terry::bot::team> ts)
{
	std::vector<std::vector<uint64_t>> S;
	S.reserve(ts.size());
	for (const auto &t : ts) {
		std::vector<uint64_t> ids;
		ids.reserve(t.members.size());
		for (const auto &u : t.members)
			ids.push_back(static_cast<uint64_t>(u.id));
		std::ranges::sort(ids);
		S.push_back(std::move(ids));
	}
	std::ranges::sort(S);
	return S;
}
} // namespace

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
				users_.emplace(static_cast<uint64_t>(std::as_const(u.id)), std::move(u));
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
	auto it = users_.find(static_cast<std::uint64_t>(std::as_const(id)));
	return it == users_.end() ? nullptr : &it->second;
}

user *team_manager::find_user(user_id id) noexcept
{
	auto it = users_.find(static_cast<std::uint64_t>(std::as_const(id)));
	return it == users_.end() ? nullptr : &it->second;
}

/**
 * @brief Insert or update a user (requires point >= 0).
 */
std::expected<ok_t, error> team_manager::upsert_user(user_id id, std::string username, double point)
{
	if (point < 0) [[unlikely]]
		return std::unexpected(error{"分數必須 >= 0"});

	auto &u = users_[static_cast<std::uint64_t>(std::as_const(id))];
	u.id = id;
	u.username = std::move(username);
	u.point = point;
	u.base_point = point; // keep baseline in sync on manual set
	return ok_t{};
}

/**
 * @brief Remove a user by ID; error if not found.
 */
std::expected<ok_t, error> team_manager::remove_user(user_id id)
{
	if (users_.erase(static_cast<std::uint64_t>(std::as_const(id))) == 0) [[unlikely]]
		return std::unexpected(error{"該使用者不存在"});

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
		if (auto it = users_.find(static_cast<std::uint64_t>(std::as_const(id))); it != users_.end())
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
		// hash participants (use the *outer* participant_ids)
		uint64_t h = 1469598103934665603ull; // FNV offset
		std::vector<uint64_t> ids;
		ids.reserve(participant_ids.size());
		for (auto id : participant_ids)
			ids.push_back(static_cast<std::uint64_t>(std::as_const(id)));
		std::ranges::sort(ids);
		for (uint64_t x : ids) {
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
				double s = result[j].total_point;
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

std::expected<ok_t, error> team_manager::set_match_winner_by_teams(std::span<const team> teams, std::vector<int> winning_teams)
{
	// validate indices
	for (int w : winning_teams) {
		if (w < 0 || w >= static_cast<int>(teams.size()))
			return std::unexpected(error{"無效的勝方隊伍索引"});
	}

	// find from the back (most recent first)
	for (auto it = history_.rbegin(); it != history_.rend(); ++it) {
		if (same_composition_(teams, it->teams)) {
			it->winning_teams = winning_teams;
			// DO NOT adjust users_ ratings/W-L here (see header note).
			return ok_t{};
		}
	}

	// Not found → append as a new record with current timestamp
	match_record mr;
	mr.when = std::chrono::time_point_cast<timestamp::duration>(std::chrono::system_clock::now());
	mr.teams = std::vector<team>(teams.begin(), teams.end());
	mr.winning_teams = std::move(winning_teams);
	history_.push_back(std::move(mr));
	return ok_t{};
}

std::expected<std::size_t, error> team_manager::add_match(std::vector<team> teams, timestamp when)
{
	match_record mr;
	mr.when = when;
	mr.teams = std::move(teams); // persisted as id-only (see models.cpp)
	mr.winning_teams.clear();		 // no winners yet
	history_.push_back(std::move(mr));
	return history_.size() - 1;
}

std::expected<ok_t, error> team_manager::set_match_winner_by_index(std::size_t index, std::vector<int> winning_teams)
{
	if (index >= history_.size())
		return std::unexpected(error{"比賽索引超出範圍"});

	const auto &teams = history_[index].teams;
	for (int w : winning_teams) {
		if (w < 0 || w >= static_cast<int>(teams.size()))
			return std::unexpected(error{"無效的勝方隊伍索引"});
	}

	history_[index].winning_teams = std::move(winning_teams);
	// NOTE: ratings/W-L are NOT recomputed retroactively here.
	return ok_t{};
}

std::vector<std::pair<std::size_t, match_record>> team_manager::recent_matches_with_index(int count) const
{
	std::vector<std::pair<std::size_t, match_record>> out;
	if (count <= 0 || history_.empty())
		return out;

	const std::size_t take = std::min<std::size_t>(static_cast<std::size_t>(count), history_.size());
	const std::size_t start = history_.size() - take; // oldest index in the slice

	out.reserve(take);
	for (std::size_t i = start; i < history_.size(); ++i)
		out.emplace_back(i, history_[i]); // oldest → newest order

	return out;
}

std::optional<match_record> team_manager::match_by_index(std::size_t index) const
{
	if (index >= history_.size())
		return std::nullopt;
	return history_[index];
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

std::expected<ok_t, error> team_manager::recompute_all_from_history()
{
	// 1) reset all users to their baseline & clear W/L
	for (auto &[id, u] : users_) {
		u.point = u.base_point; // baseline persisted in users.json
		u.wins = 0;
		u.games = 0;
	}

	// 2) get chronological order (by timestamp, tie-break by index)
	std::vector<std::size_t> order(history_.size());
	std::iota(order.begin(), order.end(), 0);
	std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) { return history_[a].when < history_[b].when; });

	// 3) replay all matches
	for (std::size_t idx : order) {
		const auto &mr = history_[idx];
		if (auto r = apply_match_effect_(mr.teams, mr.winning_teams); !r) [[unlikely]]
			return std::unexpected(r.error());
	}
	return ok_t{};
}

bool team_manager::same_composition_(std::span<const team> a, std::span<const team> b)
{
	if (a.size() != b.size())
		return false;
	return canon(a) == canon(b);
}

std::expected<ok_t, error> team_manager::apply_match_effect_(std::span<const team> teams, std::span<const int> winning_teams)
{
	// --- Validate inputs first ---
	if (teams.empty())
		return std::unexpected(error{"沒有隊伍可套用"});
	for (int w : winning_teams) {
		if (w < 0 || w >= static_cast<int>(teams.size()))
			return std::unexpected(error{"無效的勝方隊伍索引"});
	}

	// Prepare team sums/ratings
	constexpr double ELO_SCALE = 400.0;
	constexpr double kMinPower = 0.0;
	constexpr double k_factor = 4.0;

	const std::size_t N = teams.size();
	std::vector<double> team_sum(N, 0.0);
	std::vector<double> team_rating(N, 0.0);
	std::vector<double> team_delta(N, 0.0);

	for (std::size_t i = 0; i < N; ++i) {
		double sum = 0.0;
		for (const auto &m : teams[i].members) {
			if (const auto *u = find_user(m.id))
				sum += u->point;
		}
		team_sum[i] = sum;
		team_rating[i] = sum; // SUM model
	}

	std::unordered_set<int> winset(winning_teams.begin(), winning_teams.end());
	auto expected_vs = [&](double Ra, double Rb) -> double {
		const double exp_term = std::pow(10.0, (Rb - Ra) / ELO_SCALE);
		return 1.0 / (1.0 + exp_term);
	};

	// Pairwise deltas
	for (std::size_t i = 0; i < N; ++i) {
		for (std::size_t j = i + 1; j < N; ++j) {
			const double Ei = expected_vs(team_rating[i], team_rating[j]);
			const double Ej = 1.0 - Ei;
			const bool i_win = winset.contains(static_cast<int>(i));
			const bool j_win = winset.contains(static_cast<int>(j));
			double Si = 0.5, Sj = 0.5;
			if (i_win && !j_win) {
				Si = 1.0;
				Sj = 0.0;
			}
			else if (!i_win && j_win) {
				Si = 0.0;
				Sj = 1.0;
			}
			const double di = k_factor * (Si - Ei);
			const double dj = k_factor * (Sj - Ej);
			team_delta[i] += di;
			team_delta[j] += dj;
		}
	}

	// Distribute team deltas to members
	constexpr double kWeightFloor = 1e-6;
	constexpr double alpha = 0.6;

	for (std::size_t ti = 0; ti < N; ++ti) {
		const double Ts = team_sum[ti];
		const auto &members = teams[ti].members;
		if (members.empty())
			continue;

		const bool winners = (team_delta[ti] > 0.0);
		const double td = team_delta[ti];
		if (std::abs(td) == 0.0)
			continue;

		if (Ts <= 0.0) {
			const double even = 1.0 / static_cast<double>(members.size());
			for (const auto &m : members) {
				if (auto *u = find_user(m.id)) {
					double np = u->point + td * even;
					if (!std::isfinite(np))
						return std::unexpected(error{"數值不穩定（NaN/Inf）"});
					u->point = std::max(kMinPower, np);
				}
			}
		}
		else {
			std::vector<double> weights;
			weights.reserve(members.size());
			double W = 0.0;
			for (const auto &m : members) {
				double r = kWeightFloor;
				if (const auto *u = find_user(m.id))
					r = std::max(u->point, kWeightFloor);
				double wi = winners ? std::pow(r, -alpha) : std::pow(r, alpha);
				if (!std::isfinite(wi))
					return std::unexpected(error{"數值不穩定（NaN/Inf）"});
				weights.push_back(wi);
				W += wi;
			}
			if (!(W > 0.0))
				return std::unexpected(error{"權重總和異常（<=0）"});

			for (std::size_t k = 0; k < members.size(); ++k) {
				const auto &m = members[k];
				if (auto *u = find_user(m.id)) {
					const double share = weights[k] / W;
					double np = u->point + td * share;
					if (!std::isfinite(np))
						return std::unexpected(error{"數值不穩定（NaN/Inf）"});
					u->point = std::max(kMinPower, np);
				}
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

	return ok_t{};
}

} // namespace terry::bot
