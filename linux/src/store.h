#ifndef HERMES_LINUX_STORE_H
#define HERMES_LINUX_STORE_H
#include "core.h"
#include <glib.h>
typedef struct LinuxStore LinuxStore;
LinuxStore *linux_store_new(void);
cJSON *linux_store_read(LinuxStore *store, char **error);
int linux_store_write(LinuxStore *store, const cJSON *value, char **error);
void linux_store_free(LinuxStore *store);
/* Pure codec used by the encrypted store and regression checks. */
GBytes *linux_store_seal(const unsigned char key[32], const void *data, size_t size);
GBytes *linux_store_open(const unsigned char key[32], const void *data, size_t size);
#endif
