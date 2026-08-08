// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once
#include "VU.h"
#include "PS2Float.h"

extern u32  VU_MACx_UPDATE(VURegs * VU, float x);
extern u32  VU_MACy_UPDATE(VURegs * VU, float y);
extern u32  VU_MACz_UPDATE(VURegs * VU, float z);
extern u32  VU_MACw_UPDATE(VURegs * VU, float w);
extern u32  VU_MACx_UPDATE(VURegs* VU, PS2Float x);
extern u32  VU_MACy_UPDATE(VURegs* VU, PS2Float y);
extern u32  VU_MACz_UPDATE(VURegs* VU, PS2Float z);
extern u32  VU_MACw_UPDATE(VURegs* VU, PS2Float w);
extern void VU_MACx_CLEAR(VURegs * VU);
extern void VU_MACy_CLEAR(VURegs * VU);
extern void VU_MACz_CLEAR(VURegs * VU);
extern void VU_MACw_CLEAR(VURegs * VU);
extern void VU_STAT_UPDATE(VURegs * VU);

static __fi u32 VU_MAC_UPDATE_PS2Float(VURegs* VU, s32 shift, PS2Float f)
{
	const u32 v = f.raw;
	bool isUnderflow = false;

	if (v & PS2Float::SIGNMASK)
		VU->macflag |= 0x0010 << shift;
	else
		VU->macflag &= ~(0x0010 << shift);

	if (f.HasUnderflow())
	{
		isUnderflow = true;
		VU->macflag = (VU->macflag & ~(0x1000 << shift)) | (0x0101 << shift);
	}

	if (f.IsZero() && !isUnderflow)
		VU->macflag = (VU->macflag & ~(0x1100 << shift)) | (0x0001 << shift);
	else if (f.HasOverflow())
		VU->macflag = (VU->macflag & ~(0x0101 << shift)) | (0x1000 << shift);
	else if (!isUnderflow)
		VU->macflag &= ~(0x1101 << shift);

	return v;
}

static __fi void VU_STAT_UPDATE_INLINE(VURegs* VU)
{
	u32 newflag = 0;
	if (VU->macflag & 0x000F)
		newflag = 0x1;
	if (VU->macflag & 0x00F0)
		newflag |= 0x2;
	if (VU->macflag & 0x0F00)
		newflag |= 0x4;
	if (VU->macflag & 0xF000)
		newflag |= 0x8;
	VU->statusflag = (VU->statusflag & 0xFC0) | newflag;
}
