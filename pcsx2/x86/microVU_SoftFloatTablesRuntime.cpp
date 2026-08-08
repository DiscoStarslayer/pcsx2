// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "microVU_SoftFloatTables.h"

#include "common/AlignedMalloc.h"
#include "common/Assertions.h"
#include "common/BitUtils.h"
#include "common/HostSys.h"

#include <cstring>
#include <memory>
#include <zstd.h>

#if defined(__linux__)
#include <sys/mman.h>
#endif

namespace MicroVUSoftFloatTables
{
	const std::uint64_t* sqrt_correction_lookup = nullptr;
	const std::uint8_t* first_one_correction_lookup = nullptr;
	const std::uint64_t* reciprocal_correction_lookup = nullptr;

	namespace
	{
		constexpr std::size_t LARGE_PAGE_SIZE = 2 * 1024 * 1024;
		constexpr std::size_t SQRT_LOOKUP_SIZE = sizeof(SqrtCorrectionTable);
		constexpr std::size_t SQRT_RESERVATION_SIZE = SQRT_LOOKUP_SIZE + LARGE_PAGE_SIZE;
		constexpr std::size_t FIRST_ONE_LOOKUP_SIZE = sizeof(FirstOneCorrectionTable);
		constexpr std::size_t RECIPROCAL_LOOKUP_SIZE = sizeof(ReciprocalCorrectionTable);
		constexpr std::size_t RECIPROCAL_STORAGE_SIZE = LARGE_PAGE_SIZE;

		static_assert(SQRT_LOOKUP_SIZE == LARGE_PAGE_SIZE);
		static_assert(RECIPROCAL_LOOKUP_SIZE == 1024 * 1024);

		class RuntimeLookupStorage
		{
		public:
			RuntimeLookupStorage()
			{
				m_sqrt_area = SharedMemoryMappingArea::Create(SQRT_RESERVATION_SIZE);
				if (m_sqrt_area)
				{
					const uptr aligned_address = Common::AlignUpPow2(
						reinterpret_cast<uptr>(m_sqrt_area->BasePointer()), LARGE_PAGE_SIZE);
					m_sqrt_base = m_sqrt_area->Map(nullptr, 0, reinterpret_cast<void*>(aligned_address),
						SQRT_LOOKUP_SIZE, PageAccess_ReadWrite());
					if (!m_sqrt_base)
						m_sqrt_area.reset();
				}

				if (!m_sqrt_base)
					m_sqrt_base = static_cast<u8*>(_aligned_malloc(SQRT_LOOKUP_SIZE, LARGE_PAGE_SIZE));
				pxAssertRel(m_sqrt_base, "Failed to allocate microVU SQRT correction-table storage");

				m_first_one_base = static_cast<u8*>(
					_aligned_malloc(FIRST_ONE_LOOKUP_SIZE, HostSys::GetRuntimePageSize()));
				pxAssertRel(m_first_one_base, "Failed to allocate microVU first-one correction-table storage");

				m_reciprocal_area = SharedMemoryMappingArea::Create(RECIPROCAL_STORAGE_SIZE + LARGE_PAGE_SIZE);
				if (m_reciprocal_area)
				{
					const uptr aligned_address = Common::AlignUpPow2(
						reinterpret_cast<uptr>(m_reciprocal_area->BasePointer()), LARGE_PAGE_SIZE);
					m_reciprocal_base = m_reciprocal_area->Map(nullptr, 0,
						reinterpret_cast<void*>(aligned_address), RECIPROCAL_STORAGE_SIZE,
						PageAccess_ReadWrite());
					if (!m_reciprocal_base)
						m_reciprocal_area.reset();
				}
				if (!m_reciprocal_base)
					m_reciprocal_base = static_cast<u8*>(
						_aligned_malloc(RECIPROCAL_STORAGE_SIZE, LARGE_PAGE_SIZE));
				pxAssertRel(m_reciprocal_base,
					"Failed to allocate microVU reciprocal correction-table storage");

#if defined(__linux__) && defined(MADV_HUGEPAGE)
				// Back the hot, randomly accessed lookup tables with transparent huge pages when possible.
				madvise(m_sqrt_base, SQRT_LOOKUP_SIZE, MADV_HUGEPAGE);
				std::memset(m_reciprocal_base, 0, RECIPROCAL_STORAGE_SIZE);
				madvise(m_reciprocal_base, RECIPROCAL_STORAGE_SIZE, MADV_HUGEPAGE);
#endif

				Decompress(m_sqrt_base, SQRT_LOOKUP_SIZE,
					compressed_sqrt_correction_table, compressed_sqrt_correction_table_size);
				Decompress(m_first_one_base, FIRST_ONE_LOOKUP_SIZE,
					compressed_first_one_correction_table, compressed_first_one_correction_table_size);
				Decompress(m_reciprocal_base, RECIPROCAL_LOOKUP_SIZE,
					compressed_reciprocal_correction_table, compressed_reciprocal_correction_table_size);

				HostSys::MemProtect(m_sqrt_base, SQRT_LOOKUP_SIZE, PageAccess_ReadOnly());
				HostSys::MemProtect(m_first_one_base, FIRST_ONE_LOOKUP_SIZE, PageAccess_ReadOnly());
				HostSys::MemProtect(m_reciprocal_base, RECIPROCAL_STORAGE_SIZE, PageAccess_ReadOnly());

				sqrt_correction_lookup = reinterpret_cast<const std::uint64_t*>(m_sqrt_base);
				first_one_correction_lookup = m_first_one_base;
				reciprocal_correction_lookup =
					reinterpret_cast<const std::uint64_t*>(m_reciprocal_base);
			}

			~RuntimeLookupStorage()
			{
				if (m_sqrt_area)
					m_sqrt_area->Unmap(m_sqrt_base, SQRT_LOOKUP_SIZE, false);
				else if (m_sqrt_base)
				{
					HostSys::MemProtect(m_sqrt_base, SQRT_LOOKUP_SIZE, PageAccess_ReadWrite());
					_aligned_free(m_sqrt_base);
				}

				HostSys::MemProtect(m_first_one_base, FIRST_ONE_LOOKUP_SIZE, PageAccess_ReadWrite());
				_aligned_free(m_first_one_base);

				if (m_reciprocal_area)
					m_reciprocal_area->Unmap(m_reciprocal_base, RECIPROCAL_STORAGE_SIZE, false);
				else if (m_reciprocal_base)
				{
					HostSys::MemProtect(m_reciprocal_base, RECIPROCAL_STORAGE_SIZE,
						PageAccess_ReadWrite());
					_aligned_free(m_reciprocal_base);
				}
			}

		private:
			static void Decompress(void* output, std::size_t output_size,
				const void* input, std::size_t input_size)
			{
				const std::size_t decompressed_size =
					ZSTD_decompress(output, output_size, input, input_size);
				if (ZSTD_isError(decompressed_size))
					pxFailRel(ZSTD_getErrorName(decompressed_size));
				pxAssertRel(decompressed_size == output_size,
					"Unexpected microVU correction-table size");
			}

			std::unique_ptr<SharedMemoryMappingArea> m_sqrt_area;
			u8* m_sqrt_base = nullptr;
			u8* m_first_one_base = nullptr;
			std::unique_ptr<SharedMemoryMappingArea> m_reciprocal_area;
			u8* m_reciprocal_base = nullptr;
		};
	} // namespace

	void InitializeCorrectionTables()
	{
		static const RuntimeLookupStorage storage;
	}
} // namespace MicroVUSoftFloatTables
