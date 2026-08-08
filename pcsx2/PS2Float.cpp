// SPDX-FileCopyrightText: 2002-2024 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include <string>
#include <sstream>
#include <iomanip>
#include <bit>
#include "common/Pcsx2Defs.h"
#include "common/BitUtils.h"
#include "PS2Float.h"
#include "Common.h"

//****************************************************************
// Radix Divisor
// Algorithm reference: DOI 10.1109/ARITH.1995.465363
//****************************************************************

struct CSAResult
{
	u32 sum;
	u32 carry;
};

static __fi CSAResult CSA(u32 a, u32 b, u32 c)
{
	u32 u = a ^ b;
	u32 h = (a & b) | (u & c);
	u32 l = u ^ c;
	return {l, h << 1};
}

static __fi s32 quotientSelect(CSAResult current)
{
	// Note: Decimal point is between bits 24 and 25
	constexpr u32 mask = (1 << 24) - 1; // Bit 23 needs to be or'd in instead of added
	const s32 test = ((current.sum & ~mask) + current.carry) | (current.sum & mask);
	return (test >= (1 << 23)) - (test < static_cast<s32>(~0u << 24));
}

static __fi CSAResult selectCSAResult(CSAResult current, CSAResult csa, s32 quotientBit)
{
	const u32 mask = static_cast<u32>(-(quotientBit != 0));
	return {(current.sum & ~mask) | (csa.sum & mask), (current.carry & ~mask) | (csa.carry & mask)};
}

static __fi u32 mantissa(u32 x)
{
	return (x & 0x7fffff) | 0x800000;
}

static __fi u32 exponent(u32 x)
{
	return (x >> 23) & 0xff;
}

struct PS2FloatUnaryCache
{
	u32 key = 0;
	u32 raw = 0;
	u8 flags = 0;
	bool valid = false;
};

struct PS2FloatBinaryCache
{
	u32 key_a = 0;
	u32 key_b = 0;
	u32 raw = 0;
	u8 flags = 0;
	bool valid = false;
};

static __fi PS2Float ps2FloatCachedResult(u32 raw, u8 flags)
{
	PS2Float result(raw);
	result.flags = flags;
	return result;
}

static __fi u32 ps2FloatCacheIndex(u32 key)
{
	return (key * 2654435761u) >> 20;
}

static __fi u32 ps2FloatCacheIndex(u32 key_a, u32 key_b)
{
	return ps2FloatCacheIndex(key_a ^ (key_b * 2246822519u));
}

//****************************************************************
// Float Processor
//****************************************************************

PS2Float PS2Float::MulAdd(PS2Float opsend, PS2Float optend)
{
	PS2Float mulres = opsend.Mul(optend);
	return AddMulResult(mulres);
}

PS2Float PS2Float::AddMulResult(PS2Float mulres)
{
	PS2Float addres = Add(mulres);
	u32 rawres = addres.raw;
	bool oflw = addres.HasOverflow();
	bool uflw = addres.HasUnderflow();
	DetermineMacException(3, raw, HasOverflow(), mulres.HasOverflow(), mulres.Sign() ? 1 : 0, rawres, oflw, uflw);
	PS2Float result = PS2Float(rawres);
	result.SetOverflow(oflw);
	result.SetUnderflow(uflw);
	return result;
}

PS2Float PS2Float::MulAddAcc(PS2Float opsend, PS2Float optend)
{
	return AddMulResultAcc(opsend.Mul(optend));
}

PS2Float PS2Float::AddMulResultAcc(PS2Float mulres)
{
	PS2Float addres = Add(mulres);
	u32 rawres = addres.raw;
	bool oflw = addres.HasOverflow();
	bool uflw = addres.HasUnderflow();
	DetermineMacException(8, raw, HasOverflow(), mulres.HasOverflow(), mulres.Sign() ? 1 : 0, rawres, oflw, uflw);
	raw = rawres;
	SetOverflow(oflw);
	PS2Float result = PS2Float(rawres);
	result.SetOverflow(oflw);
	result.SetUnderflow(uflw);
	return result;
}

