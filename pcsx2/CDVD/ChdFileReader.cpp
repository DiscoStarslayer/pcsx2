// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "ChdFileReader.h"

#include "CDVD/CDVD.h"

#include "common/Assertions.h"
#include "common/Console.h"
#include "common/Error.h"
#include "common/FileSystem.h"
#include "common/Path.h"
#include "common/ProgressCallback.h"
#include "common/StringUtil.h"

#include "libchdr/chd.h"
#include "libchdr/cdrom.h"
#include "fmt/format.h"
#include "xxhash.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>
#include <utility>

static constexpr u32 MAX_PARENTS = 32; // Surely someone wouldn't be insane enough to go beyond this...
static constexpr u32 METADATA_SIZE = 256;

static std::vector<std::pair<std::string, chd_header>> s_chd_hash_cache; // <filename, header>
static std::recursive_mutex s_chd_hash_cache_mutex;

template <typename... T>
static bool Fail(Error* error, fmt::format_string<T...> format, T&&... args)
{
	Error::SetStringFmt(error, format, std::forward<T>(args)...);
	return false;
}

static u8 GetTrackType(const std::string_view type)
{
	if (type == "AUDIO")
		return CDVD_AUDIO_TRACK;
	if (type.starts_with("MODE1"))
		return CDVD_MODE1_TRACK;
	if (type.starts_with("MODE2"))
		return CDVD_MODE2_TRACK;
	return 0;
}

struct TrackMetadata
{
	char type[METADATA_SIZE]{}, pregap_type[METADATA_SIZE]{}, ignored[METADATA_SIZE]{};
	int number = 0, frames = 0, pregap = 0, postgap = 0;
	bool HasUnsupportedGaps() const { return (pregap && pregap_type[0] != 'V') || postgap; }
};

static bool ParseTrackMetadata(const char* metadata, const bool v2, TrackMetadata& track)
{
	if (!v2)
		return std::sscanf(metadata, CDROM_TRACK_METADATA_FORMAT, &track.number, track.type, track.ignored, &track.frames) == 4;
	return std::sscanf(metadata, CDROM_TRACK_METADATA2_FORMAT, &track.number, track.type, track.ignored, &track.frames,
			   &track.pregap, track.pregap_type, track.ignored, &track.postgap) == 8;
}

static void SwapAudioBytes(void* data, const u32 size)
{
	// chdman byte-swaps CDDA for compression; restore the original BIN/CUE byte order.
	u8* const bytes = static_cast<u8*>(data);
	for (u32 frame_offset = 0; frame_offset < size; frame_offset += CD_FRAME_SIZE)
	{
		for (u32 offset = 0; offset < CD_MAX_SECTOR_DATA; offset += 2)
			std::swap(bytes[frame_offset + offset], bytes[frame_offset + offset + 1]);
	}
}

// Provides an implementation of core_file which allows us to control if the underlying FILE handle is freed.
// Additionally, this class allows greater control and feedback while precaching CHD files.
// The lifetime of ChdCoreFileWrapper will be equal to that of the relevant chd_file,
// ChdCoreFileWrapper will also get destroyed if chd_open_core_file fails.
class ChdCoreFileWrapper
{
	DeclareNoncopyableObject(ChdCoreFileWrapper);

private:
	core_file m_core;
	std::FILE* m_file;
	bool m_free_file = false;
	ChdCoreFileWrapper* m_parent = nullptr;
	std::unique_ptr<u8[]> m_file_cache;
	s64 m_file_cache_size;
	s64 m_file_cache_pos;

public:
	ChdCoreFileWrapper(std::FILE* file, ChdCoreFileWrapper* parent)
		: m_file{file}
		, m_parent{parent}
	{
		m_core.argp = this;
		m_core.fsize = FSize;
		m_core.fread = FRead;
		m_core.fclose = FClose;
		m_core.fseek = FSeek;
	}

	~ChdCoreFileWrapper()
	{
		if (m_free_file && m_file)
			std::fclose(m_file);
	}

