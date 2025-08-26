#pragma once

#include <dpp/dpp.h>
#include <nlohmann/json.hpp>

#include <string>

using json = nlohmann::json;

struct User {
	dpp::snowflake discord_id;
	std::string username;
	int combat_power;

	User() = default;
	User(dpp::snowflake id, const std::string &name, int power) : discord_id(id), username(name), combat_power(power) {}

	json to_json() const;
	static User from_json(const json &j);
};
