/* Crafted database file -> mdbx_env_open read-write -> write txn on the main
 * table (put/overwrite/delete a few fixed records, incl. a large value that
 * needs overflow pages), commit, then a read-back walk. Reaches the RW open
 * path of dxb_setup (meta rollback/sync), GC reclaim, page alloc/split/merge. */
#include "fuzz_common.h"

int LLVMFuzzerInitialize(int *argc, char ***argv) {
  (void)argc;
  (void)argv;
  fz_quiet_logs();
  return 0;
}

static uint8_t big[9000];

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (fz_write_db(data, size))
    return 0;
  MDBX_env *env = NULL;
  if (mdbx_env_create(&env) == MDBX_SUCCESS) {
    mdbx_env_set_maxdbs(env, 64);
    if (mdbx_env_open(env, fz_db, MDBX_NOSUBDIR | MDBX_EXCLUSIVE | MDBX_SAFE_NOSYNC | MDBX_NOMETASYNC | MDBX_VALIDATION, 0644) ==
        MDBX_SUCCESS) {
      MDBX_txn *txn = NULL;
      if (mdbx_txn_begin(env, NULL, MDBX_TXN_READWRITE, &txn) == MDBX_SUCCESS) {
        MDBX_dbi dbi;
        if (mdbx_dbi_open(txn, NULL, MDBX_DB_ACCEDE, &dbi) == MDBX_SUCCESS) {
          char kb[16], vb[32];
          for (int i = 0; i < 24; ++i) {
            MDBX_val k = {kb, (size_t)snprintf(kb, sizeof(kb), "key%03d", i * 7 % 24)};
            MDBX_val v = {vb, (size_t)snprintf(vb, sizeof(vb), "value-%d-%d", i, i * i)};
            mdbx_put(txn, dbi, &k, &v, MDBX_UPSERT);
          }
          memset(big, 'B', sizeof(big));
          MDBX_val bk = {(void *)"bigkey", 6}, bv = {big, sizeof(big)};
          mdbx_put(txn, dbi, &bk, &bv, MDBX_UPSERT);
          for (int i = 0; i < 24; i += 3) {
            MDBX_val k = {kb, (size_t)snprintf(kb, sizeof(kb), "key%03d", i)};
            mdbx_del(txn, dbi, &k, NULL);
          }
          fz_walk(txn, dbi, 4096);
        }
        mdbx_txn_commit(txn);
      }
      MDBX_txn *rtxn = NULL;
      if (mdbx_txn_begin(env, NULL, MDBX_TXN_RDONLY, &rtxn) == MDBX_SUCCESS) {
        MDBX_dbi rdbi;
        if (mdbx_dbi_open(rtxn, NULL, MDBX_DB_ACCEDE, &rdbi) == MDBX_SUCCESS)
          fz_walk(rtxn, rdbi, 4096);
        mdbx_txn_abort(rtxn);
      }
    }
    mdbx_env_close_ex(env, true);
  }
  fz_cleanup();
  return 0;
}