	core_file* GetCoreFile()
	{
		return &m_core;
	}

	static ChdCoreFileWrapper* FromCoreFile(core_file* file)
	{
		return reinterpret_cast<ChdCoreFileWrapper*>(file->argp);
	}

	void SetFileOwner(bool isOwner)
	{
		m_free_file = isOwner;
	}

	s64 GetPrecacheSize()
	{
		const s64 size = static_cast<size_t>(FileSystem::FSize64(m_file));
		if (m_parent != nullptr)
			return m_parent->GetPrecacheSize() + size;
		else
			return size;
	}

	bool Precache(ProgressCallback* progress, Error* error)
	{
		progress->SetProgressRange(100);

		const s64 size = GetPrecacheSize();
		return PrecacheInternal(progress, error, 0, size);
	}

private:
	bool PrecacheInternal(ProgressCallback* progress, Error* error, s64 startSize, s64 finalSize)
	{
		m_file_cache_size = FileSystem::FSize64(m_file);
		if (m_file_cache_size <= 0)
		{
			Error::SetStringView(error, "Failed to determine file size.");
			return false;
		}

		// Copy the current file position.
		m_file_cache_pos = FileSystem::FTell64(m_file);
		if (m_file_cache_pos <= 0)
		{
			Error::SetStringView(error, "Failed to determine file position.");
			return false;
		}

		m_file_cache = std::make_unique_for_overwrite<u8[]>(m_file_cache_size);
		if (FileSystem::FSeek64(m_file, 0, SEEK_SET) != 0 ||
			FileSystem::ReadFileWithPartialProgress(
				m_file, m_file_cache.get(), m_file_cache_size, progress,
				(startSize * 100) / finalSize,
				((startSize + m_file_cache_size) * 100) / finalSize,
				error) != static_cast<size_t>(m_file_cache_size))
		{
			m_file_cache.reset();
			// Precache failed, continue using file
			// Restore file position incase it's used for subsequent reads
			FileSystem::FSeek64(m_file, m_file_cache_pos, SEEK_SET);
			Error::SetStringView(error, "Failed to read part of the file.");
			return false;
		}

		startSize += m_file_cache_size;

		if (m_parent)
		{
			if (!m_parent->PrecacheInternal(progress, error, startSize, finalSize))
			{
				// Precache failed, continue using file
				// Restore file position incase it's used for subsequent reads
				FileSystem::FSeek64(m_file, m_file_cache_pos, SEEK_SET);
				m_file_cache.reset();
				return false;
			}
		}

		if (m_free_file)
			std::fclose(m_file);
		m_file = nullptr;

		return true;
	}

	static u64 FSize(core_file* file)
	{
		ChdCoreFileWrapper* fileWrapper = FromCoreFile(file);
		if (fileWrapper->m_file_cache)
			return fileWrapper->m_file_cache_size;
		else
			return static_cast<u64>(FileSystem::FSize64(fileWrapper->m_file));
	}

	static size_t FRead(void* buffer, size_t elmSize, size_t elmCount, core_file* file)
	{
		ChdCoreFileWrapper* fileWrapper = FromCoreFile(file);
		if (fileWrapper->m_file_cache)
		{
			// While currently libchdr only uses an elmCount of 1, we can't guarantee that will always be the case.
			elmCount = std::min<size_t>(elmCount, std::max<s64>(fileWrapper->m_file_cache_size - fileWrapper->m_file_cache_pos, 0) / elmSize);
			const size_t size = elmSize * elmCount;
			std::memcpy(buffer, &fileWrapper->m_file_cache[fileWrapper->m_file_cache_pos], size);
			return elmCount;
		}
		else
			return std::fread(buffer, elmSize, elmCount, fileWrapper->m_file);
	}

	static int FClose(core_file* file)
	{
		// Destructor handles freeing the FILE handle.
		delete FromCoreFile(file);
		return 0;
	}

