#ifndef HERMES_CORE_H
#define HERMES_CORE_H
#include <stddef.h>
#include "cJSON.h"

#define WIRE_LIMIT (8u * 1024u * 1024u)
typedef struct Chat {
    char key[65], id[160], stored[160], title[256], model[160];
    char *draft;
    cJSON *messages, *requests, *attachments, *media;
    int running, pending, unread, loaded, stream;
    double sequence;
} Chat;
const char *js(const cJSON *object, const char *key);
int jb(const cJSON *object, const char *key);
char *textdup(const char *text);
char *row_text(const cJSON *row);
Chat *chat_new(const char *key);
void chat_free(Chat *chat);
void chat_load(Chat *chat, const cJSON *result);
int chat_event(Chat *chat, const cJSON *event);
cJSON *chat_json(const Chat *chat);
Chat *chat_restore(const cJSON *value);
typedef void (*FrameEmit)(void *context, cJSON *frame);
int frames_parse(const char *wire, size_t size, FrameEmit emit, void *context);
#endif
