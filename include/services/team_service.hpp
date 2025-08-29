#pragma once

#include "core/utils.hpp"
#include "models/team.hpp"

#include <random>
#include <span>
#include <vector>

namespace terry {

class team_service {
public:
	struct formation_config {
		int num_teams{2};
		bool balance_sizes{false}; // If true, try to keep team sizes equal
		std::uint64_t seed{0};		 // 0 = use random seed
	};

	// use std::span for input, result_of for output
	[[nodiscard]] static auto form_teams(std::span<const user> participants, formation_config config) -> std::expected<std::vector<team>, type::error>;

private:
	// Branch & bound implementation
	struct bnb_context {
		std::vector<user> players;
		int num_teams;
		std::mt19937_64 rng;
		std::vector<double> suffix_sums;
		double target_mean;

		// Current state
		std::vector<int> current_assignment;
		std::vector<double> current_totals;
		std::vector<int> current_counts;

		// Best solution
		std::vector<int> best_assignment;
		double best_spread;

		auto solve() -> void;

	private:
		auto dfs(int player_idx) -> void;
		[[nodiscard]] auto compute_lower_bound(int player_idx) const -> double;
		[[nodiscard]] auto must_fill_empty_teams(int player_idx) const -> bool;
		[[nodiscard]] auto compute_spread() const -> double;
	};
};

} // namespace terry
