// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include <array>
#include <bit>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <zstd.h>

namespace
{
	using u8 = std::uint8_t;
	using u32 = std::uint32_t;
	using u64 = std::uint64_t;
	using s32 = std::int32_t;

	constexpr std::size_t SQRT_CORRECTION_BITS = 1u << 24;
	constexpr std::size_t FIRST_ONE_CORRECTION_VALUES = 1u << 16;
	constexpr std::size_t RECIPROCAL_CORRECTION_BITS = 1u << 23;
	constexpr std::size_t SQRT_CORRECTION_WORDS = SQRT_CORRECTION_BITS / 64;
	constexpr std::size_t RECIPROCAL_CORRECTION_WORDS = RECIPROCAL_CORRECTION_BITS / 64;

	struct CarrySaveResult
	{
		u32 sum;
		u32 carry;
	};

	struct BoothRecode
	{
		u32 data;
		u32 negate;
	};

	struct Tables
	{
		std::array<u64, SQRT_CORRECTION_WORDS> sqrt = {};
		std::array<u8, FIRST_ONE_CORRECTION_VALUES> first_one = {};
		std::array<u64, RECIPROCAL_CORRECTION_WORDS> reciprocal = {};
	};

	[[noreturn]] void Fail(std::string_view message)
	{
		std::cerr << message << '\n';
		std::exit(1);
	}

	constexpr CarrySaveResult CarrySaveAdd(u32 a, u32 b, u32 c)
	{
		const u32 partial = a ^ b;
		return {partial ^ c, ((a & b) | (partial & c)) << 1};
	}

	constexpr s32 QuotientSelect(CarrySaveResult current)
	{
		constexpr u32 mask = (1u << 24) - 1;
		const u32 test_bits = ((current.sum & ~mask) + current.carry) | (current.sum & mask);
		const s32 test = std::bit_cast<s32>(test_bits);
		return static_cast<s32>(test >= (1 << 23)) - static_cast<s32>(test < -(1 << 24));
	}

	constexpr u32 ShiftSignedBit(s32 value, u32 shift)
	{
		return static_cast<u32>(value) << shift;
	}

	constexpr u32 ExactSqrtMantissa(u32 mantissa, bool even_exponent)
	{
		u32 radicand = mantissa << 1;
		if (even_exponent)
			radicand <<= 1;

		CarrySaveResult current = {radicand, 0};
		u32 quotient = 0;
		s32 quotient_bit = 1;
		for (s32 iteration = 0; iteration < 23; iteration++)
		{
			const u32 adjust = quotient + ShiftSignedBit(quotient_bit, 24 - iteration);
			quotient += ShiftSignedBit(quotient_bit, 25 - iteration);
			const u32 add = quotient_bit > 0 ? ~adjust : quotient_bit < 0 ? adjust :
			                                                                0;
			current.carry += static_cast<u32>(quotient_bit > 0);
			const CarrySaveResult next = CarrySaveAdd(current.sum, current.carry, add);
			const u32 select = 0u - static_cast<u32>(quotient_bit != 0);
			quotient_bit = QuotientSelect({(current.sum & ~select) | (next.sum & select),
				(current.carry & ~select) | (next.carry & select)});
			current = {next.sum << 1, next.carry << 1};
		}

		const u32 adjust = quotient + ShiftSignedBit(quotient_bit, 1);
		quotient += ShiftSignedBit(quotient_bit, 2);
		const u32 add = quotient_bit > 0 ? ~adjust : quotient_bit < 0 ? adjust :
		                                                                0;
		current.carry += static_cast<u32>(quotient_bit > 0);
		const CarrySaveResult next = CarrySaveAdd(current.sum, current.carry, add);
		const u32 select = 0u - static_cast<u32>(quotient_bit != 0);
		quotient_bit = QuotientSelect({(current.sum & ~select) | (next.sum & select),
			(current.carry & ~select) | (next.carry & select)});
		quotient += ShiftSignedBit(quotient_bit, 1);
		return quotient >> 2;
	}

