#include "core/constants.hpp"
#include "handlers/session_manager.hpp"

#include <algorithm>
#include <format>
#include <random>

namespace terry {

auto session_manager::generate_token() -> std::string
{
	static std::mt19937_64 rng{std::random_device{}()};
	std::uniform_int_distribution<uint64_t> dist;
	return std::format("{:016x}", dist(rng));
}

auto session_manager::create_session(panel_session session) -> std::string
{
	session.panel_id = generate_token();
	auto id = session.panel_id;
	sessions_.emplace(id, std::move(session));
	cleanup_old_sessions();
	return id;
}

auto session_manager::get_session(std::string_view id) -> std::optional<std::reference_wrapper<panel_session>>
{
	auto it = sessions_.find(std::string{id});
	if (it == sessions_.end() || !it->second.active) {
		return std::nullopt;
	}

	it->second.last_accessed_at = std::chrono::steady_clock::now();
	return std::ref(it->second);
}

auto session_manager::validate_owner(std::string_view id, dpp::snowflake owner) -> std::expected<std::monostate, type::error>
{
	auto sess = get_session(id);
	if (!sess) {
		return std::unexpected(type::error{std::string(constants::text::panel_expired}));
	}

	if (sess->get().owner_id != owner) {
		return std::unexpected(type::error{std::string(constants::text::panel_owner_only}));
	}

	return std::monostate{};
}

auto session_manager::remove_session(std::string_view id) -> void { sessions_.erase(std::string{id}); }

auto session_manager::cleanup_old_sessions(std::size_t max_sessions) -> void
{
	if (sessions_.size() <= max_sessions) {
		return;
	}

	// Sort by last access time
	std::vector<std::pair<std::string, std::chrono::steady_clock::time_point>> sorted;
	sorted.reserve(sessions_.size());

	for (const auto &[id, sess] : sessions_) {
		sorted.emplace_back(id, sess.last_accessed_at);
	}

	std::ranges::sort(sorted, {}, &decltype(sorted)::value_type::second);

	// Remove oldest sessions
	std::size_t to_remove = sessions_.size() - max_sessions;
	for (std::size_t i = 0; i < to_remove; ++i) {
		sessions_.erase(sorted[i].first);
	}
}

} // namespace terry
