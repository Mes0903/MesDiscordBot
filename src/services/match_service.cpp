#include "core/constants.hpp"
#include "services/match_service.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
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
		return std::unexpected(type::error{constants::text::point_must_positive});
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
		return std::unexpected(type::error{constants::text::users_not_found});
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

auto match_service::delete_match(std::size_t index) -> std::expected<std::monostate, type::error>
{
	if (index >= history_.size()) {
		return std::unexpected(type::error{"Match index out of range"});
	}

	history_.erase(history_.begin() + static_cast<long>(index));
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
	// --- Validate inputs first ---
	if (teams.empty()) {
		return std::unexpected(type::error{constants::text::teams_must_positive});
	}

	for (int w : winners) {
		if (w < 0 || w >= static_cast<int>(teams.size())) {
			return std::unexpected(type::error{"Invalid winner index"});
		}
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
			if (auto u = find_user(m.id)) {
				sum += u->get().point;
			}
		}
		team_sum[i] = sum;
		team_rating[i] = sum; // SUM model
	}

	std::unordered_set<int> winset(winners.begin(), winners.end());

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
		if (members.empty()) {
			continue;
		}

		const bool is_winning_team = (team_delta[ti] > 0.0);
		const double td = team_delta[ti];
		if (std::abs(td) == 0.0) {
			continue;
		}

		if (Ts <= 0.0) {
			// Even distribution when team sum is zero
			const double even = 1.0 / static_cast<double>(members.size());
			for (const auto &m : members) {
				auto it = users_.find(util::id_to_u64(m.id));
				if (it != users_.end()) {
					double np = it->second.point + td * even;
					if (!std::isfinite(np)) {
						return std::unexpected(type::error{"Numerical instability (NaN/Inf)"});
					}
					it->second.point = std::max(kMinPower, np);
				}
			}
		}
		else {
			// Weighted distribution
			std::vector<double> weights;
			weights.reserve(members.size());
			double W = 0.0;
			for (const auto &m : members) {
				double r = kWeightFloor;
				if (auto u = find_user(m.id)) {
					r = std::max(u->get().point, kWeightFloor);
				}
				double wi = is_winning_team ? std::pow(r, -alpha) : std::pow(r, alpha);
				if (!std::isfinite(wi)) {
					return std::unexpected(type::error{"Numerical instability (NaN/Inf)"});
				}
				weights.push_back(wi);
				W += wi;
			}
			if (!(W > 0.0)) {
				return std::unexpected(type::error{"Weight sum abnormal (<=0)"});
			}

			for (std::size_t k = 0; k < members.size(); ++k) {
				const auto &m = members[k];
				auto it = users_.find(util::id_to_u64(m.id));
				if (it != users_.end()) {
					const double share = weights[k] / W;
					double np = it->second.point + td * share;
					if (!std::isfinite(np)) {
						return std::unexpected(type::error{"Numerical instability (NaN/Inf)"});
					}
					it->second.point = std::max(kMinPower, np);
				}
			}
		}
	}

	// Win/Loss counters
	std::unordered_set<uint64_t> winner_ids;
	for (int wi : winners) {
		for (const auto &m : teams[wi].members) {
			winner_ids.insert(util::id_to_u64(m.id));
		}
	}

	for (const auto &t : teams) {
		for (const auto &m : t.members) {
			auto it = users_.find(util::id_to_u64(m.id));
			if (it != users_.end()) {
				it->second.games++;
				if (winner_ids.contains(util::id_to_u64(m.id))) {
					it->second.wins++;
				}
			}
		}
	}

	return std::monostate{};
}
} // namespace terry