	static int FSeek(core_file* file, int64_t offset, int whence)
	{
		ChdCoreFileWrapper* fileWrapper = FromCoreFile(file);
		if (fileWrapper->m_file_cache)
		{
			switch (whence)
			{
				case SEEK_SET:
					fileWrapper->m_file_cache_pos = offset;
					break;
				case SEEK_CUR:
					fileWrapper->m_file_cache_pos += offset;
					break;
				case SEEK_END:
					fileWrapper->m_file_cache_pos = fileWrapper->m_file_cache_size + offset;
					break;
				default:
					return -1;
			}

			return 0;
		}
		else
			return FileSystem::FSeek64(fileWrapper->m_file, offset, whence);
	}
};

ChdFileReader::ChdFileReader() = default;

ChdFileReader::~ChdFileReader()
{
	pxAssert(!m_chd);
}

static bool IsHeaderParentCHD(const chd_header& header, const chd_header& parent_header)
{
	static const u8 nullmd5[CHD_MD5_BYTES]{};
	static const u8 nullsha1[CHD_SHA1_BYTES]{};

	// Check MD5 if it isn't empty.
	if (std::memcmp(nullmd5, header.parentmd5, CHD_MD5_BYTES) != 0 &&
		std::memcmp(nullmd5, parent_header.md5, CHD_MD5_BYTES) != 0 &&
		std::memcmp(parent_header.md5, header.parentmd5, CHD_MD5_BYTES) != 0)
	{
		return false;
	}

	// Check SHA1 if it isn't empty.
	if (std::memcmp(nullsha1, header.parentsha1, CHD_SHA1_BYTES) != 0 &&
		std::memcmp(nullsha1, parent_header.sha1, CHD_SHA1_BYTES) != 0 &&
		std::memcmp(parent_header.sha1, header.parentsha1, CHD_SHA1_BYTES) != 0)
	{
		return false;
	}

	return true;
}

