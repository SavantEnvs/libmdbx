/* File-input driver for the mdbx_load CLI (mdbx_load.c, compiled with
 * -Dmain=mdbx_load_main). mdbx_load calls exit() on every malformed dump, so
 * this is an AFL target that forks per input rather than a libFuzzer one.
 * Usage: mdbx_load_afl <dump-file>
 * Runs `mdbx_load -n -f <dump-file> <fresh scratch db>`; the scratch db
 * (and its -lck) is removed before and after, so each run starts empty. */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int mdbx_load_main(int argc, char *argv[]);

static char db[4096], lck[4200];

static void cleanup(void) {
  unlink(db);
  unlink(lck);
}

int main(int argc, char *argv[]) {
  if (argc != 2) {
    fprintf(stderr, "usage: %s <dump-file>\n", argv[0]);
    return 2;
  }
  const char *t = getenv("TMPDIR");
  if (!t || !*t)
    t = "/tmp";
  snprintf(db, sizeof(db), "%s/mdbx-load-%ld.db", t, (long)getpid());
  snprintf(lck, sizeof(lck), "%s-lck", db);
  cleanup();
  atexit(cleanup);
  char *args[] = {"mdbx_load", "-q", "-n", "-f", argv[1], db, NULL};
  return mdbx_load_main(6, args);
}
