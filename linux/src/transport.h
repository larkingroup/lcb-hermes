#ifndef HERMES_LINUX_TRANSPORT_H
#define HERMES_LINUX_TRANSPORT_H
#include "core.h"
#include <gio/gio.h>
typedef struct LinuxNet LinuxNet;
enum { LinuxOnline=1, LinuxOffline, LinuxFrame, LinuxRest };
typedef void (*LinuxReceive)(void *context, int kind, int tag, const cJSON *json, const char *error);
LinuxNet *linux_net_new(LinuxReceive receive, void *context);
void linux_net_connect(LinuxNet *net, const char *url, const char *user, const char *password, int private_http, const char *cookie);
int linux_net_send(LinuxNet *net, const cJSON *frame);
void linux_net_rest(LinuxNet *net, const char *path, int tag);
void linux_net_free(LinuxNet *net);
const char *linux_net_cookie(LinuxNet *net);
int linux_private_address(GInetAddress *address);
#endif
