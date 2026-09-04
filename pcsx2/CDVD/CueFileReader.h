// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include "CDVD/ThreadedFileReader.h"

#include <memory>
#include <string>
#include <vector>

struct _CdIo;

class CueFileReader final : public ThreadedFileReader
{
	DeclareNoncopyableObject(CueFileReader);

protected:
	_CdIo* m_cdio = nullptr;

	std::vector<std::string> m_data_files;
	std::vector<Track> m_tracks;

	u32 m_blocks = 0;
	std::unique_ptr<u8[]> m_cache;

public:
	static constexpr u32 RAW_SECTOR_SIZE = 2352;

	CueFileReader();
	~CueFileReader() override;

	bool Open2(std::string filename, Error* error) override;
	void Close2() override;

	Chunk ChunkForOffset(u64 offset) override;
	int ReadChunk(void* dst, s64 id) override;

	u32 GetBlockCount() const override { return m_blocks; }

	bool Precache2(ProgressCallback* progress, Error* error) override;

	const std::vector<std::string>& GetDataFiles() const { return m_data_files; }
	std::span<const Track> GetTracks() const override { return m_tracks; }

protected:
	u64 GetSize() const { return static_cast<u64>(m_blocks) * RAW_SECTOR_SIZE; }

	bool AddTrack(u8 number, Error* error);
	bool ReadSectors(void* dst, u32 lsn, u32 count) const;
};