static chd_file* OpenCHD(const std::string& filename, FileSystem::ManagedCFilePtr fp, Error* error, u32 recursion_level)
{
	chd_file* chd;
	ChdCoreFileWrapper* core_wrapper = new ChdCoreFileWrapper(fp.get(), nullptr);
	// libchdr will take ownership of core_wrapper, and will close/free it on failure.
	chd_error err = chd_open_core_file(core_wrapper->GetCoreFile(), CHD_OPEN_READ, nullptr, &chd);
	if (err == CHDERR_NONE)
	{
		// core_wrapper should manage fp.
		core_wrapper->SetFileOwner(true);
		fp.release();
		return chd;
	}
	else if (err != CHDERR_REQUIRES_PARENT)
	{
		Console.Error(fmt::format("Failed to open CHD '{}': {}", filename, chd_error_string(err)));
		Error::SetString(error, chd_error_string(err));
		return nullptr;
	}

	if (recursion_level >= MAX_PARENTS)
	{
		Console.Error(fmt::format("Failed to open CHD '{}': Too many parent files", filename));
		Error::SetString(error, "Too many parent files");
		return nullptr;
	}

	// Need to get the sha1 to look for.
	chd_header header;
	err = chd_read_header_file(fp.get(), &header);
	if (err != CHDERR_NONE)
	{
		Console.Error(fmt::format("Failed to read CHD header '{}': {}", filename, chd_error_string(err)));
		Error::SetString(error, chd_error_string(err));
		return nullptr;
	}

	// Find a chd with a matching sha1 in the same directory.
	// Have to do *.* and filter on the extension manually because Linux is case sensitive.
	chd_file* parent_chd = nullptr;
	const std::string parent_dir(Path::GetDirectory(filename));
	const std::unique_lock hash_cache_lock(s_chd_hash_cache_mutex);

	// Memoize which hashes came from what files, to avoid reading them repeatedly.
	for (auto it = s_chd_hash_cache.begin(); it != s_chd_hash_cache.end(); ++it)
	{
		if (!StringUtil::compareNoCase(parent_dir, Path::GetDirectory(it->first)))
			continue;

		if (!IsHeaderParentCHD(header, it->second))
			continue;

		// Re-check the header, it might have changed since we last opened.
		chd_header parent_header;
		auto parent_fp = FileSystem::OpenManagedSharedCFile(it->first.c_str(), "rb", FileSystem::FileShareMode::DenyWrite);
		if (parent_fp && chd_read_header_file(parent_fp.get(), &parent_header) == CHDERR_NONE &&
			IsHeaderParentCHD(header, parent_header))
		{
			// Need to take a copy of the string, because the parent might add to the list and invalidate the iterator.
			const std::string filename_to_open = it->first;

			// Match! Open this one.
			parent_chd = OpenCHD(filename_to_open, std::move(parent_fp), error, recursion_level + 1);
			if (parent_chd)
			{
				Console.WriteLn(
					fmt::format("Using parent CHD '{}' from cache for '{}'.", Path::GetFileName(filename_to_open), Path::GetFileName(filename)));
			}
		}

		// No point checking any others. Since we recursively call OpenCHD(), the iterator is invalidated anyway.
		break;
	}
	if (!parent_chd)
	{
		// Look for files in the same directory as the chd.
		FileSystem::FindResultsArray parent_files;
		FileSystem::FindFiles(
			parent_dir.c_str(), "*.*", FILESYSTEM_FIND_FILES | FILESYSTEM_FIND_HIDDEN_FILES | FILESYSTEM_FIND_KEEP_ARRAY, &parent_files);
		for (FILESYSTEM_FIND_DATA& fd : parent_files)
		{
			if (!StringUtil::EndsWithNoCase(Path::GetExtension(fd.FileName), "chd"))
				continue;

			// Re-check the header, it might have changed since we last opened.
			chd_header parent_header;
			auto parent_fp = FileSystem::OpenManagedSharedCFile(fd.FileName.c_str(), "rb", FileSystem::FileShareMode::DenyWrite);
			if (!parent_fp || chd_read_header_file(parent_fp.get(), &parent_header) != CHDERR_NONE)
				continue;

			// Don't duplicate in the cache. But update it, in case the file changed.
			auto cache_it = std::find_if(s_chd_hash_cache.begin(), s_chd_hash_cache.end(), [&fd](const auto& it) { return it.first == fd.FileName; });
			if (cache_it != s_chd_hash_cache.end())
				std::memcpy(&cache_it->second, &parent_header, sizeof(parent_header));
			else
				s_chd_hash_cache.emplace_back(fd.FileName, parent_header);

			if (!IsHeaderParentCHD(header, parent_header))
				continue;

			// Match! Open this one.
			parent_chd = OpenCHD(fd.FileName, std::move(parent_fp), error, recursion_level + 1);
			if (parent_chd)
			{
				Console.WriteLn(fmt::format("Using parent CHD '{}' for '{}'.", Path::GetFileName(fd.FileName), Path::GetFileName(filename)));
				break;
			}
		}
	}
	if (!parent_chd)
	{
		Console.Error(fmt::format("Failed to open CHD '{}': Failed to find parent CHD, it must be in the same directory.", filename));
		Error::SetString(error, "Failed to find parent CHD, it must be in the same directory.");
		return nullptr;
	}

	// Our last core file wrapper got freed, so make a new one.
	core_wrapper = new ChdCoreFileWrapper(fp.get(), ChdCoreFileWrapper::FromCoreFile(chd_core_file(parent_chd)));
	// Now try re-opening with the parent.
	err = chd_open_core_file(core_wrapper->GetCoreFile(), CHD_OPEN_READ, parent_chd, &chd);
	if (err != CHDERR_NONE)
	{
		Console.Error(fmt::format("Failed to open CHD '{}': {}", filename, chd_error_string(err)));
		Error::SetString(error, chd_error_string(err));
		return nullptr;
	}

	// core_wrapper should manage fp.
	core_wrapper->SetFileOwner(true);
	fp.release();
	return chd;
}