	constexpr u64 IntegerSqrt(u64 value)
	{
		u64 remainder = value;
		u64 result = 0;
		u64 bit = u64{1} << 62;
		while (bit > remainder)
			bit >>= 2;

		while (bit != 0)
		{
			if (remainder >= result + bit)
			{
				remainder -= result + bit;
				result = (result >> 1) + bit;
			}
			else
			{
				result >>= 1;
			}
			bit >>= 2;
		}
		return result;
	}

	constexpr u32 ExactSrtReciprocalMantissa(u32 divisor)
	{
		const u32 dividend = 0x800000u << 2;
		const u32 shifted_divisor = divisor << 2;
		CarrySaveResult current = {dividend, 0};
		u32 quotient = 0;
		s32 quotient_bit = 1;
		for (u32 iteration = 0; iteration < 23; iteration++)
		{
			quotient = (quotient << 1) + quotient_bit;
			const u32 add = quotient_bit > 0 ? ~shifted_divisor :
			                quotient_bit < 0 ? shifted_divisor :
			                                   0;
			current.carry += static_cast<u32>(quotient_bit > 0);
			const CarrySaveResult next = CarrySaveAdd(current.sum, current.carry, add);
			const u32 select = 0u - static_cast<u32>(quotient_bit != 0);
			quotient_bit = QuotientSelect({(current.sum & ~select) | (next.sum & select),
				(current.carry & ~select) | (next.carry & select)});
			current = {next.sum << 1, next.carry << 1};
		}

		quotient = (quotient << 1) + quotient_bit;
		const u32 add = quotient_bit > 0 ? ~shifted_divisor :
		                quotient_bit < 0 ? shifted_divisor :
		                                   0;
		current.carry += static_cast<u32>(quotient_bit > 0);
		const CarrySaveResult next = CarrySaveAdd(current.sum, current.carry, add);
		const u32 select = 0u - static_cast<u32>(quotient_bit != 0);
		quotient_bit = QuotientSelect({(current.sum & ~select) | (next.sum & select),
			(current.carry & ~select) | (next.carry & select)});
		quotient = (quotient << 1) + quotient_bit;
		return quotient >= (1u << 24) ? quotient >> 1 : quotient;
	}

	constexpr BoothRecode Booth(u32 a, u32 b, u32 bit)
	{
		const u32 test = (bit ? b >> (bit * 2 - 1) : b << 1) & 7;
		a <<= bit * 2;
		a += (test == 3 || test == 4) ? a : 0;
		const u32 neg = (test >= 4 && test <= 6) ? ~0u : 0;
		const u32 pos = 1u << (bit * 2);
		a ^= neg & (0u - pos);
		a &= (test >= 1 && test <= 6) ? ~0u : 0;
		return {a, neg & pos};
	}

	constexpr u64 ExactMulMantissa(u32 a, u32 b)
	{
		const u64 full = static_cast<u64>(a) * b;
		const BoothRecode b0 = Booth(a, b, 0);
		const BoothRecode b1 = Booth(a, b, 1);
		const BoothRecode b2 = Booth(a, b, 2);
		const BoothRecode b3 = Booth(a, b, 3);
		const BoothRecode b4 = Booth(a, b, 4);
		const BoothRecode b5 = Booth(a, b, 5);
		const BoothRecode b6 = Booth(a, b, 6);
		BoothRecode b7 = Booth(a, b, 7);

		const CarrySaveResult t0 = CarrySaveAdd(b1.data, b2.data, b3.data);
		CarrySaveResult t1 = CarrySaveAdd(b4.data & ~0x7ffu, b5.data & ~0xfffu, b6.data);
		t1.carry |= b6.negate | (b5.data & 0x800);
		b7.data |= (b5.data & 0x400) + b5.negate;

		const CarrySaveResult t2 = CarrySaveAdd(b0.data, t0.sum, t0.carry);
		const CarrySaveResult t3 = CarrySaveAdd(b7.data, t1.sum, t1.carry);
		const CarrySaveResult t4 = CarrySaveAdd(t2.carry, t3.sum, t3.carry);
		CarrySaveResult t5 = CarrySaveAdd(t2.sum, t4.sum, t4.carry);

		t5.carry += b7.negate;
		t5.sum &= ~0x7fffu;
		t5.carry &= ~0x7fffu;
		const u32 ps2_lo = t5.sum + t5.carry;
		return full - ((ps2_lo ^ full) & 0x8000);
	}

