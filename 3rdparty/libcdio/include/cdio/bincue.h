/*
  PCSX2 downstream extensions for the libcdio BIN/CUE image driver.

  This file is distributed under the GNU General Public License version 3
  or later, consistent with libcdio.
*/

#ifndef CDIO_BINCUE_H_
#define CDIO_BINCUE_H_

#include <cdio/cdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Return the backing filename for a BIN/CUE track, or NULL on error. */
const char *cdio_bincue_get_track_filename(const CdIo_t *p_cdio,
                                           track_t i_track);

/** Read complete 2352-byte sectors without stripping data-track headers. */
driver_return_code_t cdio_bincue_read_raw_sectors(const CdIo_t *p_cdio,
                                                   void *p_buf, lsn_t i_lsn,
                                                   uint32_t i_blocks);

#ifdef __cplusplus
}
#endif

#endif /* CDIO_BINCUE_H_ */

