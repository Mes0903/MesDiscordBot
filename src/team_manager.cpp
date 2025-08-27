
#include "team_manager.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>

namespace terry::bot {

team_manager::team_manager() : rng_(std::random_device{}()) {}

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
			return std::unexpected(error{std::string{"Failed to load users: "} + e.what()});
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
			return std::unexpected(error{std::string{"Failed to load matches: "} + e.what()});
		}
	}
	return ok_t{};
}

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
		return std::unexpected(error{std::string{"Failed to save: "} + e.what()});
	}
	return ok_t{};
}

bool team_manager::has_user(user_id id) const noexcept { return users_.contains(static_cast<uint64_t>(id)); }
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

std::expected<ok_t, error> team_manager::upsert_user(user_id id, std::string username, int combat_power)
{
	if (combat_power < 0)
		return std::unexpected(error{"combat power must be >= 0"});
	auto &u = users_[static_cast<uint64_t>(id)];
	u.id = id;
	u.username = std::move(username);
	u.combat_power = combat_power;
	return ok_t{};
}

std::expected<ok_t, error> team_manager::remove_user(user_id id)
{
	auto it = users_.find(static_cast<uint64_t>(id));
	if (it == users_.end())
		return std::unexpected(error{"user not found"});
	users_.erase(it);
	return ok_t{};
}

std::vector<user> team_manager::list_users(user_sort sort) const
{
	std::vector<user> v;
	v.reserve(users_.size());
	for (const auto &[_, u] : users_)
		v.push_back(u);
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

std::vector<user> team_manager::participants_from_ids(std::span<const user_id> ids) const
{
	std::vector<user> res;
	for (auto id : ids) {
		if (auto it = users_.find(static_cast<uint64_t>(id)); it != users_.end())
			res.push_back(it->second);
	}
	return res;
}

std::vector<team> team_manager::form_teams(std::span<const user_id> participant_ids, int num_teams, std::optional<uint64_t> seed) const
{
	if (num_teams <= 0)
		return {};
	auto participants = participants_from_ids(participant_ids);
	if (participants.empty())
		return {};

	// Sort by power desc for greedy balancing
	std::ranges::sort(participants, std::greater{}, &user::combat_power);

	std::vector<team> teams(num_teams);

	// Greedy assign lowest total power team first
	for (const auto &p : participants) {
		auto it = std::ranges::min_element(teams, {}, &team::total_power);
		it->add_member(p);
	}

	// Optional randomization via swaps with slight tolerance
	std::mt19937 eng(seed ? *seed : std::random_device{}());
	int attempts = static_cast<int>(participants.size()) / 4;
	auto team_idx = std::uniform_int_distribution<size_t>(0, teams.size() - 1);
	for (int i = 0; i < attempts; ++i) {
		size_t a = team_idx(eng), b = team_idx(eng);
		if (a == b || teams[a].members.empty() || teams[b].members.empty())
			continue;
		auto ma = std::uniform_int_distribution<size_t>(0, teams[a].members.size() - 1)(eng);
		auto mb = std::uniform_int_distribution<size_t>(0, teams[b].members.size() - 1)(eng);

		int before = std::abs(teams[a].total_power - teams[b].total_power);
		int after = std::abs((teams[a].total_power - teams[a].members[ma].combat_power + teams[b].members[mb].combat_power) -
												 (teams[b].total_power - teams[b].members[mb].combat_power + teams[a].members[ma].combat_power));
		if (after <= before + 50) {
			std::swap(teams[a].members[ma], teams[b].members[mb]);
			teams[a].recalc_total_power();
			teams[b].recalc_total_power();
		}
	}
	return teams;
}

std::expected<ok_t, error> team_manager::record_match(std::vector<team> teams, std::vector<int> winning_teams, timestamp when)
{
	// validate winners
	for (int w : winning_teams) {
		if (w < 0 || w >= static_cast<int>(teams.size())) {
			return std::unexpected(error{"winning team index out of range"});
		}
	}
	match_record mr;
	mr.when = when;
	mr.teams = std::move(teams);
	mr.winning_teams = std::move(winning_teams);
	history_.push_back(std::move(mr));
	return ok_t{};
}

std::vector<match_record> team_manager::recent_matches(int count) const
{
	if (count <= 0)
		return {};
	std::vector<match_record> out;
	out.reserve(std::min<int>(count, history_.size()));
	for (int i = static_cast<int>(history_.size()) - 1; i >= 0 && static_cast<int>(out.size()) < count; --i)
		out.push_back(history_[i]);
	return out;
}

} // namespace terry::bot
