// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include "common/Pcsx2Types.h"

enum class VuUpperFmacSoftKind : u8
{
	Add,
	Sub,
	Mul,
	Madd,
	Msub,
};

enum class VuUpperFmacSoftOperandSource : u8
{
	Ft,
	I,
	Q,
	X,
	Y,
	Z,
	W,
};

enum class VuUpperFmacSoftDestination : u8
{
	Fd,
	Acc,
};

struct alignas(4) VuUpperFmacSoftDescriptor
{
	VuUpperFmacSoftKind kind;
	VuUpperFmacSoftOperandSource source;
	VuUpperFmacSoftDestination destination;

	constexpr bool operator==(const VuUpperFmacSoftDescriptor&) const = default;

	constexpr bool IsKind(VuUpperFmacSoftKind expected_kind) const
	{
		return kind == expected_kind;
	}

	constexpr bool IsMultiplyAdd() const
	{
		return IsKind(VuUpperFmacSoftKind::Madd) || IsKind(VuUpperFmacSoftKind::Msub);
	}

	constexpr bool IsAddSub() const
	{
		return IsKind(VuUpperFmacSoftKind::Add) || IsKind(VuUpperFmacSoftKind::Sub);
	}

	constexpr bool IsAddSubMul() const
	{
		return IsAddSub() || IsKind(VuUpperFmacSoftKind::Mul);
	}

	constexpr bool ReadsQ() const
	{
		return source == VuUpperFmacSoftOperandSource::Q;
	}

	constexpr bool UsesBroadcastOperand() const
	{
		return source >= VuUpperFmacSoftOperandSource::X;
	}

	constexpr bool WritesAcc() const
	{
		return destination == VuUpperFmacSoftDestination::Acc;
	}

	constexpr u8 OperandVariant() const
	{
		return static_cast<u8>(source);
	}

	constexpr bool PromotesNonSticky() const
	{
		return IsAddSubMul() &&
		       (ReadsQ() || source == VuUpperFmacSoftOperandSource::Ft || IsImmediateFdAddSubMul());
	}

	constexpr bool UsesRingStatusSource(u32 vu_index) const
	{
		// This shit is becoming a mess, needs a rethink
		return (IsAddSub() && ReadsQ()) ||
		       IsImmediateFdAddSubMul() ||
		       (IsAddSubMul() && source == VuUpperFmacSoftOperandSource::Ft &&
				   ((vu_index == 0 && destination == VuUpperFmacSoftDestination::Fd) ||
					   (vu_index == 1 && destination == VuUpperFmacSoftDestination::Acc)));
	}

	constexpr bool HasNativeProductUnderflowFd() const
	{
		return IsMultiplyAdd() && source == VuUpperFmacSoftOperandSource::Ft &&
		       destination == VuUpperFmacSoftDestination::Fd;
	}

	constexpr bool IsImmediateFdAddSubMul() const
	{
		return IsAddSubMul() && source == VuUpperFmacSoftOperandSource::I &&
		       destination == VuUpperFmacSoftDestination::Fd;
	}

	constexpr bool IsBroadcastFdAddSubMul() const
	{
		return IsAddSubMul() && UsesBroadcastOperand() && destination == VuUpperFmacSoftDestination::Fd;
	}
};

static_assert(sizeof(VuUpperFmacSoftDescriptor) == 4);
