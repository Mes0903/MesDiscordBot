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
};
} // namespace terry
