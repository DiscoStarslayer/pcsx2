/*
  Copyright (C) 2003, 2006-2009, 2011, 2017, 2025
   Rocky Bernstein <rocky@gnu.org>

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
/* Tests ISO9660 library routines. */
#ifdef HAVE_CONFIG_H
#include "config.h"
#define __CDIO_CONFIG_H__ 1
#endif

#include <ctype.h>
#include <stddef.h>
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#ifdef HAVE_STRING_H
#include <string.h>
#endif
#ifdef HAVE_STDIO_H
#include <stdio.h>
#endif

#include <cdio/cdio.h>
#include <cdio/iso9660.h>
#include <cdio/bytesex.h>

#define ISO_XA_FILE "./data/xa.iso"
#define ISO_XA_FILE_ENTRY_FOO "FOO.;1"
#define ISO_XA_FILE_ENTRY_FOO_ATTR "----1xrxrx-"
#define ISO_XA_FILE_ENTRY_FOO_MODE 0551

static int
test_oversized_directory_size(void)
{
  const char *image_name = "bad-directory-size.bin";
  const char *cue_name = "bad-directory-size.cue";
  const uint8_t oversized_size[8] =
    { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
  FILE *source;
  FILE *image;
  FILE *cue;
  uint8_t buffer[4096];
  size_t nread;
  long int root_size_offset;
  CdIo_t *cdio;
  CdioISO9660FileList_t *entries;
  int result = 1;

  source = fopen("./data/isofs-m1.bin", "rb");
  image = fopen(image_name, "wb");
  if (!source || !image) {
    printf("Could not create crafted ISO image\n");
    goto out;
  }

  while ((nread = fread(buffer, 1, sizeof(buffer), source)) != 0) {
    if (fwrite(buffer, 1, nread, image) != nread) {
      printf("Could not write crafted ISO image\n");
      goto out;
    }
  }
  if (ferror(source)) {
    printf("Could not read ISO fixture\n");
    goto out;
  }
  fclose(source);
  source = NULL;

  root_size_offset = ISO_PVD_SECTOR * CDIO_CD_FRAMESIZE_RAW
    + CDIO_CD_SYNC_SIZE + CDIO_CD_HEADER_SIZE
    + offsetof(iso9660_pvd_t, root_directory_record)
    + offsetof(iso9660_dir_t, size);
  if (fseek(image, root_size_offset, SEEK_SET) != 0
      || fwrite(oversized_size, 1, sizeof(oversized_size), image)
         != sizeof(oversized_size)) {
    printf("Could not set crafted directory size\n");
    goto out;
  }
  fclose(image);
  image = NULL;

  cue = fopen(cue_name, "w");
  if (!cue) {
    printf("Could not create crafted CUE image\n");
    goto out;
  }
  fputs("TRACK 01 MODE1/2352\n"
        "FILE \"bad-directory-size.bin\" BINARY\n"
        "INDEX 01 00:00:00\n", cue);
  fclose(cue);
  cue = NULL;

  cdio = cdio_open(cue_name, DRIVER_BINCUE);
  if (!cdio) {
    printf("Could not open crafted CUE image\n");
    goto out;
  }
  entries = iso9660_fs_readdir(cdio, "/");
  cdio_destroy(cdio);
  if (entries) {
    printf("Incorrectly listed crafted oversized directory\n");
    iso9660_dirlist_free(entries);
    goto out;
  }

  result = 0;
out:
  if (source)
    fclose(source);
  if (image)
    fclose(image);
  if (cue)
    fclose(cue);
  remove(image_name);
  remove(cue_name);
  return result;
}


static bool
time_compare(struct tm *p_tm1, struct tm *p_tm2)
{
  bool okay = true;
  if (!p_tm1) {
    printf("get time is NULL\n");
    return false;
  }
  if (!p_tm2) {
    printf("set time is NULL\n");
    return false;
  }
  if (p_tm1->tm_year != p_tm2->tm_year) {
    printf("Years aren't equal. get: %d, set %d\n",
	   p_tm1->tm_year, p_tm2->tm_year);
    okay=false;
  }
  if (p_tm1->tm_mon != p_tm2->tm_mon) {
    printf("Months aren't equal. get: %d, set %d\n",
	   p_tm1->tm_mon, p_tm2->tm_mon);
    okay=false;
  }
  if (p_tm1->tm_mday != p_tm2->tm_mday) {
    printf("Month days aren't equal. get: %d, set %d\n",
	   p_tm1->tm_mday, p_tm2->tm_mday);
    okay=false;
  }
  if (p_tm1->tm_min != p_tm2->tm_min) {
    printf("minutes aren't equal. get: %d, set %d\n",
	   p_tm1->tm_min, p_tm2->tm_min);
    okay=false;
  }
  if (p_tm1->tm_hour != p_tm2->tm_hour) {
    printf("hours aren't equal. get: %d, set %d\n",
	   p_tm1->tm_hour, p_tm2->tm_hour);
    okay=false;
  }
  if (p_tm1->tm_sec != p_tm2->tm_sec) {
    printf("seconds aren't equal. get: %d, set %d\n",
	   p_tm1->tm_sec, p_tm2->tm_sec);
    okay=false;
  }
  if (p_tm1->tm_wday != p_tm2->tm_wday) {
    printf("Week days aren't equal. get: %d, set %d\n",
	   p_tm1->tm_wday, p_tm2->tm_wday);
    okay=false;
  }
  if (p_tm1->tm_yday != p_tm2->tm_yday) {
    printf("Year days aren't equal. get: %d, set %d\n",
	   p_tm1->tm_yday, p_tm2->tm_yday);
    okay=false;
  }
#ifdef FIXED
  if (p_tm1->tm_isdst != p_tm2->tm_isdst) {
    printf("Is daylight savings times aren't equal. get: %d, set %d\n",
	   p_tm1->tm_isdst, p_tm2->tm_isdst);
    okay=false;
  }
#endif
#ifdef HAVE_TM_GMTOFF
  if (p_tm1->tm_gmtoff != p_tm2->tm_gmtoff) {
    printf("GMT offsets aren't equal. get: %ld, set %ld\n",
	   p_tm1->tm_gmtoff, p_tm2->tm_gmtoff);
    okay=false;
  }
  if (p_tm1 != p_tm2 && p_tm1 && p_tm2) {
#ifdef FIXED
    if (strcmp(p_tm1->tm_zone, p_tm2->tm_zone) != 0) {
      printf("Time Zone values. get: %s, set %s\n",
	     p_tm1->tm_zone, p_tm2->tm_zone);
      /* Argh... sometimes GMT is converted to UTC. So
	 Let's not call this a failure if everything else was okay.
       */
    }
#endif
  }
#endif
  return okay;
}


int
main (int argc, const char *argv[])
{
  int c;
  int i;
  int i_bad = 0;
  char dst[100];
  char *dst_p;
  int achars[] = {'!', '"', '%', '&', '(', ')', '*', '+', ',', '-', '.',
                  '/', '?', '<', '=', '>'};

  /*********************************************
   * Test ACHAR and DCHAR
   *********************************************/

  for (c='A'; c<='Z'; c++ ) {
    if (!iso9660_is_dchar(c)) {
      printf("Failed iso9660_is_dchar test on %c\n", c);
      i_bad++;
    }
    if (!iso9660_is_achar(c)) {
      printf("Failed iso9660_is_achar test on %c\n", c);
      i_bad++;
    }
  }

  if (i_bad) return i_bad;

  for (c='0'; c<='9'; c++ ) {
    if (!iso9660_is_dchar(c)) {
      printf("Failed iso9660_is_dchar test on %c\n", c);
      i_bad++;
    }
    if (!iso9660_is_achar(c)) {
      printf("Failed iso9660_is_achar test on %c\n", c);
      i_bad++;
    }
  }

  if (i_bad) return i_bad;

  for (i=0; i<=13; i++ ) {
    c=achars[i];
    if (iso9660_is_dchar(c)) {
      printf("Should not pass iso9660_is_dchar test on %c\n", c);
      i_bad++;
    }
    if (!iso9660_is_achar(c)) {
      printf("Failed iso9660_is_achar test on symbol %c\n", c);
      i_bad++;
    }
  }

  if (i_bad) return i_bad;

  /*********************************************
   * Test iso9660_strncpy_pad
   *********************************************/

  iso9660_strncpy_pad(dst, "1_3", 5, ISO9660_DCHARS);
  if ( 0 != strncmp(dst, "1_3  ", 5) ) {
    printf("Failed iso9660_strncpy_pad DCHARS\n");
    return 31;
  }
  iso9660_strncpy_pad(dst, "ABC!123", 2, ISO9660_ACHARS);
  if ( 0 != strncmp(dst, "AB", 2) ) {
    printf("Failed iso9660_strncpy_pad ACHARS truncation\n");
    return 32;
  }

  /*********************************************
   * Test iso9660_dirname_valid_p
   *********************************************/

  if ( iso9660_dirname_valid_p("/NOGOOD") ) {
    printf("/NOGOOD should fail iso9660_dirname_valid_p\n");
    return 33;
  }
  if ( iso9660_dirname_valid_p("LONGDIRECTORY/NOGOOD") ) {
    printf("LONGDIRECTORY/NOGOOD should fail iso9660_dirname_valid_p\n");
    return 34;
  }
  if ( !iso9660_dirname_valid_p("OKAY/DIR") ) {
    printf("OKAY/DIR should pass iso9660_dirname_valid_p\n");
    return 35;
  }
  if ( iso9660_dirname_valid_p("OKAY/FILE.EXT") ) {
    printf("OKAY/FILENAME.EXT should fail iso9660_dirname_valid_p\n");
    return 36;
  }

  /*********************************************
   * Test iso9660_pathname_valid_p
   *********************************************/

  if ( !iso9660_pathname_valid_p("OKAY/FILE.EXT") ) {
    printf("OKAY/FILE.EXT should pass iso9660_dirname_valid_p\n");
    return 37;
  }
  if ( iso9660_pathname_valid_p("OKAY/FILENAMETOOLONG.EXT") ) {
    printf("OKAY/FILENAMETOOLONG.EXT should fail iso9660_dirname_valid_p\n");
    return 38;
  }
  if ( iso9660_pathname_valid_p("OKAY/FILE.LONGEXT") ) {
    printf("OKAY/FILE.LONGEXT should fail iso9660_dirname_valid_p\n");
    return 39;
  }

  dst_p = iso9660_pathname_isofy ("this/file.ext", 1);
  if ( 0 != strncmp(dst_p, "this/file.ext;1", 16) ) {
    printf("Failed iso9660_pathname_isofy\n");
    free(dst_p);
    return 40;
  }
  free(dst_p);

  /*********************************************
   * Test get/set date
   *********************************************/

  {
    struct tm *p_tm, tm;
    iso9660_dtime_t dtime;
    time_t now = time(NULL);

    memset(&dtime, 0, sizeof(dtime));
    p_tm = localtime(&now);
    iso9660_set_dtime(p_tm, &dtime);
    iso9660_get_dtime(&dtime, true, &tm);

    p_tm = gmtime(&now);
    iso9660_set_dtime_with_timezone(p_tm, 0, &dtime);
    if (!iso9660_get_dtime(&dtime, false, &tm)) {
      printf("Error returned by iso9660_get_dtime_with_timezone\n");
      return 41;
    }

    if ( !time_compare(p_tm, &tm) ) {
      printf("GMT time retrieved with iso9660_get_dtime_with_timezone() not same as that\n");
      printf("set with iso9660_set_dtime().\n");
      return 42;
    }

#ifdef HAVE_TM_GMTOFF
    if ( !time_compare(p_tm, &tm) ) {
      return 43;
    }
    p_tm = gmtime(&now);
    iso9660_set_dtime(p_tm, &dtime);
    if (!iso9660_get_dtime(&dtime, false, &tm)) {
      printf("Error returned by iso9660_get_dtime\n");
      return 44;
    }

    if ( !time_compare(p_tm, &tm) ) {
      printf("GMT time retrieved with iso9660_get_dtime() not same as that\n");
      printf("set with iso9660_set_dtime().\n");
      return 45;
    }

    {
      iso9660_ltime_t ltime;
      p_tm = localtime(&now);
      iso9660_set_ltime(p_tm, &ltime);

      if (!iso9660_get_ltime(&ltime, &tm)) {
	printf("Problem running iso9660_get_ltime\n");
	return 46;
      }

      if ( ! time_compare(p_tm, &tm) ) {
	printf("local time retrieved with iso9660_get_ltime() not\n");
	printf("same as that set with iso9660_set_ltime().\n");
	return 47;
      }

      p_tm = gmtime(&now);
      iso9660_set_ltime(p_tm, &ltime);
      iso9660_get_ltime(&ltime, &tm);
      if ( ! time_compare(p_tm, &tm) ) {
	printf("GMT time retrieved with iso9660_get_ltime() not\n");
	printf("same as that set with iso9660_set_ltime().\n");
	return 48;
      }
    }
#endif

#if defined(HAVE_TM_GMTOFF) && !defined(_WIN32)
    /*
      Both the VS and MingW Windows environments are excluded because the POSIX
      environment that MingW provides does not include setenv() or unsetenv().
     */
    {
      /*
        Tests for extreme timezones motivated by this New Zealand timezone bug:
        https://github.com/libcdio/libcdio/issues/18

        Note that under POSIX the TZ environment variable uses positive offsets
        for west and negative offsets for east which differs from most other
        conventions such as that followed by the tm_gmtoff member in the
        tm struct.
       */
      iso9660_ltime_t ltime;

      /* save environment variable TZ if set */
      char *saved_env_tz = NULL;
      const char *env_tz = getenv("TZ");
      if (env_tz) {
        const size_t tz_len = strlen(env_tz);
        saved_env_tz = malloc(tz_len + 1);
        memcpy(saved_env_tz, env_tz, tz_len);
      }

      /*
        Farthest east timezone that ISO 9660 can encode, daylight
        savings time in mainland islands of New Zealand
       */
      setenv("TZ", "NZDT-13", 1);
      p_tm = localtime(&now);
      iso9660_set_ltime(p_tm, &ltime);
      iso9660_get_ltime(&ltime, &tm);
      if ( ! time_compare(p_tm, &tm) ) {
        printf("NZDT/GMT+13 time retrieved with iso9660_get_ltime() not\n");
        printf("same as that set with iso9660_set_ltime().\n");
        return 49;
      }

      /* Farthest west timezone that ISO 9660 can encode */
      setenv("TZ", "BIT+12", 1);
      p_tm = localtime(&now);
      iso9660_set_ltime(p_tm, &ltime);
      iso9660_get_ltime(&ltime, &tm);
      if ( ! time_compare(p_tm, &tm) ) {
        printf("BIT/GMT-12 time retrieved with iso9660_get_ltime() not\n");
        printf("same as that set with iso9660_set_ltime().\n");
        return 50;
      }

      printf("Timezone offset warnings are expected to follow.\n");

      /*
        TZ offset -14 exceeds the farthest east timezone that ISO 9660 can
        encode and should fail to be encoded
       */
      setenv("TZ", "FAKE-14", 1);
      p_tm = localtime(&now);
      iso9660_set_ltime(p_tm, &ltime);
      iso9660_get_ltime(&ltime, &tm);
      if ( time_compare(p_tm, &tm) ) {
        printf("GMT+14 timezone successfully retrieved with\n");
        printf("iso9660_get_ltime() but should have failed.\n");
        return 51;
      }

      /*
        TZ offset +13 exceeds the farthest west timezone that ISO 9660 can
        encode and should fail to be encoded
       */
      setenv("TZ", "FAKE+13", 1);
      p_tm = localtime(&now);
      iso9660_set_ltime(p_tm, &ltime);
      iso9660_get_ltime(&ltime, &tm);
      if ( time_compare(p_tm, &tm) ) {
        printf("GMT-13 timezone successfully retrieved with\n");
        printf("iso9660_get_ltime() but should have failed.\n");
        return 52;
      }

      /* restore TZ in case a test is added after this */
      if (saved_env_tz)
        setenv("TZ", saved_env_tz, 1);
      else
        unsetenv("TZ");
      free(saved_env_tz);
    }
#endif
  }

  /*********************************************
   * Test XA file attributes
   *********************************************/
  {
    CdioISO9660FileList_t *entries;
    CdioListNode_t *entry;
    iso9660_t *iso;
    posix_mode_t mode;
    bool found = false;
    iso9660_stat_t *stat;
    const char *xa_attr;

    iso = iso9660_open(ISO_XA_FILE);
    entries = iso9660_ifs_readdir(iso, "/");

    _CDIO_LIST_FOREACH(entry, entries)
      {
        stat = _cdio_list_node_data(entry);
        printf("file: %s \n", stat->filename);
        if (0 != strcmp(stat->filename, ISO_XA_FILE_ENTRY_FOO))
          continue;

        found = true;

        xa_attr = iso9660_get_xa_attr_str(stat->xa.attributes);
        if (0 != strcmp(xa_attr, ISO_XA_FILE_ENTRY_FOO_ATTR))
          {
            printf("got incorrect attr from iso9660_get_xa_attr_str() \n");
            printf("Expected: %s \n", ISO_XA_FILE_ENTRY_FOO_ATTR);
            printf("Got: %s \n", xa_attr);
            return 53;
          }

        mode = iso9660_get_posix_filemode_from_xa(uint16_from_be(stat->xa.attributes));
        if (ISO_XA_FILE_ENTRY_FOO_MODE != mode)
          {
            printf("got incorrect mode from iso9660_get_posix_filemode_from_xa() \n");
            printf("Expected: %o \n", ISO_XA_FILE_ENTRY_FOO_MODE);
            printf("Got: %o \n", mode);
            return 54;
          }
      }

    if (!found)
      {
        printf("could not find entry file %s in %s",
               ISO_XA_FILE_ENTRY_FOO,
               ISO_XA_FILE);
        return 55;
      }

    iso9660_filelist_free(entries);
    iso9660_close(iso);
  }

  if (test_oversized_directory_size())
    return 56;

  return 0;
}
