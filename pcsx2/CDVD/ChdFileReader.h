// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include "ThreadedFileReader.h"

#include <memory>
#include <vector>

typedef struct _chd_file chd_file;

class ChdFileReader final : public ThreadedFileReader
{
	DeclareNoncopyableObject(ChdFileReader);

public:
	ChdFileReader();
	~ChdFileReader() override;

	bool Open2(std::string filename, Error* error) override;
	bool Precache2(ProgressCallback* progress, Error* error) override;

	Chunk ChunkForOffset(u64 offset) override;
	int ReadChunk(void* dst, s64 id) override;

	void Close2() override;
	u32 GetBlockCount() const override;
	std::span<const Track> GetTracks() const override { return m_tracks; }

private:
	struct ChunkMap
	{
		u64 offset;
		u64 chd_offset;
		u32 length;
		bool audio;
	};

	bool ParseTOC(Error* error);
	bool ReadTracks(char* metadata, u32 tag, bool v2, Error* error);
	bool AddTrack(const char* metadata, bool v2, u64& disc_frame, u64& chd_frame, Error* error);
	void MapTrack(u64 disc_offset, u64 chd_offset, u32 frames, bool audio);

	chd_file* m_chd = nullptr;
	u64 m_size = 0;
	u32 m_hunk_size = 0;
	std::vector<Track> m_tracks;
	std::vector<ChunkMap> m_chunk_map;
	std::unique_ptr<u8[]> m_hunk_buffer;
};
