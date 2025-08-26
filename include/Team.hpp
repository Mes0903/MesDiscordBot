#pragma once

#include "User.hpp"

#include <vector>

struct Team {
	std::vector<User> members;
	int total_power = 0;

	Team() = default;

	void add_member(const User &user);
	void recalculate_total_power();
};
