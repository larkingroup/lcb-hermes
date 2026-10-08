#include "core.h"
#include "markdown.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

static int frame_count;
static void frame(void *context, cJSON *value) {
    (void)context;
    assert(cJSON_IsObject(value));
    frame_count++;
    cJSON_Delete(value);
}
static wchar_t rendered[256];
static unsigned styles;
static void span(void *context, const wchar_t *text, unsigned style) {
    (void)context;
    assert(wcslen(rendered) + wcslen(text) < 256);
    wcscat(rendered, text);
    styles |= style;
}
static cJSON *parse(const char *text) {
    cJSON *value = cJSON_Parse(text);
    assert(value);
    return value;
}
int main(void) {
    Chat *chat = chat_new("draft");
    cJSON *value, *saved;
    Chat *restored;
    const char wire[] = "{\"type\":\"one\"}\n {\"type\":\"two\"}\r\n";
    assert(chat);
    assert(frames_parse(wire, sizeof(wire)-1, frame, NULL) == 2);
    assert(frame_count == 2);
    assert(frames_parse("{} trailing", 11, frame, NULL) == -1);
    assert(frames_parse("{}\0", 3, frame, NULL) == -1);
    value = parse("{\"session_id\":\"runtime\",\"session_key\":\"saved\",\"info\":{\"title\":\"Linux check\"},\"messages\":[]}");
    chat_load(chat, value);
    cJSON_Delete(value);
    assert(chat_matches(chat, "runtime") && chat_matches(chat, "saved"));
    value = parse("{\"session_id\":\"runtime\",\"seq\":1,\"type\":\"message.start\"}");
    assert(chat_event(chat, value));
    cJSON_Delete(value);
    value = parse("{\"session_id\":\"runtime\",\"seq\":2,\"type\":\"message.delta\",\"payload\":{\"text\":\"Hello Linux\"}}");
    assert(chat_event(chat, value));
    assert(!chat_event(chat, value));
    cJSON_Delete(value);
    assert(!strcmp(js(cJSON_GetArrayItem(chat->messages, 0), "text"), "Hello Linux"));
    value = parse("{\"session_id\":\"runtime\",\"seq\":3,\"type\":\"message.complete\",\"payload\":{\"text\":\"Hello Linux!\"}}");
    assert(chat_event(chat, value));
    cJSON_Delete(value);
    assert(!chat->running && chat->stream == -1);
    free(chat->draft);
    chat->draft = textdup("keep this draft");
    saved = chat_json(chat);
    restored = chat_restore(saved);
    assert(restored && !strcmp(restored->stored, "saved"));
    assert(!strcmp(restored->draft, "keep this draft"));
    assert(!strcmp(restored->title, "Linux check"));
    cJSON_Delete(saved);
    chat_free(restored);
    chat_free(chat);
    markdown_render(L"# Heading\n**bold** and `code`", span, NULL);
    assert(wcsstr(rendered, L"Heading") && wcsstr(rendered, L"bold") && wcsstr(rendered, L"code"));
    assert((styles & (MdHeading | MdBold | MdCode)) == (MdHeading | MdBold | MdCode));
    puts("Linux core smoke: frame parsing, identity aliases, streaming, replay deduplication, draft round-trip, Markdown passed.");
    return 0;
}
