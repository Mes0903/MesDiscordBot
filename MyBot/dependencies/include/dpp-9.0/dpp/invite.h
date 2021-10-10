/************************************************************************************
 *
 * D++, A Lightweight C++ library for Discord
 *
 * Copyright 2021 Craig Edwards and D++ contributors 
 * (https://github.com/brainboxdotcc/DPP/graphs/contributors)
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 ************************************************************************************/
#pragma once
#include <dpp/export.h>
#include <dpp/discord.h>
#include <dpp/json_fwd.hpp>

namespace dpp {

/**
 * @brief Represents an invite to a discord guild or channel
 */
class CoreExport invite {
public:
	/** Invite code
	 */
	std::string code;
	/** Guild for the invite
	 */
	snowflake guild_id;
	/** Channel id for invite 
	 */
	snowflake channel_id;
	/** User ID of invite creator
	 */
	snowflake inviter_id;
	/** Target user ID of invite, for invites sent via DM
	 */
	snowflake target_user_id;
	/** Target user type (generally this is always 1, "stream")
	 */
	uint8_t target_user_type;
	/** Approximate number of online users
	 */
	uint32_t approximate_presence_count;
	/** Approximate total users online and offline
	 */
	uint32_t approximate_member_count;
	/** Maximum age of invite
	 */
	uint32_t max_age;
	/** Maximum number of uses
	 */
	uint32_t max_uses;
	/** True if a temporary invite which grants access for a limited time
	 */
	bool temporary;
	/** True if this invite should not replace or "attach to" similar invites
	 */
	bool unique;

	/** Constructor
	 */
	invite();

	/** Destructor
	 */
	~invite();

	/** Read class values from json object
	 * @param j A json object to read from
	 * @return A reference to self
	 */
	invite& fill_from_json(nlohmann::json* j);

	/** Build JSON from this object.
	 * @return The JSON text of the invite
	 */
	std::string build_json() const;

};

/** A container of invites */
typedef std::unordered_map<std::string, invite> invite_map;

};
