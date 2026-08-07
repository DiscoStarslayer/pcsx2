// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include "common/emitter/x86emitter.h"

namespace X86SoftFloatEmitter
{
	using namespace x86Emitter;

	// Fixed-register primitives shared by the EE and microVU soft-float kernel generators.
	// Callers establish their stack layouts and register state before emitting these sequences.
	alignas(16) inline constexpr u32 ScalarBoothDecode[8] = {0, 1, 1, 2, 0x102, 0x101, 0x101, 0};

	inline void EmitCarrySaveAdd(int a_offset, int b_offset, int c_offset,
		int sum_offset, int carry_offset)
	{
		xMOV(eax, ptr32[rsp + a_offset]);
		xXOR(eax, ptr32[rsp + b_offset]);
		xMOV(ecx, eax);
		xAND(ecx, ptr32[rsp + c_offset]);
		xXOR(eax, ptr32[rsp + c_offset]);
		xMOV(ptr32[rsp + sum_offset], eax);
		xMOV(edx, ptr32[rsp + a_offset]);
		xAND(edx, ptr32[rsp + b_offset]);
		xOR(edx, ecx);
		xSHL(edx, 1);
		xMOV(ptr32[rsp + carry_offset], edx);
	}

	inline void EmitDivCarrySaveStep()
	{
		xXOR(eax, eax);
		xCMP(r8d, 0);
		xForwardJLE8 quotient_not_positive;
		xMOV(eax, r11d);
		xNOT(eax);
		xINC(r10d);
		xForwardJump8 add_ready_positive;
		quotient_not_positive.SetTarget();
		xForwardJGE8 quotient_not_negative;
		xMOV(eax, r11d);
		quotient_not_negative.SetTarget();
		add_ready_positive.SetTarget();

		xMOV(edx, r9d);
		xXOR(edx, r10d);
		xMOV(ecx, edx);
		xAND(ecx, eax);
		xXOR(edx, eax);
		xMOV(eax, r9d);
		xAND(eax, r10d);
		xOR(eax, ecx);
		xSHL(eax, 1);

		xTEST(r8d, r8d);
		xMOV(ecx, edx);
		xCMOVZ(ecx, r9d);
		xMOV(r8d, eax);
		xCMOVZ(r8d, r10d);
		xMOV(r9d, edx);
		xSHL(r9d, 1);
		xMOV(r10d, eax);
		xSHL(r10d, 1);
		xMOV(edx, ecx);
		xAND(edx, 0xff000000);
		xADD(edx, r8d);
		xAND(ecx, 0x00ffffff);
		xOR(edx, ecx);
		xXOR(r8d, r8d);
		xCMP(edx, 1 << 23);
		xForwardJL8 quotient_not_one;
		xMOV(r8d, 1);
		xForwardJump8 quotient_selected;
		quotient_not_one.SetTarget();
		xCMP(edx, static_cast<u32>(~0u << 24));
		xForwardJGE8 quotient_selected_nonnegative;
		xMOV(r8d, -1);
		quotient_selected_nonnegative.SetTarget();
		quotient_selected.SetTarget();
	}

	// Input: r9d = restored dividend significand, r11d = restored divisor
	// significand. Output: eax = ordinary normalized quotient T, edx = cap,
	// ecx = distance u to the next quotient, and r8d = (sma < smb). This helper
	// only constructs the one-sided predicate; it does not select T over SRT.
	inline void EmitSrtDivCapQuotient()
	{
		xMOV(eax, r9d);
		xCMP(r9d, r11d);
		xForwardJGE8 numerator_not_lt;
		xSHL(rax, 24);
		xMOV(r8d, 1);
		xForwardJump8 numerator_ready;
		numerator_not_lt.SetTarget();
		xSHL(rax, 23);
		xXOR(r8d, r8d);
		numerator_ready.SetTarget();

		xXOR(edx, edx);
		xUDIV(r11);
		xMOV(ecx, r11d);
		xSUB(ecx, edx);
		xMOV(edx, 1 << 22);
		xTEST(r8d, r8d);
		xForwardJZ8 cap_ready_not_lt;
		xMOV(edx, r11d);
		xSUB(edx, 1 << 22);
		xCMP(edx, 1 << 23);
		xForwardJGE8 cap_ready_from_max;
		xMOV(edx, 1 << 23);
		cap_ready_not_lt.SetTarget();
		cap_ready_from_max.SetTarget();
	}

	inline void EmitSqrtCarrySaveStep()
	{
		xXOR(eax, eax);
		xCMP(r8d, 0);
		xForwardJLE8 sqrt_digit_not_positive;
		xMOV(eax, edi);
		xNOT(eax);
		xINC(r10d);
		xForwardJump8 sqrt_add_ready_positive;
		sqrt_digit_not_positive.SetTarget();
		xForwardJGE8 sqrt_digit_not_negative;
		xMOV(eax, edi);
		sqrt_digit_not_negative.SetTarget();
		sqrt_add_ready_positive.SetTarget();
		xMOV(edi, eax);

		xMOV(edx, r9d);
		xXOR(edx, r10d);
		xMOV(ecx, edx);
		xAND(ecx, edi);
		xXOR(edx, edi);
		xMOV(eax, r9d);
		xAND(eax, r10d);
		xOR(eax, ecx);
		xSHL(eax, 1);

		xTEST(r8d, r8d);
		xMOV(ecx, edx);
		xCMOVZ(ecx, r9d);
		xMOV(r8d, eax);
		xCMOVZ(r8d, r10d);
		xMOV(r9d, edx);
		xSHL(r9d, 1);
		xMOV(r10d, eax);
		xSHL(r10d, 1);
		xMOV(edx, ecx);
		xAND(ecx, 0xff000000);
		xADD(ecx, r8d);
		xAND(edx, 0x00ffffff);
		xOR(ecx, edx);
		xXOR(r8d, r8d);
		xCMP(ecx, 1 << 23);
		xForwardJL8 sqrt_quotient_not_one;
		xMOV(r8d, 1);
		xForwardJump8 sqrt_quotient_selected;
		sqrt_quotient_not_one.SetTarget();
		xCMP(ecx, static_cast<u32>(~0u << 24));
		xForwardJGE8 sqrt_quotient_selected_nonnegative;
		xMOV(r8d, -1);
		sqrt_quotient_selected_nonnegative.SetTarget();
		sqrt_quotient_selected.SetTarget();
	}
} // namespace X86SoftFloatEmitter
