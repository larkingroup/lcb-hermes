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
static void activity(Chat *c, const char *text) {
    if(!*text || !strcmp(c->activity,text)) return;
    field(c->activity,sizeof(c->activity),text);
    cJSON_AddItemToArray(c->activity_log,cJSON_CreateString(c->activity));
    while(cJSON_GetArraySize(c->activity_log)>40) cJSON_DeleteItemFromArray(c->activity_log,0);
}
static void workspace(Chat *c, const cJSON *info) {
    /* Runtime cwd may be a launch-directory fallback. It is not membership. */
    if(cJSON_HasObjectItem(info,"cwd")) field(c->cwd,sizeof(c->cwd),js(info,"cwd"));
}
void chat_project(Chat *c,const cJSON *p,const cJSON *row) {
    int unfiled=jb(p,"isNoProject");
    field(c->folder,sizeof(c->folder),unfiled?"":js(row,"cwd"));
    field(c->project_id,sizeof(c->project_id),unfiled?"":js(p,"id"));
    field(c->project_name,sizeof(c->project_name),unfiled?"":js(p,"label"));
    c->folder_known=1;
}

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
int chat_matches(const Chat *c,const char *id) { return id&&*id&&(!strcmp(id,c->id)||!strcmp(id,c->stored)||!strcmp(id,c->list_id)||!strcmp(id,c->key)); }
void chat_listing(Chat *c,const cJSON *row,unsigned long long order) {
    const char *id=*js(row,"id")?js(row,"id"):js(row,"session_id"),*resolved=js(row,"resolved_id"); const cJSON *started=cJSON_GetObjectItemCaseSensitive(row,"started_at");
    if(*id) { field(c->list_id,sizeof(c->list_id),id); field(c->stored,sizeof(c->stored),*resolved?resolved:id); }
    if(*js(row,"title")) field(c->title,sizeof(c->title),js(row,"title"));
    if(*js(row,"source")) field(c->source,sizeof(c->source),js(row,"source"));
    if(cJSON_IsNumber(started)&&started->valuedouble>0) c->started_at=started->valuedouble;
    workspace(c,row); c->listed=1; c->order=order;
}
Chat *chat_new(const char *key) {
    Chat *c=calloc(1,sizeof(*c)); if(!c) return NULL;
    field(c->key,sizeof(c->key),key); field(c->title,sizeof(c->title),"new chat");
    c->draft=textdup(""); c->messages=cJSON_CreateArray(); c->requests=cJSON_CreateArray(); c->attachments=cJSON_CreateArray(); c->media=cJSON_CreateObject(); c->stream=-1; c->sequence=-1;
    c->listed=1; c->activity_log=cJSON_CreateArray(); return c;
}
void chat_free(Chat *c) { if(!c) return; free(c->draft); cJSON_Delete(c->messages); cJSON_Delete(c->requests); cJSON_Delete(c->attachments); cJSON_Delete(c->media); cJSON_Delete(c->activity_log); free(c); }
void chat_load(Chat *c, const cJSON *r) {
    const cJSON *info=cJSON_GetObjectItemCaseSensitive(r,"info"), *flight=cJSON_GetObjectItemCaseSensitive(r,"inflight");
    const char *stored=js(info,"stored_session_id");
    if(strcmp(c->id,js(r,"session_id"))) {
        cJSON *a; c->sequence=-1;
        cJSON_ArrayForEach(a,c->attachments) cJSON_DeleteItemFromObjectCaseSensitive(a,"queued");
    }
    field(c->id,sizeof(c->id),js(r,"session_id"));
    if(!*stored) stored=js(r,"stored_session_id");
    if(!*stored) stored=js(r,"session_key");
    if(!*stored && *c->stored) stored=c->stored;
    if(!*stored) stored=c->id;
    if(stored!=c->stored) field(c->stored,sizeof(c->stored),stored);
    if(*js(info,"title")) field(c->title,sizeof(c->title),js(info,"title"));
    field(c->model,sizeof(c->model),js(info,"model"));
    if(*js(info,"provider")) field(c->provider,sizeof(c->provider),js(info,"provider"));
    workspace(c,info);
    cJSON_Delete(c->messages); c->messages=arraycopy(cJSON_GetObjectItemCaseSensitive(r,"messages"));
    { cJSON *row; int ordinal=0; cJSON_ArrayForEach(row,c->messages) if(!strcmp(js(row,"role"),"user")) { char key[40]; const cJSON *a; snprintf(key,sizeof(key),"%d",ordinal++); a=cJSON_GetObjectItemCaseSensitive(c->media,key); if(a) { cJSON_DeleteItemFromObjectCaseSensitive(row,"attachments"); cJSON_AddItemToObject(row,"attachments",cJSON_Duplicate(a,1)); } } }
    cJSON_Delete(c->requests); c->requests=arraycopy(cJSON_GetObjectItemCaseSensitive(r,"open_requests"));
    c->stream=-1; c->running=jb(r,"running") || jb(flight,"streaming"); c->pending=0; c->loaded=1;
    activity(c,c->running?"Hermes is working":"Ready");
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
    if(!chat_matches(c,id)) return 0;
    if(cJSON_IsNumber(seq)) { if(seq->valuedouble<=c->sequence) return 0; c->sequence=seq->valuedouble; }
    if(!strcmp(type,"message.start")) { c->running=1; c->stream=-1; activity(c,"Working"); }
    else if(!strcmp(type,"message.delta")) {
        cJSON *m=stream(c); const char *old=js(m,"text"), *delta=js(p,"text"); size_t a=strlen(old), b=strlen(delta); char *joined;
        if(a+b>WIRE_LIMIT) return 0;
        joined=malloc(a+b+1); if(!joined) return 0;
        memcpy(joined,old,a); memcpy(joined+a,delta,b+1); cJSON_ReplaceItemInObjectCaseSensitive(m,"text",cJSON_CreateString(joined)); free(joined);
        activity(c,"Responding");
    } else if(!strcmp(type,"message.complete")) {
        if(*js(p,"text")) { cJSON *m=stream(c); cJSON_ReplaceItemInObjectCaseSensitive(m,"text",cJSON_CreateString(js(p,"text"))); }
        c->running=0; c->pending=0; c->stream=-1;
        activity(c,*js(p,"error")?js(p,"error"):!strcmp(js(p,"status"),"interrupted")?"Interrupted":"Complete");
    } else if(!strcmp(type,"session.info")) {
        if(*js(p,"title")) field(c->title,sizeof(c->title),js(p,"title"));
        if(*js(p,"model")) field(c->model,sizeof(c->model),js(p,"model"));
        if(*js(p,"provider")) field(c->provider,sizeof(c->provider),js(p,"provider"));
        if(*js(p,"stored_session_id")) field(c->stored,sizeof(c->stored),js(p,"stored_session_id"));
        workspace(c,p);
    } else if(!strcmp(type,"session.title")) {
        if(*js(p,"title")) field(c->title,sizeof(c->title),js(p,"title"));
    } else if(!strcmp(type,"message.interim")) {
        if(*js(p,"text") && !jb(p,"already_streamed")) {
            cJSON *m=cJSON_CreateObject(); cJSON_AddStringToObject(m,"role","assistant"); cJSON_AddStringToObject(m,"text",js(p,"text")); cJSON_AddItemToArray(c->messages,m);
        }
        activity(c,"Working");
        c->stream=-1;
    } else if(!strcmp(type,"thinking.delta") || !strcmp(type,"reasoning.delta") || !strcmp(type,"reasoning.available")) {
        if(!c->running) return 0;
        activity(c,"Thinking");
    } else if(!strcmp(type,"status.update")) {
        activity(c,*js(p,"text")?js(p,"text"):js(p,"kind"));
    } else if(!strcmp(type,"tool.start") || !strcmp(type,"tool.complete") || !strcmp(type,"tool.generating")) {
        char line[512];
        if(!strcmp(type,"tool.complete") && *js(p,"summary")) field(line,sizeof(line),js(p,"summary"));
        else snprintf(line,sizeof(line),"%s %.350s%s%.100s",!strcmp(type,"tool.complete")?"Finished":!strcmp(type,"tool.generating")?"Preparing":"Using",js(p,"name"),*js(p,"context")?": ":"",js(p,"context"));
        activity(c,line);
    } else if(!strcmp(type,"request.cancel")) {
        int i; const cJSON *id=cJSON_GetObjectItemCaseSensitive(p,"id");
        for(i=cJSON_GetArraySize(c->requests)-1;i>=0;i--) if(cJSON_Compare(cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(c->requests,i),"id"),id,1)) cJSON_DeleteItemFromArray(c->requests,i);
    } else return 0;
    return 1;
}
cJSON *chat_json(const Chat *c) {
    cJSON *o=cJSON_CreateObject(); cJSON_AddStringToObject(o,"list_id",c->list_id); cJSON_AddStringToObject(o,"source",c->source); cJSON_AddNumberToObject(o,"started_at",c->started_at); cJSON_AddNumberToObject(o,"order",(double)c->order); cJSON_AddBoolToObject(o,"listed",c->listed); cJSON_AddBoolToObject(o,"completed_unread",c->completed_unread); cJSON_AddStringToObject(o,"model",c->model); cJSON_AddStringToObject(o,"provider",c->provider); cJSON_AddStringToObject(o,"reasoning",c->reasoning); cJSON_AddStringToObject(o,"key",c->key); cJSON_AddStringToObject(o,"stored",c->stored); cJSON_AddStringToObject(o,"title",c->title);
    cJSON_AddStringToObject(o,"cwd",c->cwd); cJSON_AddStringToObject(o,"project_id",c->project_id); cJSON_AddStringToObject(o,"project_name",c->project_name);
    cJSON_AddStringToObject(o,"folder",c->folder); cJSON_AddBoolToObject(o,"folder_known",c->folder_known);
    cJSON_AddStringToObject(o,"draft",c->draft); cJSON_AddItemToObject(o,"attachments",cJSON_Duplicate(c->attachments,1)); cJSON_AddItemToObject(o,"media",cJSON_Duplicate(c->media,1)); return o;
}
Chat *chat_restore(const cJSON *v) {
    Chat *c=chat_new(js(v,"key")); const cJSON *n; if(!c) return NULL;
    field(c->list_id,sizeof(c->list_id),js(v,"list_id")); field(c->source,sizeof(c->source),js(v,"source"));
    n=cJSON_GetObjectItemCaseSensitive(v,"started_at"); if(cJSON_IsNumber(n)) c->started_at=n->valuedouble;
    n=cJSON_GetObjectItemCaseSensitive(v,"order"); if(cJSON_IsNumber(n)&&n->valuedouble>0) c->order=(unsigned long long)n->valuedouble;
    c->listed=!cJSON_HasObjectItem(v,"listed")||jb(v,"listed"); c->completed_unread=jb(v,"completed_unread"); c->unread=c->completed_unread;
    field(c->model,sizeof(c->model),js(v,"model")); field(c->provider,sizeof(c->provider),js(v,"provider")); field(c->reasoning,sizeof(c->reasoning),js(v,"reasoning"));
    field(c->stored,sizeof(c->stored),js(v,"stored")); field(c->title,sizeof(c->title),js(v,"title")); free(c->draft); c->draft=textdup(js(v,"draft"));
    field(c->cwd,sizeof(c->cwd),js(v,"cwd")); field(c->project_id,sizeof(c->project_id),js(v,"project_id")); field(c->project_name,sizeof(c->project_name),js(v,"project_name"));
    field(c->folder,sizeof(c->folder),js(v,"folder")); c->folder_known=jb(v,"folder_known");
    if(!c->folder_known) c->project_id[0]=c->project_name[0]=0;
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
