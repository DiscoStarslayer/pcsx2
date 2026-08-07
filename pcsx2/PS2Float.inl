// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include "Common.h"

#include <bit>

namespace PS2FloatDetail
{
struct BoothRecode
{
	u32 data;
	u32 negate;
};

struct AddResult
{
	u32 lo;
	u32 hi;
};

static __fi BoothRecode Booth(u32 a, u32 b, u32 bit)
{
	u32 test = (bit ? b >> (bit * 2 - 1) : b << 1) & 7;
	a <<= (bit * 2);
	a += (test == 3 || test == 4) ? a : 0;
	u32 neg = (test >= 4 && test <= 6) ? ~0u : 0;
	u32 pos = 1 << (bit * 2);
	a ^= (neg & (0u - pos));
	a &= (test >= 1 && test <= 6) ? ~0u : 0;
	return {a, neg & pos};
}

static __fi AddResult Add3(u32 a, u32 b, u32 c)
{
	u32 u = a ^ b;
	return {u ^ c, ((u & c) | (a & b)) << 1};
}

static __fi u64 MulMantissa(u32 a, u32 b)
{
	u64 full = static_cast<u64>(a) * static_cast<u64>(b);
	BoothRecode b0 = Booth(a, b, 0);
	BoothRecode b1 = Booth(a, b, 1);
	BoothRecode b2 = Booth(a, b, 2);
	BoothRecode b3 = Booth(a, b, 3);
	BoothRecode b4 = Booth(a, b, 4);
	BoothRecode b5 = Booth(a, b, 5);
	BoothRecode b6 = Booth(a, b, 6);
	BoothRecode b7 = Booth(a, b, 7);

	AddResult t0 = Add3(b1.data, b2.data, b3.data);
	AddResult t1 = Add3(b4.data & ~0x7ffu, b5.data & ~0xfffu, b6.data);
	t1.hi |= b6.negate | (b5.data & 0x800);
	b7.data |= (b5.data & 0x400) + b5.negate;

	AddResult t2 = Add3(b0.data, t0.lo, t0.hi);
	AddResult t3 = Add3(b7.data, t1.lo, t1.hi);
	AddResult t4 = Add3(t2.hi, t3.lo, t3.hi);
	AddResult t5 = Add3(t2.lo, t4.lo, t4.hi);

	t5.hi += b7.negate;
	t5.lo &= ~0x7fffu;
	t5.hi &= ~0x7fffu;
	u32 ps2lo = t5.lo + t5.hi;
	const u8 correction = ((ps2lo ^ full) & 0x8000) != 0;
	return full - (static_cast<u64>(correction) << 15);
}
} // namespace PS2FloatDetail

inline __fi PS2Float PS2Float::Add(PS2Float addend)
{
	if (IsDenormalized() || addend.IsDenormalized())
	{
		bool sign = DetermineAdditionOperationSign(*this, addend);

		if (IsDenormalized() && !addend.IsDenormalized())
			return PS2Float(sign, addend.Exponent(), addend.Mantissa());
		else if (!IsDenormalized() && addend.IsDenormalized())
			return PS2Float(sign, Exponent(), Mantissa());
		else if (IsDenormalized() && addend.IsDenormalized())
		{
			if (!Sign() || !addend.Sign())
				return PS2Float(false, 0, 0);
			else if (Sign() && addend.Sign())
				return PS2Float(true, 0, 0);
			else
				Console.Error("Unhandled addition operation flags");
		}
		else
			Console.Error("Both numbers are not denormalized");

		return PS2Float(0);
	}

	u32 a = raw;
	u32 b = addend.raw;

	// exponent difference
	s32 exp_diff = Exponent() - addend.Exponent();
	if (exp_diff >= 25)
		return *this;
	else if (exp_diff <= -25)
		return addend;

	// diff = 1 .. 24, expt < expd
	if (exp_diff > 0 && exp_diff < 25)
	{
		exp_diff = exp_diff - 1;
		b = (MIN_FLOATING_POINT_VALUE << exp_diff) & b;
	}

	// diff = -24 .. -1, expd < expt
	else if (exp_diff < 0 && exp_diff > -25)
	{
		exp_diff = -exp_diff;
		exp_diff = exp_diff - 1;
		a = a & (MIN_FLOATING_POINT_VALUE << exp_diff);
	}

	return PS2Float(a).DoAdd(PS2Float(b));
}

