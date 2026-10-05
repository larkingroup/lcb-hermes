#include "core.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

const char *js(const cJSON *o, const char *key) {
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    return cJSON_IsString(v) ? v->valuestring : "";
}
int jb(const cJSON *o, const char *key) { return cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(o,key)); }
char *textdup(const char *s) { size_t n=strlen(s)+1; char *p=malloc(n); if(p) memcpy(p,s,n); return p; }
static void field(char *dst, size_t cap, const char *s) { snprintf(dst,cap,"%s",s); }
static cJSON *arraycopy(const cJSON *v) { return cJSON_IsArray(v)?cJSON_Duplicate(v,1):cJSON_CreateArray(); }

char *row_text(const cJSON *row) {
    const cJSON *v=cJSON_GetObjectItemCaseSensitive(row,"text"), *p;
    char *out; size_t n=1, used=0;
    if(!cJSON_IsString(v)) v=cJSON_GetObjectItemCaseSensitive(row,"content");
    if(cJSON_IsString(v)) return textdup(v->valuestring);
    if(!cJSON_IsArray(v)) return textdup("");
    cJSON_ArrayForEach(p,v) n+=strlen(js(p,"text"))+1;
    out=calloc(n,1); if(!out) return NULL;
    cJSON_ArrayForEach(p,v) { const char *s=js(p,"text"); size_t len=strlen(s); if(len) { memcpy(out+used,s,len); used+=len; out[used++]='\n'; } }
    if(used) out[used-1]=0;
    return out;
}
Chat *chat_new(const char *key) {
    Chat *c=calloc(1,sizeof(*c)); if(!c) return NULL;
    field(c->key,sizeof(c->key),key); field(c->title,sizeof(c->title),"new chat");
    c->draft=textdup(""); c->messages=cJSON_CreateArray(); c->requests=cJSON_CreateArray(); c->attachments=cJSON_CreateArray(); c->media=cJSON_CreateObject(); c->stream=-1; c->sequence=-1;
    return c;
}
void chat_free(Chat *c) { if(!c) return; free(c->draft); cJSON_Delete(c->messages); cJSON_Delete(c->requests); cJSON_Delete(c->attachments); cJSON_Delete(c->media); free(c); }
void chat_load(Chat *c, const cJSON *r) {
    const cJSON *info=cJSON_GetObjectItemCaseSensitive(r,"info"), *flight=cJSON_GetObjectItemCaseSensitive(r,"inflight");
    const char *stored=js(info,"stored_session_id");
    field(c->id,sizeof(c->id),js(r,"session_id"));
    if(!*stored) stored=js(r,"stored_session_id"); if(!*stored && *c->stored) stored=c->stored; if(!*stored) stored=c->id;
    if(stored!=c->stored) field(c->stored,sizeof(c->stored),stored);
    if(*js(info,"title")) field(c->title,sizeof(c->title),js(info,"title"));
    field(c->model,sizeof(c->model),js(info,"model"));
    cJSON_Delete(c->messages); c->messages=arraycopy(cJSON_GetObjectItemCaseSensitive(r,"messages"));
    { cJSON *row; int ordinal=0; cJSON_ArrayForEach(row,c->messages) if(!strcmp(js(row,"role"),"user")) { char key[40]; const cJSON *a; snprintf(key,sizeof(key),"%d",ordinal++); a=cJSON_GetObjectItemCaseSensitive(c->media,key); if(a) { cJSON_DeleteItemFromObjectCaseSensitive(row,"attachments"); cJSON_AddItemToObject(row,"attachments",cJSON_Duplicate(a,1)); } } }
    cJSON_Delete(c->requests); c->requests=arraycopy(cJSON_GetObjectItemCaseSensitive(r,"open_requests"));
    c->stream=-1; c->running=jb(r,"running") || jb(flight,"streaming"); c->pending=0; c->loaded=1;
    if(*js(flight,"assistant")) {
        cJSON *m=cJSON_CreateObject(); cJSON_AddStringToObject(m,"role","assistant"); cJSON_AddStringToObject(m,"text",js(flight,"assistant"));
        c->stream=cJSON_GetArraySize(c->messages); cJSON_AddItemToArray(c->messages,m);
    }
}
static cJSON *stream(Chat *c) {
    cJSON *m;
    if(c->stream>=0) return cJSON_GetArrayItem(c->messages,c->stream);
    m=cJSON_CreateObject(); cJSON_AddStringToObject(m,"role","assistant"); cJSON_AddStringToObject(m,"text","");
    c->stream=cJSON_GetArraySize(c->messages); cJSON_AddItemToArray(c->messages,m); return m;
}
int chat_event(Chat *c, const cJSON *e) {
    const char *id=js(e,"session_id"), *type=js(e,"type"); const cJSON *p=cJSON_GetObjectItemCaseSensitive(e,"payload"), *seq=cJSON_GetObjectItemCaseSensitive(e,"seq");
    if(!*id || (strcmp(id,c->id)&&strcmp(id,c->stored))) return 0;
    if(cJSON_IsNumber(seq)) { if(seq->valuedouble<=c->sequence) return 0; c->sequence=seq->valuedouble; }
    if(!strcmp(type,"message.start")) { c->running=1; c->stream=-1; }
    else if(!strcmp(type,"message.delta")) {
        cJSON *m=stream(c); const char *old=js(m,"text"), *delta=js(p,"text"); size_t a=strlen(old), b=strlen(delta); char *joined;
        if(a+b>WIRE_LIMIT) return 0; joined=malloc(a+b+1); if(!joined) return 0;
        memcpy(joined,old,a); memcpy(joined+a,delta,b+1); cJSON_ReplaceItemInObjectCaseSensitive(m,"text",cJSON_CreateString(joined)); free(joined);
    } else if(!strcmp(type,"message.complete")) {
        if(*js(p,"text")) { cJSON *m=stream(c); cJSON_ReplaceItemInObjectCaseSensitive(m,"text",cJSON_CreateString(js(p,"text"))); }
        c->running=0; c->pending=0; c->stream=-1;
    } else if(!strcmp(type,"session.info")) {
        if(*js(p,"title")) field(c->title,sizeof(c->title),js(p,"title"));
        if(*js(p,"model")) field(c->model,sizeof(c->model),js(p,"model"));
        if(*js(p,"stored_session_id")) field(c->stored,sizeof(c->stored),js(p,"stored_session_id"));
    } else if(!strcmp(type,"session.title")) {
        if(*js(p,"title")) field(c->title,sizeof(c->title),js(p,"title"));
    } else if(!strcmp(type,"message.interim")) {
        c->stream=-1;
    } else if(!strcmp(type,"request.cancel")) {
        int i; const cJSON *id=cJSON_GetObjectItemCaseSensitive(p,"id");
        for(i=cJSON_GetArraySize(c->requests)-1;i>=0;i--) if(cJSON_Compare(cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(c->requests,i),"id"),id,1)) cJSON_DeleteItemFromArray(c->requests,i);
    } else return 0;
    return 1;
}
cJSON *chat_json(const Chat *c) {
    cJSON *o=cJSON_CreateObject(); cJSON_AddStringToObject(o,"key",c->key); cJSON_AddStringToObject(o,"stored",c->stored); cJSON_AddStringToObject(o,"title",c->title);
    cJSON_AddStringToObject(o,"draft",c->draft); cJSON_AddItemToObject(o,"attachments",cJSON_Duplicate(c->attachments,1)); cJSON_AddItemToObject(o,"media",cJSON_Duplicate(c->media,1)); return o;
}
Chat *chat_restore(const cJSON *v) {
    Chat *c=chat_new(js(v,"key")); if(!c) return NULL;
    field(c->stored,sizeof(c->stored),js(v,"stored")); field(c->title,sizeof(c->title),js(v,"title")); free(c->draft); c->draft=textdup(js(v,"draft"));
    cJSON_Delete(c->attachments); c->attachments=arraycopy(cJSON_GetObjectItemCaseSensitive(v,"attachments"));
    if(cJSON_IsObject(cJSON_GetObjectItemCaseSensitive(v,"media"))) { cJSON_Delete(c->media); c->media=cJSON_Duplicate(cJSON_GetObjectItemCaseSensitive(v,"media"),1); } return c;
}
int frames_parse(const char *wire, size_t size, FrameEmit emit, void *context) {
    size_t begin=0, i; int count=0;
    if(size>WIRE_LIMIT || memchr(wire,0,size)) return -1;
    for(i=0;i<=size;i++) if(i==size || wire[i]=='\n') {
        size_t n=i-begin; cJSON *frame; const char *end=NULL;
        while(n && (wire[begin]==' '||wire[begin]=='\r'||wire[begin]=='\t')) { begin++; n--; }
        if(n) {
            frame=cJSON_ParseWithLengthOpts(wire+begin,n,&end,0);
            if(!frame || !cJSON_IsObject(frame)) { cJSON_Delete(frame); return -1; }
            while(end<wire+begin+n && (*end==' '||*end=='\t'||*end=='\r')) end++;
            if(end!=wire+begin+n) { cJSON_Delete(frame); return -1; }
            emit(context,frame); count++;
        }
        begin=i+1;
    }
    return count;
}
