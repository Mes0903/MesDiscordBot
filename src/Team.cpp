#include "Team.hpp"

void Team::add_member(const User &user)
{
	members.push_back(user);
	total_power += user.combat_power;
}

void Team::recalculate_total_power()
{
	total_power = 0;
	for (const auto &member : members) {
		total_power += member.combat_power;
	}
}
