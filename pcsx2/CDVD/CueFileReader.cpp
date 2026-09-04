// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "CDVD/CueFileReader.h"
#include "CDVD/CDVD.h"

#include "common/Error.h"
#include "common/ProgressCallback.h"

#include <cdio/bincue.h>
#include <cdio/track.h>

#include <algorithm>
#include <cstring>

namespace
{
	constexpr u32 CHUNK_SECTORS = 55;
	constexpr u64 CHUNK_SIZE = static_cast<u64>(CHUNK_SECTORS) * CueFileReader::RAW_SECTOR_SIZE;
	constexpr u8 TRACK_TYPES[] = {
		CDVD_AUDIO_TRACK, CDVD_MODE2_TRACK, CDVD_MODE2_TRACK, CDVD_MODE1_TRACK, CDVD_MODE1_TRACK};
} // namespace

CueFileReader::CueFileReader()
{
	m_internalBlockSize = RAW_SECTOR_SIZE;
}

CueFileReader::~CueFileReader()
{
	Close();
}

bool CueFileReader::Open2(std::string filename, Error* error)
{
	m_filename = std::move(filename);

	m_cdio = cdio_open_cue(m_filename.c_str());
	if (!m_cdio)
	{
		Error::SetStringFmt(error, "Failed to open CUE sheet '{}'", m_filename);
		return false;
	}

	const track_t first = cdio_get_first_track_num(m_cdio);
	const track_t count = cdio_get_num_tracks(m_cdio);
	const lsn_t leadout = cdio_get_track_lsn(m_cdio, CDIO_CDROM_LEADOUT_TRACK);
	if (first == CDIO_INVALID_TRACK || count == CDIO_INVALID_TRACK || !count || leadout <= 0)
	{
		Error::SetStringFmt(error, "CUE sheet '{}' has an invalid table of contents", m_filename);
		Close2();
		return false;
	}

	m_blocks = static_cast<u32>(leadout);
	m_tracks.reserve(count);
	m_data_files.reserve(count);
	for (track_t track = first; track < first + count; track++)
	{
		if (!AddTrack(track, error))
		{
			Close2();
			return false;
		}
	}

	return true;
}

bool CueFileReader::AddTrack(const u8 number, Error* error)
{
	const lsn_t index1 = cdio_get_track_lsn(m_cdio, number);
	const lsn_t pregap = cdio_get_track_pregap_lsn(m_cdio, number);
	const lsn_t next_pregap = cdio_get_track_pregap_lsn(m_cdio, number + 1);
	const lsn_t index0 = (pregap == CDIO_INVALID_LSN) ? index1 : pregap;
	const lsn_t end = (next_pregap == CDIO_INVALID_LSN) ? cdio_get_track_lsn(m_cdio, number + 1) : next_pregap;
	if (index0 < 0 || index1 < index0 || end <= index1 || end > static_cast<lsn_t>(m_blocks))
	{
		Error::SetStringFmt(error, "CUE sheet '{}' has invalid positions for track {}", m_filename, number);
		return false;
	}

	const track_format_t format = cdio_get_track_format(m_cdio, number);
	const u8 type = (format < TRACK_FORMAT_ERROR) ? TRACK_TYPES[format] : 0;
	if (!type)
	{
		Error::SetStringFmt(error, "CUE sheet '{}' has an unsupported format for track {}", m_filename, number);
		return false;
	}

	m_tracks.push_back({number, type, static_cast<u32>(index0), static_cast<u32>(index1)});
	if (const char* const file = cdio_bincue_get_track_filename(m_cdio, number);
		file && std::find(m_data_files.begin(), m_data_files.end(), file) == m_data_files.end())
	{
		m_data_files.emplace_back(file);
	}

	return true;
}

bool CueFileReader::Precache2(ProgressCallback* progress, Error* error)
{
	const u64 size = GetSize();
	if (!CheckAvailableMemoryForPrecaching(size, error))
		return false;

	std::unique_ptr<u8[]> cache = std::make_unique_for_overwrite<u8[]>(static_cast<size_t>(size));
	progress->SetProgressRange(m_blocks);
	for (u32 lsn = 0; lsn < m_blocks;)
	{
		if (progress->IsCancelled())
		{
			Error::SetStringView(error, "CUE precaching was cancelled.");
			return false;
		}

		const u32 count = std::min(CHUNK_SECTORS, m_blocks - lsn);
		if (!ReadSectors(cache.get() + (static_cast<u64>(lsn) * RAW_SECTOR_SIZE), lsn, count))
		{
			Error::SetStringFmt(error, "Failed to precache CUE sectors starting at {}", lsn);
			return false;
		}

		lsn += count;
		progress->SetProgressValue(lsn);
	}

	m_cache = std::move(cache);
	return true;
}

ThreadedFileReader::Chunk CueFileReader::ChunkForOffset(const u64 offset)
{
	const u64 size = GetSize();
	if (offset >= size)
		return {-1, 0, 0};

	const s64 id = static_cast<s64>(offset / CHUNK_SIZE);
	const u64 start = static_cast<u64>(id) * CHUNK_SIZE;
	return {id, start, static_cast<u32>(std::min(CHUNK_SIZE, size - start))};
}

int CueFileReader::ReadChunk(void* dst, const s64 id)
{
	if (id < 0)
		return -1;

	const Chunk chunk = ChunkForOffset(static_cast<u64>(id) * CHUNK_SIZE);
	if (chunk.chunkID < 0)
		return -1;

	if (m_cache)
	{
		std::memcpy(dst, m_cache.get() + chunk.offset, chunk.length);
		return static_cast<int>(chunk.length);
	}

	if (!ReadSectors(dst, static_cast<u32>(chunk.offset / RAW_SECTOR_SIZE), chunk.length / RAW_SECTOR_SIZE))
		return 0;
	return static_cast<int>(chunk.length);
}

void CueFileReader::Close2()
{
	m_cache.reset();
	cdio_destroy(m_cdio);
	m_cdio = nullptr;
	m_data_files.clear();
	m_tracks.clear();
	m_blocks = 0;
}

bool CueFileReader::ReadSectors(void* dst, const u32 lsn, const u32 count) const
{
	return cdio_bincue_read_raw_sectors(m_cdio, dst, static_cast<lsn_t>(lsn), count) == DRIVER_OP_SUCCESS;
}

const CueFileReader::Track* CueFileReader::FindTrack(const u32 lsn) const
{
	if (lsn >= m_blocks)
		return nullptr;

	const auto it = std::find_if(m_tracks.rbegin(), m_tracks.rend(),
		[lsn](const Track& track) { return lsn >= track.index0_lsn; });
	return (it == m_tracks.rend()) ? nullptr : &*it;
}
