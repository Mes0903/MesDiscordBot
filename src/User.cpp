#include "User.hpp"

json User::to_json() const { return json{{"discord_id", static_cast<uint64_t>(discord_id)}, {"username", username}, {"combat_power", combat_power}}; }

User User::from_json(const json &j)
{
	return User{dpp::snowflake(j["discord_id"].get<uint64_t>()), j["username"].get<std::string>(), j["combat_power"].get<int>()};
}
