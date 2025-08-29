#include "core/constants.hpp"
#include "services/team_service.hpp"

#include <algorithm>
#include <functional>
#include <limits>
#include <numeric>

namespace terry {

auto team_service::form_teams(std::span<const user> participants, formation_config config) -> std::expected<std::vector<team>, type::error>
{

	if (config.num_teams < 1) {
		return std::unexpected(type::error{std::string(constants::text::teams_must_positive)});
	}

	if (participants.size() < static_cast<std::size_t>(config.num_teams)) {
		return std::unexpected(type::error{std::string(constants::text::users_not_enough)});
	}

	bnb_context ctx{}; // zero-init everything, then fill explicitly
	ctx.players.assign(participants.begin(), participants.end());
	ctx.num_teams = config.num_teams;

	// safe defaults (even if you'll recompute later)
	ctx.suffix_sums.clear();
	ctx.target_mean = 0.0;

	// allocate working buffers
	ctx.current_assignment.assign(participants.size(), -1);
	ctx.current_totals.assign(config.num_teams, 0.0);
	ctx.current_counts.assign(config.num_teams, 0);

	// best-so-far
	ctx.best_assignment.assign(participants.size(), -1);
	ctx.best_spread = std::numeric_limits<double>::infinity();

	// rng: if no seed provided, hash participant IDs (sorted) ^ mixed wall-clock (old impl behavior)
	auto make_seed = [&]() -> std::uint64_t {
		std::uint64_t h = 1469598103934665603ull; // FNV offset
		std::vector<std::uint64_t> ids;
		ids.reserve(ctx.players.size());
		for (const auto &u : ctx.players)
			ids.push_back(util::id_to_u64(u.id));
		std::ranges::sort(ids);
		for (auto x : ids) {
			h ^= x;
			h *= 1099511628211ull;
		}
		std::uint64_t t = util::id_to_u64(std::chrono::system_clock::now().time_since_epoch().count());
		// xorshift/murmur-ish mix
		t ^= t >> 33;
		t *= 0xff51afd7ed558ccdULL;
		t ^= t >> 33;
		t *= 0xc4ceb9fe1a85ec53ULL;
		t ^= t >> 33;
		return h ^ t;
	};
	ctx.rng = std::mt19937_64{config.seed ? config.seed : make_seed()};

	// Prepare players
	std::ranges::shuffle(ctx.players, ctx.rng);
	std::ranges::stable_sort(ctx.players, std::greater{}, &user::point);

	// Compute suffix sums
	ctx.suffix_sums.resize(ctx.players.size() + 1, 0.0);
	for (std::size_t i = ctx.players.size(); i > 0; --i) {
		ctx.suffix_sums[i - 1] = ctx.suffix_sums[i] + ctx.players[i - 1].point;
	}

	ctx.target_mean = ctx.suffix_sums[0] / static_cast<double>(config.num_teams);

	// Initialize state
	ctx.current_assignment.resize(ctx.players.size(), -1);
	ctx.current_totals.resize(config.num_teams, 0.0);
	ctx.current_counts.resize(config.num_teams, 0);
	ctx.best_assignment = ctx.current_assignment;
	ctx.best_spread = std::numeric_limits<double>::infinity();

	// Solve
	ctx.solve();

	// Build result
	std::vector<team> result(config.num_teams);
	for (std::size_t i = 0; i < ctx.players.size(); ++i) {
		int team_idx = ctx.best_assignment[i];
		if (team_idx >= 0 && team_idx < config.num_teams) {
			result[team_idx].add_member(ctx.players[i]);
		}
	}

	return result;
}

auto team_service::bnb_context::solve() -> void
{
	// Get initial greedy solution
	for (std::size_t i = 0; i < players.size(); ++i) {
		auto min_it = std::ranges::min_element(current_totals);
		int team_idx = util::narrow<int>(std::distance(current_totals.begin(), min_it));

		current_totals[team_idx] += players[i].point;
		current_counts[team_idx]++;
		best_assignment[i] = team_idx;
	}

	best_spread = compute_spread();

	// Reset for DFS
	std::ranges::fill(current_totals, 0.0);
	std::ranges::fill(current_counts, 0);
	std::ranges::fill(current_assignment, -1);

	// Start branch & bound
	dfs(0);
}

auto team_service::bnb_context::dfs(int player_idx) -> void
{
	if (player_idx == static_cast<int>(players.size())) {
		// Check if all teams have at least one member
		if (std::ranges::any_of(current_counts, [](int c) { return c == 0; })) {
			return;
		}

		double spread = compute_spread();
		if (spread < best_spread - 1e-9) {
			best_spread = spread;
			best_assignment = current_assignment;
		}
		else if (std::abs(spread - best_spread) < 1e-9) {
			// Random tie-breaking
			if (std::uniform_int_distribution<>(0, 1)(rng)) {
				best_assignment = current_assignment;
			}
		}
		return;
	}

	// Pruning
	if (compute_lower_bound(player_idx) >= best_spread - 1e-9) {
		return;
	}

	// Try assigning to each team
	std::vector<int> team_order(num_teams);
	std::iota(team_order.begin(), team_order.end(), 0);

	if (!must_fill_empty_teams(player_idx)) {
		// Sort by current total (ascending)
		std::ranges::sort(team_order, {}, [this](int t) { return current_totals[t]; });
	}
	else {
		// Must fill empty teams first
		auto part = std::ranges::partition(team_order, [this](int t) { return current_counts[t] == 0; });
		// Shuffle only the “true” partition (teams that are currently empty)
		std::ranges::shuffle(std::ranges::subrange(team_order.begin(), part.begin()), rng);
	}

	for (int team : team_order) {
		current_assignment[player_idx] = team;
		current_totals[team] += players[player_idx].point;
		current_counts[team]++;

		dfs(player_idx + 1);

		current_counts[team]--;
		current_totals[team] -= players[player_idx].point;
		current_assignment[player_idx] = -1;
	}
}

auto team_service::bnb_context::compute_lower_bound(int player_idx) const -> double
{
	auto [min_it, max_it] = std::ranges::minmax_element(current_totals);
	double cur_min = *min_it;
	double cur_max = *max_it;

	double remaining = suffix_sums[player_idx];

	// Best case: all remaining points go to min team
	double best_case_spread = std::max(0.0, cur_max - (cur_min + remaining));

	// Consider target mean constraint
	double mean_constraint = std::max(cur_max - target_mean, target_mean - cur_min);

	return std::max(best_case_spread, mean_constraint);
}

auto team_service::bnb_context::must_fill_empty_teams(int player_idx) const -> bool
{
	const int empty_count = util::narrow<int>(std::ranges::count(current_counts, 0));
	const int remaining = static_cast<int>(players.size()) - player_idx;
	return remaining == empty_count && empty_count > 0;
}

auto team_service::bnb_context::compute_spread() const -> double
{
	auto [min_it, max_it] = std::ranges::minmax_element(current_totals);
	return *max_it - *min_it;
}

} // namespace terry
