#include "core.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int count;
static void receive(void *ctx,cJSON *frame) { (void)ctx; assert(cJSON_IsObject(frame)); count++; cJSON_Delete(frame); }
static cJSON *parse(const char *text) { cJSON *j=cJSON_Parse(text); assert(j); return j; }
int main(void) {
    Chat *a=chat_new("one"),*b=chat_new("two"),*restored; cJSON *e,*r,*saved; char *text;
    assert(a&&b); strcpy(a->id,"runtime-a"); strcpy(a->stored,"stored-a"); strcpy(b->id,"runtime-b");
    e=parse("{\"session_id\":\"runtime-a\",\"seq\":1,\"type\":\"message.delta\",\"payload\":{\"text\":\"hello 🌿\"}}");
    assert(chat_event(a,e)==1); assert(chat_event(a,e)==0); assert(chat_event(b,e)==0); cJSON_Delete(e);
    e=parse("{\"session_id\":\"runtime-a\",\"seq\":2,\"type\":\"message.complete\",\"payload\":{\"text\":\"hello 🌿\"}}");
    assert(chat_event(a,e)); assert(!a->running); assert(cJSON_GetArraySize(a->messages)==1); cJSON_Delete(e);
    text=row_text(cJSON_GetArrayItem(a->messages,0)); assert(!strcmp(text,"hello 🌿")); free(text);
    free(b->draft); b->draft=textdup("keep my draft"); saved=chat_json(b); restored=chat_restore(saved); assert(!strcmp(restored->draft,"keep my draft")); chat_free(restored); cJSON_Delete(saved);
    r=parse("{\"session_id\":\"new-runtime\",\"info\":{\"stored_session_id\":\"stored-a\",\"title\":\"saved chat\",\"model\":\"model\"},\"messages\":[{\"role\":\"assistant\",\"content\":[{\"type\":\"text\",\"text\":\"first\"},{\"type\":\"text\",\"text\":\"second\"}]}],\"running\":true,\"inflight\":{\"assistant\":\"working\",\"streaming\":true},\"open_requests\":[{\"id\":1,\"method\":\"approval\"}]}");
    chat_load(a,r); assert(!strcmp(a->stored,"stored-a")); assert(!strcmp(a->id,"new-runtime")); assert(a->running); assert(cJSON_GetArraySize(a->requests)==1); assert(cJSON_GetArraySize(a->messages)==2); cJSON_Delete(r);
    text=row_text(cJSON_GetArrayItem(a->messages,0)); assert(!strcmp(text,"first\nsecond")); free(text);
    e=parse("{\"session_id\":\"new-runtime\",\"seq\":3,\"type\":\"session.info\",\"payload\":{\"stored_session_id\":\"canonical-id\",\"title\":\"canonical chat\"}}");
    assert(chat_event(a,e)); assert(!strcmp(a->stored,"canonical-id")); saved=chat_json(a); restored=chat_restore(saved); assert(!strcmp(restored->stored,"canonical-id")); chat_free(restored); cJSON_Delete(saved); cJSON_Delete(e);
    e=parse("{\"session_id\":\"new-runtime\",\"seq\":4,\"type\":\"request.cancel\",\"payload\":{\"id\":1}}"); assert(chat_event(a,e)); assert(cJSON_GetArraySize(a->requests)==0); cJSON_Delete(e);
    assert(frames_parse("{\"id\":1}\n{\"id\":2}\n",18,receive,NULL)==2); assert(count==2);
    assert(frames_parse("{\"id\":1}garbage",15,receive,NULL)==-1); assert(frames_parse("[]",2,receive,NULL)==-1); assert(frames_parse("{\"id\":1}\0oops",14,receive,NULL)==-1);
    chat_free(a); chat_free(b); puts("protocol, replay, chat routing, resume and drafts: passed"); return 0;
}
