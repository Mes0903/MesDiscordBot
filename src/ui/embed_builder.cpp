#include "core/constants.hpp"
#include "ui/embed_builder.hpp"

#include <format>
#include <unordered_set>

namespace terry::ui {

auto embed_builder::build_help() -> dpp::embed
{
	dpp::embed e;
	e.set_title("指令說明 / Help");

	e.add_field("使用者管理",
							"• `/adduser <user> <point>` 新增或更新成員分數\n"
							"• `/removeuser <user>` 移除成員\n"
							"• `/listusers` 顯示使用者清單",
							false);

	e.add_field("分隊面板",
							"• `/formteams <teams>` 開啟面板，預設為 2 組\n"
							"• 於下拉選單勾選參與者（Discord 限制：列表最多 25 人）\n"
							"• 按 **「分配」** 產生/重抽隊伍\n"
							"• 按 **「新增場次」** 把目前隊伍**加入對戰紀錄**（先不標勝負）\n"
							"• 按 **「結束」** 關閉面板\n"
							"• 之後用 **`/sethistory`** 選擇最近 8 場並**編輯／更改勝負**",
							false);

	e.add_field("戰績記錄", "• `/history [count]` 顯示最近戰績，預設為 5 筆記錄\n", false);

	return e;
}

auto embed_builder::build_user_list(std::span<const user> users) -> dpp::embed
{
	dpp::embed e;
	e.set_title("使用者清單");

	std::string desc;
	for (const auto &u : users) {
		int win_rate = u.games > 0 ? static_cast<int>(std::round((u.wins * 100.0) / u.games)) : 0;

		desc += std::format("{} **({:.0f} CP)** — 勝率 {}% ({}/{})\n", util::mention(u.id), u.point, win_rate, u.wins, u.games);
	}

	e.set_description(desc);
	return e;
}

auto embed_builder::build_history(std::span<const match_record> matches) -> dpp::embed
{
	dpp::embed e;
	e.set_title("近期對戰");

	std::string desc;
	int idx = 1;

	for (const auto &match : matches) {
		std::string winners;
		if (!match.winning_teams.empty()) {
			winners = "勝利隊伍：";
			bool first = true;
			for (int w : match.winning_teams) {
				if (!first)
					winners += "、";
				winners += std::format("隊伍 {}", w + 1);
				first = false;
			}
		}
		else {
			winners = "未記錄勝方";
		}

		desc += std::format("**比賽 #{}（{}）**\n", idx++, winners);
		desc += format_timestamp(match.when) + "\n";

		// Show teams
		std::unordered_set<int> winner_set(match.winning_teams.begin(), match.winning_teams.end());

		for (std::size_t i = 0; i < match.teams.size(); ++i) {
			const auto &team = match.teams[i];
			bool is_winner = winner_set.contains(static_cast<int>(i));

			std::string prefix = is_winner ? std::string(constants::text::trophy) : "🥈 ";

			desc += std::format("{}隊伍 {}：", prefix, i + 1);
			desc += format_team_members(team);
			desc += "\n";
		}

		desc += "\n";
	}

	e.set_description(desc);
	return e;
}

auto embed_builder::build_teams(std::span<const team> teams) -> dpp::embed
{
	dpp::embed e;
	e.set_title("隊伍分配結果");

	std::string desc;

	// Calculate spread
	auto totals = teams | std::views::transform([](const auto &t) { return t.total_point(); });
	auto [min_it, max_it] = std::ranges::minmax_element(totals);
	double spread = *max_it - *min_it;

	for (std::size_t i = 0; i < teams.size(); ++i) {
		const auto &team = teams[i];
		desc += std::format("**隊伍 {}** (總分 {:.0f} CP)：", i + 1, team.total_point());
		desc += format_team_members(team);
		desc += "\n";
	}

	desc += std::format("\n最大分數差：{:.0f} CP", spread);

	e.set_description(desc);
	return e;
}

auto embed_builder::format_team_members(const team &t) -> std::string
{
	std::string result;
	bool first = true;

	for (const auto &member : t.members) {
		if (!first)
			result += "、";
		result += util::mention(member.id);
		first = false;
	}

	return result;
}

} // namespace terry::ui