PS2Float PS2Float::MulSub(PS2Float opsend, PS2Float optend)
{
	PS2Float mulres = opsend.Mul(optend);
	return SubMulResult(mulres);
}

PS2Float PS2Float::SubMulResult(PS2Float mulres)
{
	PS2Float subres = Sub(mulres);
	u32 rawres = subres.raw;
	bool oflw = subres.HasOverflow();
	bool uflw = subres.HasUnderflow();
	DetermineMacException(4, raw, HasOverflow(), mulres.HasOverflow(), mulres.Sign() ? 1 : 0, rawres, oflw, uflw);
	PS2Float result = PS2Float(rawres);
	result.SetOverflow(oflw);
	result.SetUnderflow(uflw);
	return result;
}

PS2Float PS2Float::MulSubAcc(PS2Float opsend, PS2Float optend)
{
	return SubMulResultAcc(opsend.Mul(optend));
}

PS2Float PS2Float::SubMulResultAcc(PS2Float mulres)
{
	PS2Float subres = Sub(mulres);
	u32 rawres = subres.raw;
	bool oflw = subres.HasOverflow();
	bool uflw = subres.HasUnderflow();
	DetermineMacException(9, raw, HasOverflow(), mulres.HasOverflow(), mulres.Sign() ? 1 : 0, rawres, oflw, uflw);
	raw = rawres;
	SetOverflow(oflw);
	PS2Float result = PS2Float(rawres);
	result.SetOverflow(oflw);
	result.SetUnderflow(uflw);
	return result;
}

PS2Float PS2Float::Div(PS2Float divend)
{
	u32 a = raw;
	u32 b = divend.raw;
	static constexpr u32 CACHE_SETS = 4096;
	static constexpr u32 CACHE_WAYS = 4;
	static thread_local PS2FloatBinaryCache s_div_cache[CACHE_SETS][CACHE_WAYS];
	static thread_local u8 s_div_cache_next[CACHE_SETS]{};
	const u32 cache_index = ps2FloatCacheIndex(a, b);
	for (u32 way = 0; way < CACHE_WAYS; way++)
	{
		PS2FloatBinaryCache& entry = s_div_cache[cache_index][way];
		if (entry.valid && entry.key_a == a && entry.key_b == b)
			return ps2FloatCachedResult(entry.raw, entry.flags);
	}
	PS2FloatBinaryCache* const cache_entry =
		&s_div_cache[cache_index][s_div_cache_next[cache_index]++ & (CACHE_WAYS - 1)];
	auto cacheResult = [&](PS2Float result) {
		cache_entry->key_a = a;
		cache_entry->key_b = b;
		cache_entry->raw = result.raw;
		cache_entry->flags = result.flags;
		cache_entry->valid = true;
		return result;
	};

	u32 sign = ((a ^ b) & 0x80000000);
	if (((a & 0x7F800000) == 0) && ((b & 0x7F800000) != 0))
	{
		return cacheResult(PS2Float(sign));
	}
	if (((a & 0x7F800000) != 0) && ((b & 0x7F800000) == 0))
	{
		PS2Float result = PS2Float(sign | PS2Float::MAX_FLOATING_POINT_VALUE);
		result.SetDivideByZero();
		return cacheResult(result);
	}
	if (((a & 0x7F800000) == 0) && ((b & 0x7F800000) == 0))
	{
		PS2Float result = PS2Float(sign | PS2Float::MAX_FLOATING_POINT_VALUE);
		result.SetInvalid();
		return cacheResult(result);
	}
	u32 Dvdtexp = exponent(a);
	u32 Dvsrexp = exponent(b);
	s32 cexp = Dvdtexp - Dvsrexp + 126;
	if (cexp > 255)
	{
		PS2Float result = PS2Float(sign | PS2Float::MAX_FLOATING_POINT_VALUE);
		result.SetOverflow();
		return cacheResult(result);
	}
	else if (cexp < 0)
	{
		PS2Float result = PS2Float(sign);
		result.SetUnderflow();
		return cacheResult(result);
	}
	u32 am = mantissa(a) << 2;
	u32 bm = mantissa(b) << 2;
	struct CSAResult current = {am, 0};
	u32 quotient = 0;
	int quotientBit = 1;
#if defined(__clang__)
#pragma clang loop unroll(full)
#endif
	for (int i = 0; i < 23; i++)
	{
		quotient = (quotient << 1) + quotientBit;
		u32 add = quotientBit > 0 ? ~bm : quotientBit < 0 ? bm : 0;
		current.carry += quotientBit > 0;
		struct CSAResult csa = CSA(current.sum, current.carry, add);
		quotientBit = quotientSelect(selectCSAResult(current, csa, quotientBit));
		current.sum = csa.sum << 1;
		current.carry = csa.carry << 1;
	}
	quotient = (quotient << 1) + quotientBit;
	u32 add = quotientBit > 0 ? ~bm : quotientBit < 0 ? bm : 0;
	current.carry += quotientBit > 0;
	struct CSAResult csa = CSA(current.sum, current.carry, add);
	quotientBit = quotientSelect(selectCSAResult(current, csa, quotientBit));
	quotient = (quotient << 1) + quotientBit;
	if (quotient >= (1 << 24))
	{
		cexp += 1;
		quotient >>= 1;
	}
	if (cexp > 255)
	{
		PS2Float result = PS2Float(sign | PS2Float::MAX_FLOATING_POINT_VALUE);
		result.SetOverflow();
		return cacheResult(result);
	}
	else if (cexp < 1)
	{
		PS2Float result = PS2Float(sign);
		result.SetUnderflow();
		return cacheResult(result);
	}
	return cacheResult((quotient & 0x7fffff) | (cexp << 23) | sign);
}

