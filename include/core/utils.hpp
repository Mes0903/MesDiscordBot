#pragma once

#include <dpp/dpp.h>

#include <chrono>
#include <expected>
#include <string>

namespace terry {

namespace type {
// Strong type aliases
using timestamp = std::chrono::sys_seconds;

// Error handling
struct error {
	std::string message;

	constexpr error(std::string_view sv) : message(sv) {}

	constexpr error() = default;
	constexpr error(const error &) = default;
	constexpr error(error &&) noexcept = default;
	constexpr error &operator=(const error &) = default;
	constexpr error &operator=(error &&) noexcept = default;

	// explicit object parameter
	[[nodiscard]] auto what(this const auto &self) -> std::string_view { return self.message; }
};
} // namespace type

namespace util {
// Force the const conversion operator and silence -Wconversion noise.
[[nodiscard]] constexpr auto id_to_u64(const dpp::snowflake &id) noexcept -> std::uint64_t
{
	// the const qualifier in argument would make it picks operator uint64_t() const
	return static_cast<std::uint64_t>(id);
}

// Handy mention formatter.
[[nodiscard]] inline auto mention(const dpp::snowflake &id) -> std::string { return std::format("<@{}>", id_to_u64(id)); }

// Explicit (silent) narrowing cast — just a named static_cast.
template <std::integral To, std::integral From>
[[nodiscard]] constexpr To narrow_cast(From v) noexcept
{
	return static_cast<To>(v);
}

// Checked narrowing: asserts in debug if out of range, still returns casted value.
template <std::integral To, std::integral From>
[[nodiscard]] constexpr To narrow(From v)
{
	if (!std::in_range<To>(v)) {
		assert(!"narrow(): value out of range");
	}

	return static_cast<To>(v);
}

} // namespace util

} // namespace terry