bool ChdFileReader::Open2(std::string filename, Error* error)
{
	Close2();

	m_filename = std::move(filename);

	auto fp = FileSystem::OpenManagedSharedCFile(m_filename.c_str(), "rb", FileSystem::FileShareMode::DenyWrite, error);
	if (!fp)
		return false;

	m_chd = OpenCHD(m_filename, std::move(fp), error, 0);
	if (!m_chd)
		return false;

	const chd_header* header = chd_get_header(m_chd);
	m_hunk_size = header->hunkbytes;
	// CHD likes to use full 2448 byte blocks, but keeps the +24 offset of source ISOs
	// The rest of PCSX2 likes to use 2448 byte buffers, which can't fit that so trim blocks instead
	m_internalBlockSize = header->unitbytes;

	m_size = static_cast<u64>(header->unitbytes) * header->unitcount;
	if (!ParseTOC(error))
	{
		Close2();
		return false;
	}

	if (m_tracks.empty())
		Console.Warning("Failed to parse CHD TOC, file size may be incorrect.");

	return true;
}

bool ChdFileReader::Precache2(ProgressCallback* progress, Error* error)
{
	ChdCoreFileWrapper* wrapper = ChdCoreFileWrapper::FromCoreFile(chd_core_file(m_chd));
	if (!CheckAvailableMemoryForPrecaching(wrapper->GetPrecacheSize(), error))
		return false;

	return wrapper->Precache(progress, error);
}

ThreadedFileReader::Chunk ChdFileReader::ChunkForOffset(const u64 offset)
{
	if (offset >= m_size)
		return {-1, 0, 0};

	if (m_chunk_map.empty())
	{
		const s64 id = offset / m_hunk_size;
		return {id, static_cast<u64>(id) * m_hunk_size, m_hunk_size};
	}

	auto chunk = std::upper_bound(m_chunk_map.begin(), m_chunk_map.end(), offset,
		[](const u64 value, const ChunkMap& chunk) { return value < chunk.offset; });
	--chunk;

	return {static_cast<s64>(chunk - m_chunk_map.begin()), chunk->offset, chunk->length};
}

int ChdFileReader::ReadChunk(void* dst, const s64 id)
{
	if (id < 0)
		return -1;

	const ChunkMap direct{0, static_cast<u64>(id) * m_hunk_size, m_hunk_size, false};
	const ChunkMap& chunk = m_chunk_map.empty() ? direct : m_chunk_map[static_cast<size_t>(id)];
	const u32 hunk_offset = chunk.chd_offset % m_hunk_size;
	void* const target = (hunk_offset || chunk.length != m_hunk_size) ? m_hunk_buffer.get() : dst;
	const chd_error error = chd_read(m_chd, static_cast<u32>(chunk.chd_offset / m_hunk_size), target);
	if (error != CHDERR_NONE)
	{
		Console.Error("CDVD: chd_read returned error: %s", chd_error_string(error));
		return 0;
	}

	if (target != dst)
		std::memcpy(dst, m_hunk_buffer.get() + hunk_offset, chunk.length);
	if (chunk.audio)
		SwapAudioBytes(dst, chunk.length);
	return chunk.length;
}

void ChdFileReader::Close2()
{
	if (m_chd)
		chd_close(m_chd);
	m_chd = nullptr;
	m_hunk_buffer.reset();
	m_chunk_map.clear();
	m_tracks.clear();
	m_size = 0;
	m_hunk_size = 0;
}

u32 ChdFileReader::GetBlockCount() const
{
	return (m_size > m_dataoffset) ? static_cast<u32>((m_size - m_dataoffset) / m_internalBlockSize) : 0;
}

void ChdFileReader::MapTrack(u64 disc_offset, u64 chd_offset, const u32 frames, const bool audio)
{
	const u64 disc_end = disc_offset + (static_cast<u64>(frames) * CD_FRAME_SIZE);
	while (disc_offset < disc_end)
	{
		const u32 length =
			static_cast<u32>(std::min<u64>(disc_end - disc_offset, m_hunk_size - (chd_offset % m_hunk_size)));
		m_chunk_map.push_back({disc_offset, chd_offset, length, audio});
		disc_offset += length;
		chd_offset += length;
	}
}

