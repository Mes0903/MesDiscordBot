#pragma once

#include "core/utils.hpp"
#include <nlohmann/json.hpp>

#include <compare>

namespace terry {

class user {
public:
	dpp::snowflake id{};
	std::string username;
	double point{};
	double base_point{};
	int wins{};
	int games{};

	// defaulted three-way comparison
	[[nodiscard]] auto operator<=>(const user &) const = default;

	// explicit object parameter for const correctness
	[[nodiscard]] auto to_json(this const auto &self) -> nlohmann::json
	{
		return {{"discord_id", util::id_to_u64(self.id)}, {"username", self.username}, {"point", self.point},
						{"base_point", self.base_point},					{"wins", self.wins},				 {"games", self.games}};
	}

	[[nodiscard]] static auto from_json(const nlohmann::json &j) -> user
	{
		return {.id = dpp::snowflake{j.at("discord_id").get<std::uint64_t>()},
						.username = j.at("username").get<std::string>(),
						.point = j.at("point").get<double>(),
						.base_point = j.value("base_point", j.at("point").get<double>()),
						.wins = j.value("wins", 0),
						.games = j.value("games", 0)};
	}

	[[nodiscard]] auto win_rate(this const auto &self) -> double
	{
		return self.games > 0 ? static_cast<double>(self.wins) / static_cast<double>(self.games) : 0.0;
	}
};

} // namespace terry
