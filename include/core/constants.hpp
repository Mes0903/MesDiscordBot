#pragma once

#include <string_view>

namespace terry::constants {

// UI Text
namespace text {
inline constexpr std::string_view unknown_command = "未知的指令";
inline constexpr std::string_view unsupported_button = "不支援的按鈕";
inline constexpr std::string_view users_not_enough = "使用者的數量不夠";
inline constexpr std::string_view users_not_found = "找不到該使用者";
inline constexpr std::string_view panel_expired = "此面板已失效";
inline constexpr std::string_view panel_owner_only = "只有面板擁有者才能操作此面板";
inline constexpr std::string_view point_must_positive = "分數需大於 0";
inline constexpr std::string_view teams_must_positive = "隊伍數量需大於 0";
inline constexpr std::string_view no_users = "尚無使用者";

inline constexpr std::string_view ok_prefix = "✅ ";
inline constexpr std::string_view err_prefix = "❌ ";
inline constexpr std::string_view trophy = "🏆 ";
} // namespace text

// File paths
namespace files {
inline constexpr std::string_view users_file = "users.json";
inline constexpr std::string_view matches_file = "matches.json";
} // namespace files

// Limits
namespace limits {
inline constexpr std::size_t max_discord_select_options = 25;
inline constexpr std::size_t max_recent_sessions = 8;
inline constexpr std::size_t default_history_count = 5;
} // namespace limits

} // namespace terry::constants
