#ifndef HERMES_TRANSPORT_WIN_H
#define HERMES_TRANSPORT_WIN_H
#ifndef UNICODE
#define UNICODE
#endif
#include <windows.h>
#include "core.h"
#define NET_MESSAGE (WM_APP + 20)
enum { NetOnline=1, NetOffline, NetFrame, NetRest };
typedef struct Net Net;
typedef struct NetMessage {
    int kind, tag;
    void *context;
    cJSON *json;
    char error[256], url[1024], user[256], cookie[8192];
} NetMessage;
Net *net_new(HWND target);
void net_connect(Net *net, const char *url, const char *user, const char *password, int private_http, const char *cookie);
int net_send(Net *net, const cJSON *frame);
void net_rest(Net *net, const char *path, int tag, void *context);
int net_post(Net *net, const char *path, const cJSON *body, int tag, void *context);
int net_write(Net *net, const char *method, const char *path, const cJSON *body, int tag, void *context);
void net_upload(Net *net, const wchar_t *file, int tag, void *context);
void net_free(Net *net);
wchar_t *wide(const char *text);
char *utf8(const wchar_t *text);
int endpoint_valid(const char *url, int private_http, char *error, size_t size);
#endif
