#include "core/constants.hpp"
#include "services/persistence_service.hpp"
#include <nlohmann/json.hpp>

#include <fstream>

namespace terry {

auto persistence_service::users_path() const -> std::filesystem::path { return data_dir_ / constants::files::users_file; }

auto persistence_service::matches_path() const -> std::filesystem::path { return data_dir_ / constants::files::matches_file; }

auto persistence_service::load_users() -> std::expected<std::unordered_map<std::uint64_t, user>, type::error>
{
	std::unordered_map<std::uint64_t, user> users;

	if (!std::filesystem::exists(users_path())) {
		return users; // Return empty map if file doesn't exist
	}

	try { // The try block is for nlohmann::json
		std::ifstream file(users_path());
		nlohmann::json j;
		file >> j;

		for (const auto &item : j) {
			auto u = user::from_json(item);
			users.emplace(util::id_to_u64(u.id), std::move(u));
		}

		return users;
	} catch (const std::exception &e) {
		return std::unexpected(type::error{std::format("無法載入使用者：{}", e.what())});
	}
}

auto persistence_service::save_users(const std::unordered_map<std::uint64_t, user> &users) -> std::expected<std::monostate, type::error>
{
	try { // The try block is for nlohmann::json
		nlohmann::json j = nlohmann::json::array();
		for (const auto &[id, u] : users) {
			j.push_back(u.to_json());
		}

		std::ofstream file(users_path());
		file << j.dump(2);

		return std::monostate{};
	} catch (const std::exception &e) {
		return std::unexpected(type::error{std::format("無法載入使用者：{}", e.what())});
	}
}

auto persistence_service::load_matches() -> std::expected<std::vector<match_record>, type::error>
{
	std::vector<match_record> matches;

	if (!std::filesystem::exists(matches_path())) {
		return matches; // Return empty vector if file doesn't exist
	}

	try { // The try block is for nlohmann::json
		std::ifstream file(matches_path());
		nlohmann::json j;
		file >> j;

		for (const auto &item : j) {
			matches.push_back(match_record::from_json(item));
		}

		return matches;
	} catch (const std::exception &e) {
		return std::unexpected(type::error{std::format("無法載入配對紀錄：{}", e.what())});
	}
}

auto persistence_service::save_matches(const std::vector<match_record> &matches) -> std::expected<std::monostate, type::error>
{
	try { // The try block is for nlohmann::json
		nlohmann::json j = nlohmann::json::array();
		for (const auto &m : matches) {
			j.push_back(m.to_json());
		}

		std::ofstream file(matches_path());
		file << j.dump(2);

		return std::monostate{};
	} catch (const std::exception &e) {
		return std::unexpected(type::error{std::format("無法載入配對紀錄：{}", e.what())});
	}
}

} // namespace terry
