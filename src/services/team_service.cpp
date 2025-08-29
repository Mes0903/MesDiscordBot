#include "core/constants.hpp"
#include "services/team_service.hpp"

#include <algorithm>
#include <functional>
#include <limits>
#include <numeric>

namespace terry {

auto team_service::form_teams(std::span<const user> participants, formation_config config) -> std::expected<std::vector<team>, type::error>
{
	// ---- Sanity checks ------------------------------------------------------
	if (config.num_teams < 1) {
		return std::unexpected(type::error{constants::text::teams_must_positive});
	}

	if (participants.size() < static_cast<std::size_t>(config.num_teams)) {
		return std::unexpected(type::error{constants::text::users_not_enough});
	}

	const int P = static_cast<int>(participants.size());
	const int T = config.num_teams;

	// Copy participants to work with
	std::vector<user> players(participants.begin(), participants.end());

	// Build a local RNG with deterministic seeding based on participant IDs
	auto make_seed = [&]() -> std::uint64_t {
		// Hash participant IDs (sorted) for determinism
		std::uint64_t h = 1469598103934665603ull; // FNV offset
		std::vector<std::uint64_t> ids;
		ids.reserve(players.size());
		for (const auto &u : players) {
			ids.push_back(util::id_to_u64(u.id));
		}
		std::ranges::sort(ids);
		for (auto x : ids) {
			h ^= x;
			h *= 1099511628211ull; // FNV prime
		}
		// Mix with wall-clock for run-to-run variation
		std::uint64_t t = util::id_to_u64(std::chrono::system_clock::now().time_since_epoch().count());
		// xorshift mix
		t ^= t >> 33;
		t *= 0xff51afd7ed558ccdULL;
		t ^= t >> 33;
		t *= 0xc4ceb9fe1a85ec53ULL;
		t ^= t >> 33;
		return h ^ t;
	};

	std::mt19937_64 rng(config.seed ? config.seed : make_seed());

	// ---- Preprocess ---------------------------------------------------------
	// Shuffle first, then stable_sort by rating so ties become randomized
	std::ranges::shuffle(players, rng);
	std::ranges::stable_sort(players, std::greater{}, &user::point);

	// Precompute suffix sums of remaining points for quick bounds
	std::vector<double> suf(P + 1, 0.0);
	for (int i = P - 1; i >= 0; --i) {
		suf[i] = suf[i + 1] + players[i].point;
	}
	const double TOTAL = suf[0];
	const double TARGET_MEAN = TOTAL / static_cast<double>(T);

	auto spread_of = [](const std::vector<double> &v) {
		auto [mn, mx] = std::ranges::minmax_element(v);
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
					// Random tie-break among equal-cost teams
					if (std::uniform_int_distribution<int>(0, 1)(rng)) {
						best_t = t;
					}
				}
			}
			totals[best_t] += players[k].point;
			best_asg[k] = best_t;
		}
		ub_tot = totals;
	}
	double best_spread = spread_of(ub_tot); // Current upper bound

	// ---- Branch & Bound core -------------------------------------------------
	std::vector<int> cur_asg(P, -1);
	std::vector<double> totals(T, 0.0);
	std::vector<int> counts(T, 0);

	auto team_order = [&](std::vector<int> &out) {
		// Deterministic order; randomness only on end-state replacement
		out.resize(T);
		std::iota(out.begin(), out.end(), 0);
		std::ranges::stable_sort(out, [&](int a, int b) {
			if (totals[a] != totals[b]) {
				return totals[a] < totals[b];
			}
			return counts[a] < counts[b];
		});
	};

	auto lower_bound = [&](int k) -> double {
		const auto [mn_it, mx_it] = std::ranges::minmax_element(totals);
		const double cur_min = *mn_it, cur_max = *mx_it;
		const double lb_mean = std::max(cur_max - TARGET_MEAN, TARGET_MEAN - cur_min);
		const double lb_rem = std::max(0.0, cur_max - (cur_min + suf[k]));
		return std::max(lb_mean, lb_rem);
	};

	auto must_fill_empty = [&](int k) -> bool {
		int empty = 0;
		for (int t = 0; t < T; ++t) {
			if (counts[t] == 0) {
				++empty;
			}
		}
		const int left = P - k;
		return left == empty && empty > 0;
	};

	std::function<void(int)> dfs = [&](int k) {
		if (k == P) {
			// All assigned & feasible
			for (int t = 0; t < T; ++t) {
				if (counts[t] == 0) {
					return;
				}
			}
			const double sp = spread_of(totals);

			// Accept strictly-better; if equal-best, replace with 50% to diversify
			if (sp < best_spread - 1e-12) {
				best_spread = sp;
				best_asg = cur_asg;
			}
			else if (std::abs(sp - best_spread) <= 1e-12) {
				if (std::uniform_int_distribution<int>(0, 1)(rng)) {
					best_asg = cur_asg; // Swap to an alternative optimal solution
				}
			}
			return;
		}

		if (lower_bound(k) >= best_spread - 1e-12) {
			return;
		}

		std::vector<int> order;
		if (!must_fill_empty(k)) {
			team_order(order);
		}
		else {
			order.clear();
			for (int t = 0; t < T; ++t) {
				if (counts[t] == 0) {
					order.push_back(t);
				}
			}
			// Optional: randomize the order among empty teams
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

	// ---- Build result from best_asg ------------------------------------------
	std::vector<team> result(T);
	for (int k = 0; k < P; ++k) {
		int t = best_asg[k];
		if (t < 0 || t >= T) {
			// Fallback: assign to team with lowest total
			int best_t = 0;
			double best_tot = std::numeric_limits<double>::infinity();
			for (int j = 0; j < T; ++j) {
				double s = result[j].total_point();
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

} // namespace terry