	void SetBit(auto& table, std::size_t index)
	{
		table[index >> 6] |= u64{1} << (index & 63);
	}

	std::unique_ptr<Tables> GenerateTables()
	{
		auto tables = std::make_unique<Tables>();
		for (u32 parity = 0; parity < 2; parity++)
		{
			const u32 first_mantissa = 1u << 23;
			const u64 first_radicand = static_cast<u64>(first_mantissa << (parity ? 2 : 1)) << 22;
			u32 floor_root = static_cast<u32>(IntegerSqrt(first_radicand));
			for (u32 fraction = 0; fraction < (1u << 23); fraction++)
			{
				const u32 mantissa = fraction | (1u << 23);
				const u64 radicand = static_cast<u64>(mantissa << (parity ? 2 : 1)) << 22;
				while (static_cast<u64>(floor_root + 1) * (floor_root + 1) <= radicand)
					floor_root++;
				const u32 exact = ExactSqrtMantissa(mantissa, parity != 0);
				if (exact < floor_root || exact - floor_root > 1)
					Fail("Invalid PS2 SQRT correction");
				if (exact != floor_root)
					SetBit(tables->sqrt, fraction | (parity << 23));
			}
		}

		for (u32 low = 0; low < (1u << 16); low++)
		{
			const u32 operand = 0x800000 | low;
			const u64 full = static_cast<u64>(0x800000) * operand;
			tables->first_one[low] = static_cast<u8>(ExactMulMantissa(0x800000, operand) != full);
		}

		for (u32 fraction = 0; fraction < RECIPROCAL_CORRECTION_BITS; fraction++)
		{
			const u32 divisor = 0x800000u | fraction;
			const u32 ordinary = static_cast<u32>(
				(static_cast<u64>(0x800000u) << (fraction == 0 ? 23 : 24)) / divisor);
			const u32 exact = ExactSrtReciprocalMantissa(divisor);
			if (exact < ordinary || exact - ordinary > 1)
				Fail("Invalid PS2 reciprocal correction");
			if (exact != ordinary)
				SetBit(tables->reciprocal, fraction);
		}
		return tables;
	}

	std::vector<u8> CompressTable(const auto& table)
	{
		std::vector<u8> compressed(ZSTD_compressBound(sizeof(table)));
		const std::size_t compressed_size = ZSTD_compress(compressed.data(), compressed.size(),
			table.data(), sizeof(table), 19);
		if (ZSTD_isError(compressed_size))
			Fail(ZSTD_getErrorName(compressed_size));
		compressed.resize(compressed_size);
		return compressed;
	}

	void WriteCompressedTable(std::ostringstream& output, const std::vector<u8>& table)
	{
		output << std::hex << std::setfill('0');
		const std::size_t word_count = (table.size() + sizeof(u64) - 1) / sizeof(u64);
		for (std::size_t index = 0; index < word_count; index++)
		{
			const std::size_t offset = index * sizeof(u64);
			u64 word = 0;
			for (std::size_t byte = 0; byte < sizeof(word) && offset + byte < table.size(); byte++)
				word |= static_cast<u64>(table[offset + byte]) << (byte * 8);

			if ((index & 7) == 0)
				output << '\t';
			output << "0x" << std::setw(16) << word << "ULL,";
			output << (((index & 7) == 7 || index + 1 == word_count) ? "\n" : " ");
		}
		output << std::dec;
	}

