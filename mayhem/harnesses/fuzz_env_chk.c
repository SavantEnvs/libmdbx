/* Crafted database file -> mdbx_env_open(RDONLY|VALIDATION) -> mdbx_env_chk.
 * Same API sequence as the mdbx_chk tool (mdbx_chk.c): meta/geometry
 * validation in the open path, then the full B-tree + GC walk of the checker. */
#include "fuzz_common.h"

int LLVMFuzzerInitialize(int *argc, char ***argv) {
  (void)argc;
  (void)argv;
  fz_quiet_logs();
  return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (fz_write_db(data, size))
    return 0;
  MDBX_env *env = NULL;
  if (mdbx_env_create(&env) == MDBX_SUCCESS) {
    mdbx_env_set_maxdbs(env, 64);
    int rc = mdbx_env_open(env, fz_db, MDBX_NOSUBDIR | MDBX_RDONLY | MDBX_EXCLUSIVE | MDBX_VALIDATION, 0);
    if (rc == MDBX_SUCCESS) {
      MDBX_chk_callbacks_t cb;
      MDBX_chk_context_t ctx;
      memset(&cb, 0, sizeof(cb));
      memset(&ctx, 0, sizeof(ctx));
      mdbx_env_chk(env, &cb, &ctx, MDBX_CHK_DEFAULTS, MDBX_chk_result, 0);
    }
    mdbx_env_close_ex(env, true);
  }
  fz_cleanup();
  return 0;
}
