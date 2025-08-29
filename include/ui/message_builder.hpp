#pragma once

#include "core/constants.hpp"
#include <dpp/dpp.h>

#include <format>
#include <string_view>

namespace terry::ui {

// for type safety
template <typename T>
concept Replyable = requires(T t, dpp::message m) { t.reply(m); };

class message_builder {
public:
	[[nodiscard]] static auto error(std::string_view msg) -> dpp::message
	{
		return dpp::message{std::format("{}{}", constants::text::err_prefix, msg)}.set_flags(dpp::m_ephemeral);
	}

	[[nodiscard]] static auto success(std::string_view msg) -> dpp::message { return dpp::message{std::format("{}{}", constants::text::ok_prefix, msg)}; }

	static auto reply_error(Replyable auto &event, std::string_view msg) -> void
	{
		if constexpr (requires { event.reply(dpp::ir_channel_message_with_source, type::error(msg)); }) {
			event.reply(dpp::ir_channel_message_with_source, type::error(msg));
		}
		else {
			event.reply(error(msg));
		}
	}
};

} // namespace terry::ui