PS2Float PS2Float::Sqrt()
{
	u32 a = raw;
	static constexpr u32 CACHE_SETS = 4096;
	static constexpr u32 CACHE_WAYS = 4;
	static thread_local PS2FloatUnaryCache s_sqrt_cache[CACHE_SETS][CACHE_WAYS];
	static thread_local u8 s_sqrt_cache_next[CACHE_SETS]{};
	const u32 cache_index = ps2FloatCacheIndex(a);
	for (u32 way = 0; way < CACHE_WAYS; way++)
	{
		PS2FloatUnaryCache& entry = s_sqrt_cache[cache_index][way];
		if (entry.valid && entry.key == a)
			return ps2FloatCachedResult(entry.raw, entry.flags);
	}
	PS2FloatUnaryCache* const cache_entry =
		&s_sqrt_cache[cache_index][s_sqrt_cache_next[cache_index]++ & (CACHE_WAYS - 1)];
	auto cacheResult = [&](PS2Float result) {
		cache_entry->key = a;
		cache_entry->raw = result.raw;
		cache_entry->flags = result.flags;
		cache_entry->valid = true;
		return result;
	};

	const bool negative = (a & SIGNMASK) != 0;
	if ((a & 0x7F800000) == 0)
	{
		PS2Float result = PS2Float(0);
		result.SetInvalid(negative);
		return cacheResult(result);
	}
	u32 m = mantissa(a) << 1;
	if (!(a & 0x800000)) // If exponent is odd after subtracting bias of 127
		m <<= 1;
	struct CSAResult current = {m, 0};
	u32 quotient = 0;
	s32 quotientBit = 1;
#if defined(__clang__)
#pragma clang loop unroll(full)
#endif
	for (s32 i = 0; i < 23; i++)
	{
		// Adding n to quotient adds n * (2*quotient + n) to quotient^2
		// (which is what we need to subtract from the remainder)
		u32 adjust = quotient + (quotientBit << (24 - i));
		quotient += quotientBit << (25 - i);
		u32 add = quotientBit > 0 ? ~adjust : quotientBit < 0 ? adjust : 0;
		current.carry += quotientBit > 0;
		struct CSAResult csa = CSA(current.sum, current.carry, add);
		quotientBit = quotientSelect(selectCSAResult(current, csa, quotientBit));
		current.sum = csa.sum << 1;
		current.carry = csa.carry << 1;
	}
	// Adding n to quotient adds n * (2*quotient + n) to quotient^2
	// (which is what we need to subtract from the remainder)
	u32 adjust = quotient + (quotientBit << 1);
	quotient += quotientBit << 2;
	u32 add = quotientBit > 0 ? ~adjust : quotientBit < 0 ? adjust : 0;
	current.carry += quotientBit > 0;
	struct CSAResult csa = CSA(current.sum, current.carry, add);
	quotientBit = quotientSelect(selectCSAResult(current, csa, quotientBit));
	quotient += quotientBit << 1;
	s32 Dvdtexp = exponent(a);
	Dvdtexp = (Dvdtexp + 127) >> 1;
	PS2Float result = PS2Float(((quotient >> 2) & 0x7fffff) | (Dvdtexp << 23));
	if (negative)
		result.SetInvalid();
	return cacheResult(result);
}