bool ChdFileReader::AddTrack(
	const char* metadata, const bool v2, u64& disc_frame, u64& chd_frame, Error* error)
{
	TrackMetadata track;
	if (!ParseTrackMetadata(metadata, v2, track))
		return Fail(error, "Invalid CHD track metadata: '{}'", metadata);

	if (track.number != static_cast<int>(m_tracks.size()) + 1 || track.number > CD_MAX_TRACKS || track.frames <= 0 ||
		track.pregap < 0 || track.pregap > track.frames || track.postgap < 0)
		return Fail(error, "CHD track {} has invalid numbering or frame counts", track.number);

	if (track.HasUnsupportedGaps())
		return Fail(error, "CHD track {} uses unsupported non-virtual pregap or postgap data", track.number);

	const u8 type = GetTrackType(track.type);
	if (!type)
		return Fail(error, "CHD track {} has unsupported type '{}'", track.number, track.type);

	const u32 frames = static_cast<u32>(track.frames);
	const u64 padded_frames = (frames + CD_TRACK_PADDING - 1) & ~(CD_TRACK_PADDING - 1);
	if (disc_frame + frames > std::numeric_limits<u32>::max() ||
		chd_frame + padded_frames > chd_get_header(m_chd)->unitcount)
		return Fail(error, "CHD track metadata exceeds the image size");

	m_tracks.push_back({static_cast<u8>(track.number), type, static_cast<u32>(disc_frame),
		static_cast<u32>(disc_frame + track.pregap)});
	MapTrack(disc_frame * CD_FRAME_SIZE, chd_frame * CD_FRAME_SIZE, frames, type == CDVD_AUDIO_TRACK);
	disc_frame += frames;
	chd_frame += padded_frames;
	return true;
}

bool ChdFileReader::ReadTracks(char* metadata, const u32 tag, const bool v2, Error* error)
{
	u64 disc_frame = 0;
	u64 chd_frame = 0;
	chd_error err = CHDERR_NONE;
	for (u32 index = 0; err == CHDERR_NONE; index++)
	{
		if (!AddTrack(metadata, v2, disc_frame, chd_frame, error))
			return false;
		err = chd_get_metadata(m_chd, tag, index + 1, metadata, METADATA_SIZE, nullptr, nullptr, nullptr);
	}
	if (err != CHDERR_METADATA_NOT_FOUND)
		return Fail(error, "Failed to read CHD track metadata: {}", chd_error_string(err));

	m_size = disc_frame * CD_FRAME_SIZE;
	m_hunk_buffer = std::make_unique_for_overwrite<u8[]>(m_hunk_size);
	return true;
}

bool ChdFileReader::ParseTOC(Error* error)
{
	char metadata[METADATA_SIZE]{};
	chd_error err = chd_get_metadata(
		m_chd, CDROM_TRACK_METADATA2_TAG, 0, metadata, sizeof(metadata), nullptr, nullptr, nullptr);
	const bool v2 = err != CHDERR_METADATA_NOT_FOUND;
	if (!v2)
		err = chd_get_metadata(
			m_chd, CDROM_TRACK_METADATA_TAG, 0, metadata, sizeof(metadata), nullptr, nullptr, nullptr);
	if (err == CHDERR_METADATA_NOT_FOUND)
		return true;
	if (err != CHDERR_NONE)
		return Fail(error, "Failed to read CHD track metadata: {}", chd_error_string(err));

	if (m_internalBlockSize != CD_FRAME_SIZE || !m_hunk_size || m_hunk_size % CD_FRAME_SIZE)
		return Fail(error, "CHD has invalid CD frame or hunk size ({} and {})", m_internalBlockSize, m_hunk_size);

	const u32 tag = v2 ? CDROM_TRACK_METADATA2_TAG : CDROM_TRACK_METADATA_TAG;
	return ReadTracks(metadata, tag, v2, error);
}
