/* Crafted database file -> mdbx_env_open(RDONLY|VALIDATION) -> read txn:
 * env stat/info, mdbx_enumerate_tables, and a cursor walk of the main table
 * and every named table (the mdbx_dump / mdbx_stat read path). */
#include "fuzz_common.h"

int LLVMFuzzerInitialize(int *argc, char ***argv) {
  (void)argc;
  (void)argv;
  fz_quiet_logs();
  return 0;
}

static int on_table(void *ctx, const MDBX_txn *txn, const MDBX_val *name, MDBX_db_flags_t flags,
                    const struct MDBX_stat *stat, MDBX_dbi dbi) {
  (void)ctx;
  (void)name;
  (void)flags;
  (void)stat;
  fz_walk((MDBX_txn *)txn, dbi, 4096);
  return MDBX_SUCCESS;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (fz_write_db(data, size))
    return 0;
  MDBX_env *env = NULL;
  if (mdbx_env_create(&env) == MDBX_SUCCESS) {
    mdbx_env_set_maxdbs(env, 64);
    if (mdbx_env_open(env, fz_db, MDBX_NOSUBDIR | MDBX_RDONLY | MDBX_EXCLUSIVE | MDBX_VALIDATION, 0) == MDBX_SUCCESS) {
      MDBX_envinfo info;
      MDBX_stat st;
      mdbx_env_info_ex(env, NULL, &info, sizeof(info));
      mdbx_env_stat_ex(env, NULL, &st, sizeof(st));
      MDBX_txn *txn = NULL;
      if (mdbx_txn_begin(env, NULL, MDBX_TXN_RDONLY, &txn) == MDBX_SUCCESS) {
        mdbx_env_info_ex(env, txn, &info, sizeof(info));
        MDBX_dbi main_dbi;
        if (mdbx_dbi_open(txn, NULL, MDBX_DB_ACCEDE, &main_dbi) == MDBX_SUCCESS) {
          mdbx_dbi_stat(txn, main_dbi, &st, sizeof(st));
          fz_walk(txn, main_dbi, 4096);
        }
        mdbx_enumerate_tables(txn, on_table, NULL);
        mdbx_txn_abort(txn);
      }
    }
    mdbx_env_close_ex(env, true);
  }
  fz_cleanup();
  return 0;
}
