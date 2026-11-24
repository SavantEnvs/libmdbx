/* Crafted database file -> mdbx_env_open(RDONLY|VALIDATION) -> mdbx_env_copy with
 * MDBX_CP_COMPACT (the mdbx_copy -c path: compacting tree walker + meta rewrite).
 * The first input byte selects copy flags (bit 2: MDBX_CP_FORCE_DYNAMIC_SIZE);
 * the rest of the input is the database file.
 * (mdbx_env_open_for_recovery is not used: it always opens without MDBX_NOSUBDIR
 * and without MDBX_VALIDATION, which upstream requires for untrusted files.) */
#include "fuzz_common.h"

int LLVMFuzzerInitialize(int *argc, char ***argv) {
  (void)argc;
  (void)argv;
  fz_quiet_logs();
  return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (size < 1)
    return -1;
  const uint8_t sel = data[0];
  if (fz_write_db(data + 1, size - 1))
    return 0;
  MDBX_env *env = NULL;
  if (mdbx_env_create(&env) == MDBX_SUCCESS) {
    mdbx_env_set_maxdbs(env, 64);
    int rc = mdbx_env_open(env, fz_db, MDBX_NOSUBDIR | MDBX_RDONLY | MDBX_EXCLUSIVE | MDBX_VALIDATION, 0);
    if (rc == MDBX_SUCCESS) {
      MDBX_copy_flags_t cf = MDBX_CP_COMPACT;
      if (sel & 4)
        cf |= MDBX_CP_FORCE_DYNAMIC_SIZE;
      mdbx_env_copy(env, fz_aux, cf);
    }
    mdbx_env_close_ex(env, true);
  }
  fz_cleanup();
  return 0;
}