inline __fi PS2Float PS2Float::Sub(PS2Float subtrahend)
{
	if (IsDenormalized() || subtrahend.IsDenormalized())
	{
		bool sign = DetermineSubtractionOperationSign(*this, subtrahend);

		if (IsDenormalized() && !subtrahend.IsDenormalized())
			return PS2Float(sign, subtrahend.Exponent(), subtrahend.Mantissa());
		else if (!IsDenormalized() && subtrahend.IsDenormalized())
			return PS2Float(sign, Exponent(), Mantissa());
		else if (IsDenormalized() && subtrahend.IsDenormalized())
		{
			if (!Sign() || subtrahend.Sign())
				return PS2Float(false, 0, 0);
			else if (Sign() && !subtrahend.Sign())
				return PS2Float(true, 0, 0);
			else
				Console.Error("Unhandled subtraction operation flags");
		}
		else
			Console.Error("Both numbers are not denormalized");

		return PS2Float(0);
	}

	u32 a = raw;
	u32 b = subtrahend.raw;

	// exponent difference
	s32 exp_diff = Exponent() - subtrahend.Exponent();
	if (exp_diff >= 25)
		return *this;
	else if (exp_diff <= -25)
		return subtrahend.Negate();

	// diff = 1 .. 24, expt < expd
	if (exp_diff > 0 && exp_diff < 25)
	{
		exp_diff = exp_diff - 1;
		b = (MIN_FLOATING_POINT_VALUE << exp_diff) & b;
	}

	// diff = -24 .. -1, expd < expt
	else if (exp_diff < 0 && exp_diff > -25)
	{
		exp_diff = -exp_diff;
		exp_diff = exp_diff - 1;
		a = a & (MIN_FLOATING_POINT_VALUE << exp_diff);
	}

	return PS2Float(a).DoAdd(PS2Float(b).Negate());
}

inline __fi PS2Float PS2Float::DoAdd(PS2Float other)
{
	u8 selfExponent = Exponent();
	s32 resExponent = selfExponent - other.Exponent();

	if (resExponent < 0)
		return other.DoAdd(*this);
	else if (resExponent >= 25)
		return *this;

	const u8 roundingMultiplier = 6;

	// http://graphics.stanford.edu/~seander/bithacks.html#ConditionalNegate
	u32 sign1 = (u32)((s32)raw >> 31);
	s32 selfMantissa = (s32)(((Mantissa() | 0x800000) ^ sign1) - sign1);
	u32 sign2 = (u32)((s32)other.raw >> 31);
	s32 otherMantissa = (s32)(((other.Mantissa() | 0x800000) ^ sign2) - sign2);

	s32 man = (selfMantissa << roundingMultiplier) + ((otherMantissa << roundingMultiplier) >> resExponent);
	s32 absMan = man < 0 ? -man : man;
	if (absMan == 0)
		return PS2Float(0);

	const s32 highestBit = 31 - std::countl_zero(static_cast<u32>(absMan));
	s32 rawExp = selfExponent + highestBit - (MANTISSA_BITS + roundingMultiplier);
	if (highestBit > MANTISSA_BITS)
		absMan >>= highestBit - MANTISSA_BITS;
	else
		absMan <<= MANTISSA_BITS - highestBit;

	if (rawExp > 255)
	{
		PS2Float result = man < 0 ? Min() : Max();
		result.SetOverflow();
		return result;
	}
	else if (rawExp < 1)
	{
		PS2Float result = PS2Float(((u32)man & SIGNMASK) | ((u32)absMan & 0x7FFFFF));
		result.SetUnderflow();
		return result;
	}

	return PS2Float(((u32)man & SIGNMASK) | (u32)rawExp << MANTISSA_BITS | ((u32)absMan & 0x7FFFFF));
}

inline __fi PS2Float PS2Float::Mul(PS2Float mulend)
{
	if (IsDenormalized() || mulend.IsDenormalized())
		return PS2Float(DetermineMultiplicationDivisionOperationSign(*this, mulend), 0, 0);

	return DoMul(mulend);
}

inline __fi PS2Float PS2Float::DoMul(PS2Float other)
{
	u8 selfExponent = Exponent();
	u8 otherExponent = other.Exponent();
	u32 sign = (raw ^ other.raw) & SIGNMASK;
	s32 resExponent = selfExponent + otherExponent - 127;
	if (resExponent > 255)
	{
		PS2Float result = PS2Float(sign | MAX_FLOATING_POINT_VALUE);
		result.SetOverflow();
		return result;
	}
	else if (resExponent < 0)
	{
		PS2Float result = PS2Float(sign);
		result.SetUnderflow();
		return result;
	}

	u32 selfMantissa = Mantissa() | 0x800000;
	u32 otherMantissa = other.Mantissa() | 0x800000;
	u32 resMantissa = (u32)(PS2FloatDetail::MulMantissa(selfMantissa, otherMantissa) >> MANTISSA_BITS);

	if (resMantissa > 0xFFFFFF)
	{
		resMantissa >>= 1;
		resExponent++;
	}

	if (resExponent > 255)
	{
		PS2Float result = PS2Float(sign | MAX_FLOATING_POINT_VALUE);
		result.SetOverflow();
		return result;
	}
	else if (resExponent < 1)
	{
		PS2Float result = PS2Float(sign);
		result.SetUnderflow();
		return result;
	}

	return PS2Float(sign | (u32)(resExponent << MANTISSA_BITS) | (resMantissa & 0x7FFFFF));
}
