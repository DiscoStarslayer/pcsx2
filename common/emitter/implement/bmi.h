// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

// Implement BMI1/BMI2 instruction set

namespace x86Emitter
{

	struct xImplBMI_RVM
	{
		SIMDInstructionInfo info;

		// RVM
		// MULX 	Unsigned multiply without affecting flags, and arbitrary destination registers
		// PDEP 	Parallel bits deposit
		// PEXT 	Parallel bits extract
		// ANDN 	Logical and not 	~x & y
		void operator()(const xRegisterInt& to, const xRegisterInt& from1, const xRegisterInt& from2) const;
		void operator()(const xRegisterInt& to, const xRegisterInt& from1, const xIndirectVoid& from2) const;
	};
} // namespace x86Emitter
