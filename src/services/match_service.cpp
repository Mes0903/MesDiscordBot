#include "core/constants.hpp"
#include "services/match_service.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace terry {

match_service::match_service(std::shared_ptr<persistence_service> persistence) : persistence_(std::move(persistence)) {}

auto match_service::load() -> std::expected<std::monostate, type::error>
{
	// Load users
	// The gcc has false positvies issue here, for more detail, see https://gcc.gnu.org/bugzilla/show_bug.cgi?id=107532
	if (auto users_res = persistence_->load_users()) {
		users_ = std::move(*users_res);
	}
	else {
		return std::unexpected(users_res.error());
	}

	// Load matches
	if (auto matches_res = persistence_->load_matches()) {
		history_ = std::move(*matches_res);
	}
	else {
		return std::unexpected(matches_res.error());
	}

	return std::monostate{};
}

auto match_service::save() const -> std::expected<std::monostate, type::error>
{
	// Save users
	if (auto res = persistence_->save_users(users_); !res) {
		return res;
	}

	// Save matches
	if (auto res = persistence_->save_matches(history_); !res) {
		return res;
	}

	return std::monostate{};
}

auto match_service::find_user(dpp::snowflake id) const -> std::optional<std::reference_wrapper<const user>>
{
	auto it = users_.find(util::id_to_u64(id));
	return it == users_.end() ? std::nullopt : std::optional{std::ref(it->second)};
}

auto match_service::upsert_user(dpp::snowflake id, std::string username, double point) -> std::expected<std::monostate, type::error>
{
	if (point < 0) {
		return std::unexpected(type::error{std::string(constants::text::point_must_positive)});
	}

	auto &u = users_[util::id_to_u64(id)];
	u.id = id;
	u.username = std::move(username);
	u.point = point;
	u.base_point = point;

	return std::monostate{};
}

auto match_service::remove_user(dpp::snowflake id) -> std::expected<std::monostate, type::error>
{
	if (users_.erase(util::id_to_u64(id)) == 0) {
		return std::unexpected(type::error{std::string(constants::text::users_not_found)});
	}
	return std::monostate{};
}

auto match_service::list_users(bool sort_by_point) const -> std::vector<user>
{
	// grab just the values (users) from the map, and materialize into a vector<user>
	auto vals = users_ | std::views::values;
	auto out = std::ranges::to<std::vector<user>>(vals);

	// sort by projection
	if (sort_by_point) {
		std::ranges::sort(out, std::greater<>{}, &user::point); // desc by point
	}
	else {
		std::ranges::sort(out, std::less<>{}, &user::username); // asc by name
	}

	return out;
}

auto match_service::add_match(std::vector<team> teams, type::timestamp when) -> std::expected<std::size_t, type::error>
{
	match_record mr{.when = when, .teams = std::move(teams), .winning_teams = {}};

	history_.push_back(std::move(mr));
	return history_.size() - 1;
}

auto match_service::set_match_winner(std::size_t index, std::vector<int> winning_teams) -> std::expected<std::monostate, type::error>
{
	if (index >= history_.size()) {
		return std::unexpected(type::error{"Match index out of range"});
	}

	const auto &teams = history_[index].teams;
	for (int w : winning_teams) {
		if (w < 0 || w >= static_cast<int>(teams.size())) {
			return std::unexpected(type::error{"Invalid winning team index"});
		}
	}

	history_[index].winning_teams = std::move(winning_teams);
	return std::monostate{};
}

auto match_service::recent_matches(int count) const -> std::vector<match_record>
{
	if (count <= 0 || history_.empty())
		return {};

	const auto take_count = std::min<std::size_t>(static_cast<std::size_t>(count), history_.size());
	auto view = history_ | std::views::reverse | std::views::take(take_count) | std::views::transform(std::bind_front(&match_service::hydrate_match, this));

	return std::ranges::to<std::vector<match_record>>(view);
}

