#include "core/constants.hpp"
#include "models/match.hpp"
#include "ui/panel_builder.hpp"

#include <format>
#include <unordered_set>

namespace terry {

auto panel_builder::build_formteams_panel(const panel_session &sess, std::span<const user> all_users) const -> dpp::message
{

	dpp::message msg;
	dpp::embed e;
	e.set_title("分配隊伍面板");

	std::string body;
	body += std::format("隊伍數量： **{}**\n", sess.num_teams);

	bool can_assign = sess.selected_users.size() >= static_cast<std::size_t>(sess.num_teams);

	if (!sess.selected_users.empty()) {
		body += std::format("參與者 ({})： ", sess.selected_users.size());
		for (auto id : sess.selected_users) {
			body += util::mention(id);
		}
		body += "\n\n";

		if (!can_assign) {
			body += std::format("⚠️ 需至少選擇 {} 名玩家（每隊 1 人）才能分配。\n", sess.num_teams);
		}
	}
	else {
		body += "*於底下的清單中選取要參與隊伍分配的使用者*\n";
	}

	// Show formed teams if any
	if (!sess.formed_teams.empty()) {
		auto spreads = sess.formed_teams | std::views::transform([](const auto &t) { return t.total_point(); });
		auto [min_it, max_it] = std::ranges::minmax_element(spreads);
		double min_total = *min_it;
		double max_total = *max_it;

		for (std::size_t i = 0; i < sess.formed_teams.size(); ++i) {
			const auto &team = sess.formed_teams[i];
			body += std::format("隊伍 {}（總分數 {:.3f} CP）：", i + 1, team.total_point());

			bool first = true;
			for (const auto &m : team.members) {
				if (!first)
					body += "、";
				body += util::mention(m.id);
				first = false;
			}
			body += "\n";
		}
		body += std::format("最大分數差：{:.3f} CP\n", max_total - min_total);
	}

	e.set_description(body);
	msg.add_embed(e);

	// Add user select menu
	auto select_menu = create_user_select_menu(sess.panel_id, all_users, sess.selected_users);
	dpp::component row1;
	row1.add_component(select_menu);
	msg.add_component(row1);

	// Add buttons
	dpp::component row2;
	row2.add_component(dpp::component()
												 .set_type(dpp::cot_button)
												 .set_style(dpp::cos_primary)
												 .set_label("分配")
												 .set_id(std::format("panel:{}:assign", sess.panel_id))
												 .set_disabled(!can_assign));

	row2.add_component(dpp::component()
												 .set_type(dpp::cot_button)
												 .set_style(dpp::cos_success)
												 .set_label("新增場次")
												 .set_id(std::format("panel:{}:newmatch", sess.panel_id))
												 .set_disabled(sess.formed_teams.empty()));

	row2.add_component(
			dpp::component().set_type(dpp::cot_button).set_style(dpp::cos_danger).set_label("結束").set_id(std::format("panel:{}:end", sess.panel_id)));

	msg.add_component(row2);

	return msg;
}

auto panel_builder::build_setwinner_panel(const panel_session &sess, std::span<const std::pair<std::size_t, match_record>> recent_matches) const -> dpp::message
{
	dpp::message msg;
	dpp::embed e;
	e.set_title("勝負記錄面板");

	std::string body;

	// Find current match
	std::optional<match_record> current_match;
	if (sess.selected_match_index) {
		for (const auto &[idx, match] : recent_matches) {
			if (idx == *sess.selected_match_index) {
				current_match = match;
				break;
			}
		}
	}

	if (!current_match && !recent_matches.empty()) {
		current_match = recent_matches.front().second;
	}

	if (current_match) {
		body += std::format("建立時間：**{}**\n\n", format_timestamp(current_match->when));

		// Show teams with current ratings
		for (std::size_t i = 0; i < current_match->teams.size(); ++i) {
			const auto &team = current_match->teams[i];
			double total = team.total_point();

			body += std::format("隊伍 {}（總分 {:.3f} CP）：", i + 1, total);

			bool first = true;
			for (const auto &m : team.members) {
				if (!first)
					body += "、";
				body += util::mention(m.id);
				first = false;
			}
			body += "\n";
		}

		// Show current winners
		if (!current_match->winning_teams.empty()) {
			body += "\n**目前標記勝方**：";
			bool first = true;
			for (int w : current_match->winning_teams) {
				if (!first)
					body += "、";
				body += std::format("隊伍 {}", w + 1);
				first = false;
			}
			body += "\n";
		}
	}
	else {
		body += "（沒有可顯示的場次）\n";
	}

	e.set_description(body);
	msg.add_embed(e);

	// Add match select menu
	if (!recent_matches.empty()) {
		auto select_menu = create_match_select_menu(sess.panel_id, recent_matches, sess.selected_match_index);
		dpp::component row1;
		row1.add_component(select_menu);
		msg.add_component(row1);
	}

	// Add winner buttons
	if (current_match) {
		dpp::component row;
		int in_row = 0;

		for (std::size_t i = 0; i < current_match->teams.size(); ++i) {
			if (in_row == 5) {
				msg.add_component(row);
				row = dpp::component{};
				in_row = 0;
			}

			row.add_component(dpp::component()
														.set_type(dpp::cot_button)
														.set_style(dpp::cos_success)
														.set_label(std::format("隊伍 {} 勝", i + 1))
														.set_id(std::format("panel:{}:win:{}", sess.panel_id, i)));
			++in_row;
		}

		if (in_row > 0) {
			msg.add_component(row);
		}
	}

	// Add end button
	dpp::component end_row;
	end_row.add_component(
			dpp::component().set_type(dpp::cot_button).set_style(dpp::cos_danger).set_label("結束").set_id(std::format("panel:{}:end", sess.panel_id)));
	msg.add_component(end_row);

	return msg;
}

auto panel_builder::create_user_select_menu(const std::string &panel_id, std::span<const user> users, std::span<const dpp::snowflake> selected) const
		-> dpp::component
{
	dpp::component menu;
	menu.set_type(dpp::cot_selectmenu);
	menu.set_id(std::format("panel:{}:select", panel_id));
	menu.set_placeholder("選擇參與分配的成員 (可複選)");

	std::unordered_set<uint64_t> selected_set;
	for (auto id : selected) {
		selected_set.insert(util::id_to_u64(id));
	}

	std::size_t max_options = std::min(users.size(), constants::limits::max_discord_select_options);

	for (std::size_t i = 0; i < max_options; ++i) {
		const auto &u = users[i];
		bool is_selected = selected_set.contains(util::id_to_u64(u.id));

		std::string label = u.username.empty() ? util::mention(u.id) : u.username;
		label += std::format(" ({:.3f} CP)", u.point);

		dpp::select_option opt(label, std::to_string(util::id_to_u64(u.id)));
		if (is_selected) {
			opt.set_default(true);
		}

		menu.add_select_option(std::move(opt));
	}

	menu.set_min_values(0);
	menu.set_max_values(static_cast<int>(max_options));

	return menu;
}

auto panel_builder::create_match_select_menu(const std::string &panel_id, std::span<const std::pair<std::size_t, match_record>> matches,
																						 std::optional<std::size_t> selected) const -> dpp::component
{
	dpp::component menu;
	menu.set_type(dpp::cot_selectmenu);
	menu.set_id(std::format("panel:{}:choose", panel_id));
	menu.set_placeholder("選擇要設定勝負的場次");
	menu.set_min_values(1);
	menu.set_max_values(1);

	// Newest first; label #1 = newest
	for (std::size_t i = 0; i < matches.size(); ++i) {
		const auto &[idx, match] = matches[i];

		dpp::select_option opt;
		opt.set_label(std::format("#{} {}", i + 1, format_timestamp(match.when)));
		opt.set_value(std::to_string(idx));

		if (selected && *selected == idx) {
			opt.set_default(true);
		}

		menu.add_select_option(std::move(opt));
	}

	return menu;
}
} // namespace terry
