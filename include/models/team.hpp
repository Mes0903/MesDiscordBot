#pragma once

#include "models/user.hpp"

#include <numeric>
#include <ranges>
#include <vector>

namespace terry {

class team {
public:
	std::vector<user> members;

	// computed property using explicit object parameter
	[[nodiscard]] auto total_point(this const auto &self) -> double
	{
		return std::ranges::fold_left(self.members | std::views::transform(&user::point), 0.0, std::plus{});
	}

	auto add_member(this auto &self, user u) -> void { self.members.push_back(std::move(u)); }

	[[nodiscard]] auto size(this const auto &self) -> std::size_t { return self.members.size(); }

	[[nodiscard]] auto empty(this const auto &self) -> bool { return self.members.empty(); }
};

} // namespace terry
