// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include "Config.h"
#include "common/Pcsx2Defs.h"

#include <span>
#include <string_view>
#include <vector>

class SettingsInterface;

s32 FWopen();
void FWclose();
void PHYWrite();
void PHYRead();
u32 FWread32(u32 addr);
void FWwrite32(u32 addr, u32 value);
void FWsectorReadStatusInterrupt();
void FWmixSubboardAudio(s32* left, s32* right);

namespace FireWire
{
	const char* GetConfigSection();
	std::string GetConfigSubKey(std::string_view bind_name);
	std::span<const InputBindingInfo> GetP1IOBindings();
	float GetP1IOBindValue(u32 bind_index);
	void SetP1IOBindValue(u32 bind_index, float value);
	void ResetP1IOBindState();
	bool MapP1IO(SettingsInterface& si, const std::vector<std::pair<GenericInputBinding, std::string>>& mapping);
	void ClearP1IOBindings(SettingsInterface& si);
	void CopyConfiguration(SettingsInterface* dest_si, const SettingsInterface& src_si, bool copy_bindings = true);
	void SetDefaultConfiguration(SettingsInterface* si);
}

#define FW_BASE                 0x1f808400
#define FW_NODE_ID       (FW_BASE + 0x000)
#define FW_CYCLE_TIME    (FW_BASE + 0x004)
#define FW_CTRL0         (FW_BASE + 0x008)
#define   FW_CTRL0_Root          (1 << 19)
#define   FW_CTRL0_BusIDRst      (1 << 23)
#define   FW_CTRL0_RxRst         (1 << 24)
#define   FW_CTRL0_TxRst         (1 << 25)
#define FW_CTRL1         (FW_BASE + 0x00c)
#define FW_CTRL2         (FW_BASE + 0x010)
#define FW_PHY_ACCESS    (FW_BASE + 0x014)
#define   FW_PHY_ACCESS_WRITE    (1 << 30)
#define   FW_PHY_ACCESS_READ     (1 << 31)
#define FW_INTR0         (FW_BASE + 0x020)
#define   FW_INTR0_DRFR          (1 <<  0)
#define   FW_INTR0_PBCntR        (1 <<  9)
#define   FW_INTR0_AckRcvd       (1 << 14)
#define   FW_INTR0_URx           (1 << 22)
#define   FW_INTR0_PhyRst        (1 << 29)
#define   FW_INTR0_PhyRRx        (1 << 30)
#define FW_INTR0_MASK    (FW_BASE + 0x024)
#define FW_INTR1         (FW_BASE + 0x028)
#define   FW_INTR1_UTD           (1 <<  1)
#define FW_INTR1_MASK    (FW_BASE + 0x02c)
#define FW_INTR2         (FW_BASE + 0x030)
#define FW_INTR2_MASK    (FW_BASE + 0x034)
#define FW_DMAR          (FW_BASE + 0x038)
#define FW_ACK_STAT      (FW_BASE + 0x03c)
#define FW_UBUF_TX_NEXT  (FW_BASE + 0x040)
#define FW_UBUF_TX_LAST  (FW_BASE + 0x044)
#define FW_UBUF_TX_CLR   (FW_BASE + 0x048)
#define FW_UBUF_RX_CLR   (FW_BASE + 0x04c)
#define FW_UBUF_RX       (FW_BASE + 0x050)
#define FW_UBUF_RX_LVL   (FW_BASE + 0x054)
#define FW_REG_7C        (FW_BASE + 0x07c)
#define FW_PHT_CTRL0     (FW_BASE + 0x080)
#define   FW_PHT_CTRL_EWREQ      (1 << 16)
#define   FW_PHT_CTRL_ERREQ      (1 << 17)
#define   FW_PHT_CTRL_PHTRst     (1 << 21)
#define FW_PHT_SPLIT0    (FW_BASE + 0x084)
#define FW_CH_SEL_HI0    (FW_BASE + 0x094)
#define FW_CH_SEL_LO0    (FW_BASE + 0x098)
#define FW_DMA_CTRL0     (FW_BASE + 0x0b8)
#define FW_DMA_RX_THRSH0 (FW_BASE + 0x0bc)
#define FW_DBUF_FIFO_LV0 (FW_BASE + 0x0c0)
#define   FW_DBUF_FIFO_RESET_TX  (1 << 15)
#define   FW_DBUF_FIFO_RESET_RX  (1 << 31)
#define FW_DBUF_TX_DATA0 (FW_BASE + 0x0c4)
#define FW_DBUF_RX_DATA  (FW_BASE + 0x0c8)
#define FW_PHT_CTRL1     (FW_BASE + 0x100)
#define FW_PHT_SPLIT1    (FW_BASE + 0x104)
#define FW_CH_SEL_HI1    (FW_BASE + 0x114)
#define FW_CH_SEL_LO1    (FW_BASE + 0x118)
#define FW_DMA_CTRL1     (FW_BASE + 0x138)
#define FW_DMA_RX_THRSH1 (FW_BASE + 0x13c)
#define FW_DBUF_FIFO_LV1 (FW_BASE + 0x140)
#define FW_DBUF_TX_DATA1 (FW_BASE + 0x144)