	std::size_t CountSetBits(const auto& table)
	{
		std::size_t count = 0;
		for (const auto value : table)
			count += std::popcount(value);
		return count;
	}

	std::string GenerateSource(const Tables& tables)
	{
		const std::vector<u8> compressed_sqrt = CompressTable(tables.sqrt);
		const std::vector<u8> compressed_first_one = CompressTable(tables.first_one);
		const std::vector<u8> compressed_reciprocal = CompressTable(tables.reciprocal);
		std::ostringstream output;
		output << "// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team\n"
				  "// SPDX-License-Identifier: GPL-3.0+\n\n"
				  "// Generated by tools/microvu_table_generator.cpp. Do not edit manually.\n";
		output << "// SQRT correction bits set: " << CountSetBits(tables.sqrt) << '\n';
		output << "// SQRT bytes: " << sizeof(tables.sqrt) << " -> " << compressed_sqrt.size() << '\n';
		output << "// First-one bytes: " << sizeof(tables.first_one) << " -> " << compressed_first_one.size() << '\n';
		output << "// Reciprocal correction bits set: " << CountSetBits(tables.reciprocal) << '\n';
		output << "// Reciprocal bytes: " << sizeof(tables.reciprocal) << " -> " << compressed_reciprocal.size() << "\n\n";
		output << "#include \"microVU_SoftFloatTables.h\"\n\n"
				  "namespace MicroVUSoftFloatTables\n{\n"
				  "alignas(64) constinit const std::uint64_t compressed_sqrt_correction_table[] = {\n";
		WriteCompressedTable(output, compressed_sqrt);
		output << "};\n";
		output << "constinit const std::size_t compressed_sqrt_correction_table_size = "
			   << compressed_sqrt.size() << ";\n\n";
		output << "alignas(64) constinit const std::uint64_t compressed_first_one_correction_table[] = {\n";
		WriteCompressedTable(output, compressed_first_one);
		output << "};\n";
		output << "constinit const std::size_t compressed_first_one_correction_table_size = "
			   << compressed_first_one.size() << ";\n";
		output << "alignas(64) constinit const std::uint64_t compressed_reciprocal_correction_table[] = {\n";
		WriteCompressedTable(output, compressed_reciprocal);
		output << "};\n";
		output << "constinit const std::size_t compressed_reciprocal_correction_table_size = "
			   << compressed_reciprocal.size() << ";\n";
		output << "} // namespace MicroVUSoftFloatTables\n";
		return output.str();
	}

	std::string ReadFile(const std::filesystem::path& path)
	{
		std::ifstream input(path, std::ios::binary);
		if (!input)
			return {};
		return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
	}

	void ProcessFile(const std::filesystem::path& path, const std::string& expected)
	{
		if (ReadFile(path) == expected)
		{
			std::cout << path.string() << " is up to date\n";
			return;
		}

		std::ofstream output(path, std::ios::binary | std::ios::trunc);
		if (!output || !output.write(expected.data(), static_cast<std::streamsize>(expected.size())))
			Fail("Failed to write " + path.string());
		std::cout << "Wrote " << path.string() << '\n';
	}
} // namespace

int main(int argc, char** argv)
{
	if (argc != 2)
	{
		std::cerr << "Usage: microvu_table_generator <output-directory>\n";
		return 1;
	}

	const std::filesystem::path output_directory = argv[1];
	std::error_code error;
	std::filesystem::create_directories(output_directory, error);
	if (error)
	{
		Fail("Failed to create output directory: " + error.message());
	}

	std::cout << "Generating microVU soft-float correction tables...\n";
	const std::unique_ptr<Tables> tables = GenerateTables();
	ProcessFile(output_directory / "microVU_SoftFloatTables.cpp", GenerateSource(*tables));
	return 0;
}