auto match_service::match_by_index(std::size_t index) const -> std::optional<match_record>
{
	if (index >= history_.size()) {
		return std::nullopt;
	}
	return hydrate_match(history_[index]);
}

auto match_service::recompute_ratings() -> std::expected<std::monostate, type::error>
{
	// Reset all users to base ratings
	for (auto &[id, u] : users_) {
		u.point = u.base_point;
		u.wins = 0;
		u.games = 0;
	}

	// Chronological order
	std::vector<std::size_t> order(history_.size());
	std::iota(order.begin(), order.end(), 0);
	std::stable_sort(order.begin(), order.end(), [this](std::size_t a, std::size_t b) { return history_[a].when < history_[b].when; });

	// Apply each match
	for (std::size_t idx : order) {
		const auto &mr = history_[idx];
		if (auto res = apply_match_effect(mr.teams, mr.winning_teams); !res) {
			return res;
		}
	}

	return std::monostate{};
}

auto match_service::hydrate_match(const match_record &mr) const -> match_record
{
	match_record out = mr;
	for (auto &t : out.teams) {
		for (auto &m : t.members) {
			if (auto it = users_.find(util::id_to_u64(m.id)); it != users_.end()) {
				m = it->second; // copy full user (point, username, etc.)
			}
		}
	}
	return out;
}

auto match_service::apply_match_effect(std::span<const team> teams, std::span<const int> winners) -> std::expected<std::monostate, type::error>
{
	if (teams.empty()) {
		return std::unexpected(type::error{std::string(constants::text::teams_must_positive)});
	}

	// Validate winner indices
	for (int w : winners) {
		if (w < 0 || w >= static_cast<int>(teams.size())) {
			return std::unexpected(type::error{"Invalid winner index"});
		}
	}

	// ELO calculation parameters
	constexpr double K_FACTOR = 4.0;
	constexpr double ELO_SCALE = 400.0;

	// Calculate team ratings
	std::vector<double> team_ratings(teams.size());
	for (std::size_t i = 0; i < teams.size(); ++i) {
		double sum = 0.0;
		for (const auto &member : teams[i].members) {
			if (auto u = find_user(member.id)) {
				sum += u->get().point;
			}
		}
		team_ratings[i] = sum;
	}

	// Calculate deltas for each team
	std::vector<double> team_deltas(teams.size(), 0.0);
	std::unordered_set<int> winner_set(winners.begin(), winners.end());

	for (std::size_t i = 0; i < teams.size(); ++i) {
		for (std::size_t j = i + 1; j < teams.size(); ++j) {
			double expected_i = 1.0 / (1.0 + std::pow(10.0, (team_ratings[j] - team_ratings[i]) / ELO_SCALE));
			double expected_j = 1.0 - expected_i;

			double actual_i = 0.5, actual_j = 0.5;
			if (winner_set.contains(static_cast<int>(i)) && !winner_set.contains(static_cast<int>(j))) {
				actual_i = 1.0;
				actual_j = 0.0;
			}
			else if (!winner_set.contains(static_cast<int>(i)) && winner_set.contains(static_cast<int>(j))) {
				actual_i = 0.0;
				actual_j = 1.0;
			}

			team_deltas[i] += K_FACTOR * (actual_i - expected_i);
			team_deltas[j] += K_FACTOR * (actual_j - expected_j);
		}
	}

	// Apply deltas to individual players
	for (std::size_t team_idx = 0; team_idx < teams.size(); ++team_idx) {
		const auto &team = teams[team_idx];
		if (team.empty())
			continue;

		double delta_per_member = team_deltas[team_idx] / static_cast<double>(team.size());
		bool is_winner = winner_set.contains(static_cast<int>(team_idx));

		for (const auto &member : team.members) {
			auto it = users_.find(util::id_to_u64(member.id));
			if (it == users_.end())
				continue;

			auto &u = it->second;
			u.point = std::max(0.0, u.point + delta_per_member);
			u.games++;
			if (is_winner) {
				u.wins++;
			}
		}
	}

	return std::monostate{};
}
} // namespace terry
