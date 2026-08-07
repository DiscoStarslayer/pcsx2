// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>

namespace MicroVUSoftFloatTables
{
	static_assert(std::endian::native == std::endian::little);

	inline constexpr std::size_t SQRT_CORRECTION_BITS = 1u << 24;
	inline constexpr std::size_t FIRST_ONE_CORRECTION_VALUES = 1u << 16;
	inline constexpr std::size_t RECIPROCAL_CORRECTION_BITS = 1u << 23;

	using SqrtCorrectionTable = std::array<std::uint64_t, SQRT_CORRECTION_BITS / 64>;
	using FirstOneCorrectionTable = std::array<std::uint8_t, FIRST_ONE_CORRECTION_VALUES>;
	using ReciprocalCorrectionTable =
		std::array<std::uint64_t, RECIPROCAL_CORRECTION_BITS / 64>;

	alignas(64) extern const std::uint64_t compressed_sqrt_correction_table[];
	extern const std::size_t compressed_sqrt_correction_table_size;
	alignas(64) extern const std::uint64_t compressed_first_one_correction_table[];
	extern const std::size_t compressed_first_one_correction_table_size;
	alignas(64) extern const std::uint64_t compressed_reciprocal_correction_table[];
	extern const std::size_t compressed_reciprocal_correction_table_size;

	extern const std::uint64_t* sqrt_correction_lookup;
	extern const std::uint8_t* first_one_correction_lookup;
	extern const std::uint64_t* reciprocal_correction_lookup;

	void InitializeCorrectionTables();
} // namespace MicroVUSoftFloatTables
