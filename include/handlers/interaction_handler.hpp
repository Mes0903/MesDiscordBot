#pragma once

#include "core/utils.hpp"
#include "handlers/session_manager.hpp"
#include "services/match_service.hpp"
#include "services/team_service.hpp"
#include "ui/panel_builder.hpp"
#include <dpp/dpp.h>

#include <memory>

namespace terry {

class interaction_handler {
public:
	explicit interaction_handler(std::shared_ptr<team_service> team_svc, std::shared_ptr<match_service> match_svc, std::shared_ptr<session_manager> session_mgr,
															 std::shared_ptr<panel_builder> panel_bld);

	// Interaction handlers
	auto on_button(const dpp::button_click_t &ev) -> void;
	auto on_select(const dpp::select_click_t &ev) -> void;

private:
	std::shared_ptr<team_service> team_svc_;
	std::shared_ptr<match_service> match_svc_;
	std::shared_ptr<session_manager> session_mgr_;
	std::shared_ptr<panel_builder> panel_bld_;

	// Helper to parse custom_id format: "panel:<panel_id>:<action>[:arg]"
	struct parsed_custom_id {
		std::string panel_id;
		std::string action;
		std::optional<std::string> arg;
	};

	[[nodiscard]] auto parse_custom_id(std::string_view custom_id) const -> std::optional<parsed_custom_id>;

	// Button action handlers
	auto handle_assign(const dpp::button_click_t &ev, panel_session &sess) -> void;
	auto handle_newmatch(const dpp::button_click_t &ev, panel_session &sess) -> void;
	auto handle_win(const dpp::button_click_t &ev, panel_session &sess) -> void;
	auto handle_remove(const dpp::button_click_t &ev, panel_session &sess) -> void;
	auto handle_end(const dpp::button_click_t &ev, panel_session &sess) -> void;

	// Select action handlers
	auto handle_user_select(const dpp::select_click_t &ev, panel_session &sess) -> void;
	auto handle_match_choose(const dpp::select_click_t &ev, panel_session &sess) -> void;
};

} // namespace terry
