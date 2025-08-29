#pragma once

#include "core/utils.hpp"
#include "models/match.hpp"
#include "models/user.hpp"

#include <filesystem>
#include <unordered_map>
#include <vector>

namespace terry {

class persistence_service {
public:
	explicit persistence_service(std::filesystem::path data_dir = ".") : data_dir_{std::move(data_dir)} {}

	// User operations
	[[nodiscard]] auto load_users() -> std::expected<std::unordered_map<std::uint64_t, user>, type::error>;
	[[nodiscard]] auto save_users(const std::unordered_map<std::uint64_t, user> &users) -> std::expected<std::monostate, type::error>;

	// Match operations
	[[nodiscard]] auto load_matches() -> std::expected<std::vector<match_record>, type::error>;
	[[nodiscard]] auto save_matches(const std::vector<match_record> &matches) -> std::expected<std::monostate, type::error>;

private:
	std::filesystem::path data_dir_;

	[[nodiscard]] auto users_path() const -> std::filesystem::path;
	[[nodiscard]] auto matches_path() const -> std::filesystem::path;
};

} // namespace terry