PS2Float PS2Float::Rsqrt(PS2Float other)
{
	return RsqrtImpl(other);
}

PS2Float PS2Float::VuRsqrt(PS2Float other)
{
	if (other.IsDenormalized())
	{
		PS2Float result(MAX_FLOATING_POINT_VALUE);
		if (IsZero())
			result.SetInvalid();
		else
			result.SetDivideByZero();
		if (other.Sign())
			result.SetInvalid();
		return result;
	}

	return RsqrtImpl(other);
}

PS2Float PS2Float::RsqrtImpl(PS2Float other)
{
	const u32 other_abs = other.Abs();
	if ((other_abs & 0x7F800000) == 0)
	{
		PS2Float result = PS2Float((raw & SIGNMASK) | PS2Float::MAX_FLOATING_POINT_VALUE);
		if (IsDenormalized())
			result.SetInvalid();
		else
			result.SetDivideByZero();
		return result;
	}
	if (IsDenormalized())
		return PS2Float(raw & SIGNMASK);

	const u32 sign = raw & SIGNMASK;
	const u32 sqrt_exp = (other.Exponent() + 127) >> 1;
	const s32 cexp = Exponent() - sqrt_exp + 126;
	if (cexp > 255)
	{
		PS2Float result = PS2Float(sign | PS2Float::MAX_FLOATING_POINT_VALUE);
		result.SetOverflow();
		return result;
	}
	else if (cexp < 0)
	{
		PS2Float result = PS2Float(sign);
		result.SetUnderflow();
		return result;
	}

	PS2Float sqrt = PS2Float(other_abs).Sqrt();
	return Div(sqrt);
}

PS2Float PS2Float::ERCPR()
{
	return PS2Float(ONE).Div(*this);
}

PS2Float PS2Float::ESQRT()
{
	return Sqrt();
}

PS2Float PS2Float::ESQUR()
{
	return Mul(*this);
}

PS2Float PS2Float::ERSQRT()
{
	// ERSQRT retains the final redundant SRT digit, unlike Q-unit RSQRT.
	return PS2Float(ONE).RsqrtImpl(*this);
}

s32 PS2Float::CompareTo(PS2Float other)
{
	s32 selfTwoComplementVal = (s32)Abs();
	if (Sign())
		selfTwoComplementVal = -selfTwoComplementVal;

	s32 otherTwoComplementVal = (s32)other.Abs();
	if (other.Sign())
		otherTwoComplementVal = -otherTwoComplementVal;

	if (selfTwoComplementVal < otherTwoComplementVal)
		return -1;
	else if (selfTwoComplementVal == otherTwoComplementVal)
		return 0;
	else
		return 1;
}

