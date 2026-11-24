/* Shared helpers for the libmdbx file-backed harnesses.
 * libmdbx only opens databases by path (it mmaps the file), so each input is
 * written to a per-process scratch file under $TMPDIR, opened with MDBX_NOSUBDIR
 * (lock file = "<path>-lck"), and both files are removed afterwards, so each
 * input's result depends only on that input. */
#ifndef MDBX_FUZZ_COMMON_H
#define MDBX_FUZZ_COMMON_H
#include "mdbx.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static char fz_db[4096], fz_lck[4200], fz_aux[4200], fz_auxlck[4300];

static void fz_paths(void) {
  if (fz_db[0])
    return;
  const char *t = getenv("TMPDIR");
  if (!t || !*t)
    t = "/tmp";
  snprintf(fz_db, sizeof(fz_db), "%s/mdbx-fuzz-%ld.db", t, (long)getpid());
  snprintf(fz_lck, sizeof(fz_lck), "%s-lck", fz_db);
  snprintf(fz_aux, sizeof(fz_aux), "%s.copy", fz_db);
  snprintf(fz_auxlck, sizeof(fz_auxlck), "%s-lck", fz_aux);
}

static void fz_cleanup(void) {
  unlink(fz_db);
  unlink(fz_lck);
  unlink(fz_aux);
  unlink(fz_auxlck);
}

/* Write the input to the scratch db path. Returns 0 on success. */
static int fz_write_db(const uint8_t *data, size_t size) {
  fz_paths();
  fz_cleanup();
  FILE *f = fopen(fz_db, "wb");
  if (!f)
    return -1;
  if (size && fwrite(data, 1, size, f) != size) {
    fclose(f);
    return -1;
  }
  fclose(f);
  return 0;
}

static void fz_quiet_logs(void) {
  /* Only lowers the log verbosity once, never changed by an input. */
  mdbx_setup_debug(MDBX_LOG_FATAL, MDBX_DBG_DONTCHANGE, MDBX_LOGGER_DONTCHANGE);
}

/* Touch every byte of a value so ASan sees reads of mapped page content. */
static volatile uint8_t fz_sink;
static void fz_touch(const MDBX_val *v) {
  const uint8_t *p = (const uint8_t *)v->iov_base;
  uint8_t acc = 0;
  for (size_t i = 0; p && i < v->iov_len; ++i)
    acc ^= p[i];
  fz_sink ^= acc;
}

/* Walk a table with a cursor, FIRST..NEXT, touching keys and values. */
static void fz_walk(MDBX_txn *txn, MDBX_dbi dbi, size_t limit) {
  MDBX_cursor *cur = NULL;
  if (mdbx_cursor_open(txn, dbi, &cur) != MDBX_SUCCESS)
    return;
  MDBX_val k, v;
  int rc = mdbx_cursor_get(cur, &k, &v, MDBX_FIRST);
  for (size_t n = 0; rc == MDBX_SUCCESS && n < limit; ++n) {
    fz_touch(&k);
    fz_touch(&v);
    rc = mdbx_cursor_get(cur, &k, &v, MDBX_NEXT);
  }
  rc = mdbx_cursor_get(cur, &k, &v, MDBX_LAST);
  for (size_t n = 0; rc == MDBX_SUCCESS && n < 64; ++n) {
    fz_touch(&k);
    rc = mdbx_cursor_get(cur, &k, &v, MDBX_PREV);
  }
  mdbx_cursor_close(cur);
}
#endif