s32 PS2Float::CompareOperands(PS2Float other)
{
	u32 selfTwoComplementVal = Abs();
	u32 otherTwoComplementVal = other.Abs();

	if (selfTwoComplementVal < otherTwoComplementVal)
		return -1;
	else if (selfTwoComplementVal == otherTwoComplementVal)
		return 0;
	else
		return 1;
}

double PS2Float::ToDouble()
{
	return std::bit_cast<double>(((u64)Sign() << 63) | ((((u64)Exponent() - BIAS) + 1023ULL) << 52) | ((u64)Mantissa() << 29));
}

std::string PS2Float::ToString()
{
	double res = ToDouble();

	u32 value = raw;
	std::ostringstream oss;
	oss << std::fixed << std::setprecision(6);

	if (IsDenormalized())
	{
		oss << "Denormalized(" << res << ")";
	}
	else if (value == MAX_FLOATING_POINT_VALUE)
	{
		oss << "Fmax(" << res << ")";
	}
	else if (value == MIN_FLOATING_POINT_VALUE)
	{
		oss << "-Fmax(" << res << ")";
	}
	else
	{
		oss << "PS2Float(" << res << ")";
	}

	return oss.str();
}

u8 PS2Float::Clip(u32 f1, u32 f2, bool& cplus, bool& cminus)
{
	bool resultPlus = false;
	bool resultMinus = false;
	u32 a;

	if ((f1 & 0x7F800000) == 0)
	{
		f1 &= 0xFF800000;
	}

	a = f1;

	if ((f2 & 0x7F800000) == 0)
	{
		f2 &= 0xFF800000;
	}

	f1 = f1 & MAX_FLOATING_POINT_VALUE;
	f2 = f2 & MAX_FLOATING_POINT_VALUE;

	if ((-1 < (int)a) && (f2 < f1))
		resultPlus = true;

	cplus = resultPlus;

	if (((int)a < 0) && (f2 < f1))
		resultMinus = true;

	cminus = resultMinus;

	return 0;
}

bool PS2Float::DetermineMultiplicationDivisionOperationSign(PS2Float a, PS2Float b)
{
	return a.Sign() ^ b.Sign();
}

bool PS2Float::DetermineAdditionOperationSign(PS2Float a, PS2Float b)
{
	return a.CompareOperands(b) >= 0 ? a.Sign() : b.Sign();
}

bool PS2Float::DetermineSubtractionOperationSign(PS2Float a, PS2Float b)
{
	return a.CompareOperands(b) >= 0 ? a.Sign() : !b.Sign();
}

u8 PS2Float::DetermineMacException(u8 mode, u32 acc, bool acc_oflw, bool moflw, s32 msign, u32& addsubres, bool& oflw, bool& uflw)
{
	bool roundToMax;

	if ((mode == 3) || (mode == 8))
		roundToMax = msign == 0;
	else
	{
		if ((mode != 4) && (mode != 9))
		{
			Console.Error("Unhandled MacFlag operation flags");
			return 1;
		}

		roundToMax = msign != 0;
	}

	if (!acc_oflw)
	{
		if (moflw)
		{
			if (roundToMax)
			{
				addsubres = MAX_FLOATING_POINT_VALUE;
				uflw = false;
				oflw = true;
			}
			else
			{
				addsubres = MIN_FLOATING_POINT_VALUE;
				uflw = false;
				oflw = true;
			}
		}
	}
	else if (!moflw)
	{
		addsubres = acc;
		uflw = false;
		oflw = true;
	}
	else if (roundToMax)
	{
		addsubres = MAX_FLOATING_POINT_VALUE;
		uflw = false;
		oflw = true;
	}
	else
	{
		addsubres = MIN_FLOATING_POINT_VALUE;
		uflw = false;
		oflw = true;
	}

	return 0;
}
