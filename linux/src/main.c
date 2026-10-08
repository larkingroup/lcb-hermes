#define _POSIX_C_SOURCE 200809L
#include <X11/Intrinsic.h>
#include <X11/StringDefs.h>
#include <X11/Shell.h>
#include <X11/Xaw/AsciiText.h>
#include <X11/Xaw/AsciiSrc.h>
#include <X11/Xaw/Command.h>
#include <X11/Xaw/Label.h>
#include <X11/Xaw/List.h>
#include <X11/Xaw/Paned.h>
#include <X11/Xaw/Box.h>
#include <X11/Xaw/Viewport.h>
#include <X11/Xaw/Toggle.h>
#include <X11/Xaw/MultiSink.h>
#include <glib.h>
#include <openssl/crypto.h>
#include <locale.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include "transport.h"
#include "store.h"

enum { Capabilities=1,List,Resume,Create,Submit,Interrupt,ResumeSend,Models,SetModel,Tree,Config,Stats };
typedef struct Wait { int id,kind; Chat *chat; gint64 at; struct Wait *next; } Wait;
static struct {
    XtAppContext xt; Widget shell,prompt,transcript,list,title,status,activity,monitor,send,request;
    Widget serverwin,url,user,password,http,serverlist,servername,modelwin,model,provider,cwd;
    Widget questionwin,question,answer,allow,modelcatalog;
    LinuxNet *net; LinuxStore *store; cJSON *settings,*profile,*models,*questioncopy,*questions,*answers;
    Chat *chats[1000],*rows[1000],*current; int count,nrows,connected,connecting,nextid,restoring,run,demo,corrupt,questionindex,storage_failed;
    char **labels; char notice[512],epoch[160],default_cwd[1024]; Wait *wait;
    guint render_source,save_source; int stats_pending,list_pending,list_again;
    Pixel face,paper,ink,accent; XFontSet fontset; Atom wm_delete;
} app;
static void render(void),sidebar(void),save(void),submit(void),server_show(void),request_show(void);
static void resume(Chat *c,int sending);
static void set(Widget w,const char *s) {
    if(w) XtVaSetValues(XawTextGetSource(w),XtNstring,s?s:"",NULL);
}
static char *get(Widget w) {
    char *s=NULL; Widget source=XawTextGetSource(w); XtVaGetValues(source,XtNstring,&s,NULL);
    char *copy=g_strdup(s?s:""); XawAsciiSourceFreeString(source); return copy;
}
static void label(Widget w,const char *s) { if(w) XtVaSetValues(w,XtNlabel,s,NULL); }
static void notice(const char *s) { g_strlcpy(app.notice,s,sizeof(app.notice)); label(app.status,app.notice); }
static void put(cJSON *o,const char *key,const char *s) { cJSON_DeleteItemFromObjectCaseSensitive(o,key); cJSON_AddStringToObject(o,key,s); }
static Chat *find(const char *id) { if(id && *id) for(int i=0;i<app.count;i++) if(chat_matches(app.chats[i],id)) return app.chats[i]; return NULL; }
static int add(Chat *c) { if(!c) return 0; if(app.count==1000) { chat_free(c); return 0; } app.chats[app.count++]=c; return 1; }
static void capture_draft(void) {
    if(app.current && !app.restoring) { char *draft=get(app.prompt); free(app.current->draft); app.current->draft=textdup(draft); g_free(draft); }
}
static gboolean saved(gpointer context) { (void)context; app.save_source=0; save(); return G_SOURCE_REMOVE; }
static void schedule_save(void) { if(!app.demo && !app.save_source) app.save_source=g_timeout_add(1000,saved,NULL); }
static void changed(Widget w,XtPointer context,XtPointer call) {
    (void)w; (void)context; (void)call; if(!app.restoring) { capture_draft(); schedule_save(); }
}
static void fresh(void) {
    if(app.modelwin) { XtDestroyWidget(app.modelwin); app.modelwin=NULL; }
    capture_draft(); char *key=g_uuid_string_random(); Chat *c=chat_new(key); g_free(key);
    if(!add(c)) return;
    g_strlcpy(c->cwd,app.default_cwd,sizeof(c->cwd)); app.current=c; sidebar(); render(); schedule_save();
    XtSetKeyboardFocus(app.shell,app.prompt);
}
static void clear_chats(void) {
    if(app.questionwin) { XtDestroyWidget(app.questionwin); app.questionwin=NULL; }
    cJSON_Delete(app.questioncopy); app.questioncopy=NULL; cJSON_Delete(app.questions); app.questions=NULL; cJSON_Delete(app.answers); app.answers=NULL;
    while(app.wait) { Wait *w=app.wait; app.wait=w->next; g_free(w); }
    for(int i=0;i<app.count;i++) chat_free(app.chats[i]);
    app.count=0; app.current=NULL; app.profile=NULL; app.default_cwd[0]=0;
    if(app.modelwin) { XtDestroyWidget(app.modelwin); app.modelwin=NULL; }
    cJSON_Delete(app.models); app.models=NULL;
}
static void coalesce(Chat *keep) {
    if(!keep || !*keep->stored) return;
    for(int i=0;i<app.count;) {
        Chat *other=app.chats[i];
        if(other==keep || !chat_matches(other,keep->stored)) { i++; continue; }
        if(*other->draft && *keep->draft && strcmp(other->draft,keep->draft)) {
            char *key=g_uuid_string_random(); g_strlcpy(other->key,key,sizeof(other->key)); g_free(key);
            other->id[0]=other->stored[0]=other->list_id[0]=0; other->loaded=other->running=other->pending=0;
            i++; continue;
        }
        if(!*keep->draft && *other->draft) { free(keep->draft); keep->draft=textdup(other->draft); }
        keep->unread|=other->unread;
        for(Wait *w=app.wait;w;w=w->next) if(w->chat==other) w->chat=keep;
        if(app.current==other) app.current=keep;
        memmove(app.chats+i,app.chats+i+1,(size_t)(app.count-i-1)*sizeof(*app.chats)); app.count--;
        chat_free(other);
    }
}
static void save(void) {
    if(!app.profile) return;
    capture_draft(); cJSON *chats=cJSON_CreateArray();
    for(int i=0;i<app.count;i++) cJSON_AddItemToArray(chats,chat_json(app.chats[i]));
    cJSON_DeleteItemFromObjectCaseSensitive(app.profile,"chats"); cJSON_AddItemToObject(app.profile,"chats",chats);
    put(app.profile,"active",app.current?app.current->key:"");
    if(app.demo || app.corrupt || app.storage_failed) return;
    char *error=NULL;
    if(!linux_store_write(app.store,app.settings,&error)) { app.storage_failed=1; notice(error?error:"Settings could not be saved; this session remains in memory."); }
    g_free(error);
}
static void profile_load(cJSON *p) {
    clear_chats(); app.profile=p;
    const cJSON *row; cJSON_ArrayForEach(row,cJSON_GetObjectItemCaseSensitive(p,"chats")) add(chat_restore(row));
    app.current=find(js(p,"active")); if(!app.current && app.count) app.current=app.chats[0];
    if(!app.current) fresh();
    sidebar(); render();
}
static int rpc(const char *method,cJSON *params,int kind,Chat *chat) {
    if(!app.connected) { cJSON_Delete(params); notice("Connect to your Hermes server first."); return 0; }
    Wait *w=g_new0(Wait,1); w->id=++app.nextid; w->kind=kind; w->chat=chat; w->at=g_get_monotonic_time();
    cJSON *f=cJSON_CreateObject(); cJSON_AddStringToObject(f,"jsonrpc","2.0"); cJSON_AddNumberToObject(f,"id",w->id);
    cJSON_AddStringToObject(f,"method",method); cJSON_AddItemToObject(f,"params",params);
    int ok=linux_net_send(app.net,f); cJSON_Delete(f);
    if(!ok) { g_free(w); if(chat) chat->pending=0; notice("Connection lost. The request was not retried."); return 0; }
    w->next=app.wait; app.wait=w; return w->id;
}
static cJSON *session(Chat *c) { cJSON *p=cJSON_CreateObject(); cJSON_AddStringToObject(p,"session_id",*c->id?c->id:c->stored); return p; }
static void list_chats(void) {
    if(app.list_pending) { app.list_again=1; return; }
    cJSON *p=cJSON_CreateObject(); cJSON_AddNumberToObject(p,"limit",500); app.list_pending=rpc("session.list",p,List,NULL)!=0;
}
static void resume(Chat *c,int sending) {
    if(!c || !*c->stored || c->pending) return;
    cJSON *p=cJSON_CreateObject(); cJSON_AddStringToObject(p,"session_id",c->stored);
    cJSON_AddStringToObject(p,"source","desktop"); cJSON_AddBoolToObject(p,"close_on_disconnect",0);
    c->pending=1; rpc("session.resume",p,sending?ResumeSend:Resume,c);
}
static void send_prompt(Chat *c) {
    cJSON *p=session(c); cJSON_AddStringToObject(p,"text",c->draft); cJSON_AddStringToObject(p,"surface","desktop");
    c->pending=1; rpc("prompt.submit",p,Submit,c); render();
}
static void submit(void) {
    capture_draft(); Chat *c=app.current;
    if(!c || c->pending || !app.connected) return;
    if(c->running) { rpc("session.interrupt",session(c),Interrupt,c); return; }
    if(!*c->draft) return;
    if(!*c->id && *c->stored) { resume(c,1); return; }
    if(!*c->id) {
        if(!*c->cwd) g_strlcpy(c->cwd,app.default_cwd,sizeof(c->cwd));
        cJSON *p=cJSON_CreateObject(); cJSON_AddStringToObject(p,"source","desktop"); cJSON_AddBoolToObject(p,"close_on_disconnect",0);
        if(*c->cwd) cJSON_AddStringToObject(p,"cwd",c->cwd);
        if(*c->model) cJSON_AddStringToObject(p,"model",c->model);
        if(*c->provider) cJSON_AddStringToObject(p,"provider",c->provider);
        c->pending=1; rpc("session.create",p,Create,c);
    } else send_prompt(c);
    render();
}
static gboolean render_later(gpointer context) { (void)context; app.render_source=0; render(); return G_SOURCE_REMOVE; }
static void queue_render(void) { if(!app.render_source) app.render_source=g_timeout_add(40,render_later,NULL); }
static void render(void) {
    Chat *c=app.current; GString *text=g_string_new("");
    if(c) {
        const cJSON *row;
        cJSON_ArrayForEach(row,c->messages) {
            const char *role=js(row,"role"); char *body=row_text(row);
            g_string_append_printf(text,"%s\n%s\n\n",!strcmp(role,"user")?"You":!strcmp(role,"assistant")?"Hermes":role,body?body:""); free(body);
            if(text->len>WIRE_LIMIT) { g_string_truncate(text,WIRE_LIMIT); break; }
        }
        if(!text->len) g_string_append(text,"A quiet place to work.\n\nChoose a conversation or start a new chat.\nModels and tools stay on your Hermes server.\n");
        label(app.title,c->title);
        GString *log=g_string_new(""); const cJSON *line;
        cJSON_ArrayForEach(line,c->activity_log) if(cJSON_IsString(line)) g_string_append_printf(log,"%s\n",line->valuestring);
        set(app.activity,log->str); g_string_free(log,TRUE);
        app.restoring=1;
        char *old=get(app.prompt); if(strcmp(old,c->draft)) set(app.prompt,c->draft); g_free(old); app.restoring=0;
    }
    /* Avoid resetting the read-only source unless its content changed. */
    char *previous=get(app.transcript);
    if(strcmp(previous,text->str)) {
        XawTextPosition start=0; XtVaGetValues(app.transcript,XtNdisplayPosition,&start,NULL);
        int follow=start==0; set(app.transcript,text->str);
        if(follow || (c && c->running)) XawTextSetInsertionPoint(app.transcript,(XawTextPosition)g_utf8_strlen(text->str,-1));
    }
    g_free(previous); g_string_free(text,TRUE);
    label(app.send,c && c->running?"Stop":"Send");
    XtSetSensitive(app.send,app.connected && c && !c->pending);
    XtSetSensitive(app.prompt,!(c && c->pending));
    XtSetSensitive(app.request,c && cJSON_GetArraySize(c->requests)>0);
    char status[512]; g_snprintf(status,sizeof(status),"%s%s%s",app.connected?"Connected":app.connecting?"Connecting":"Offline",c && *c->activity?"  /  ":"",c?c->activity:"");
    label(app.status,*app.notice?app.notice:status);
}
static void sidebar(void) {
    char **old=app.labels; app.labels=g_new0(char *,1001); app.nrows=0;
    for(int i=0;i<app.count;i++) {
        Chat *c=app.chats[i]; if(*c->stored && !c->listed && !*c->draft && c!=app.current) continue;
        app.rows[app.nrows]=c;
        app.labels[app.nrows++]=g_strdup_printf("%s%s%s",c->unread?"* ":"",!strcmp(c->source,"cron")?"[cron] ":"",c->title);
    }
    if(!app.nrows) { app.labels[0]=g_strdup("No conversations"); XawListChange(app.list,app.labels,1,0,True); }
    else XawListChange(app.list,app.labels,app.nrows,0,True);
    g_strfreev(old);
    for(int i=0;i<app.nrows;i++) if(app.rows[i]==app.current) XawListHighlight(app.list,i);
}
static void selected(Widget w,XtPointer context,XtPointer call) {
    (void)w; (void)context; XawListReturnStruct *row=call;
    if(row->list_index<0 || row->list_index>=app.nrows) return;
    capture_draft();
    if(app.modelwin) { XtDestroyWidget(app.modelwin); app.modelwin=NULL; }
    app.current=app.rows[row->list_index]; app.current->unread=0;
    if(app.connected && !app.current->loaded) resume(app.current,0);
    sidebar(); render(); schedule_save();
}
static void answer_request(cJSON *r,cJSON *value,const char *error) {
    cJSON *f=cJSON_CreateObject(); cJSON_AddStringToObject(f,"jsonrpc","2.0");
    cJSON_AddItemToObject(f,"id",cJSON_Duplicate(cJSON_GetObjectItemCaseSensitive(r,"id"),1));
    if(error) { cJSON *e=cJSON_CreateObject(); cJSON_AddNumberToObject(e,"code",-32000); cJSON_AddStringToObject(e,"message",error); cJSON_AddItemToObject(f,"error",e); cJSON_Delete(value); }
    else cJSON_AddItemToObject(f,"result",value);
    if(linux_net_send(app.net,f)) {
        for(int i=0;i<app.count;i++) for(int j=cJSON_GetArraySize(app.chats[i]->requests)-1;j>=0;j--)
            if(cJSON_Compare(cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(app.chats[i]->requests,j),"id"),cJSON_GetObjectItemCaseSensitive(r,"id"),1)) cJSON_DeleteItemFromArray(app.chats[i]->requests,j);
    }
    cJSON_Delete(f); render();
}
static void received(void *context,int kind,int tag,const cJSON *json,const char *error) {
    (void)context;
    if(kind==LinuxOnline) {
        app.connected=1; app.connecting=0; app.notice[0]=0;
        if(app.profile) put(app.profile,"cookie",linux_net_cookie(app.net));
        cJSON *p=cJSON_CreateObject(); cJSON_AddBoolToObject(p,"server_requests",1); rpc("client.capabilities",p,Capabilities,NULL);
        list_chats();
        if(app.current && *app.current->stored) resume(app.current,0);
        p=cJSON_CreateObject(); cJSON_AddNumberToObject(p,"preview_limit",500); cJSON_AddNumberToObject(p,"session_limit",500); rpc("projects.tree",p,Tree,NULL);
        linux_net_rest(app.net,"/api/config",Config);
        app.stats_pending=1; linux_net_rest(app.net,"/api/system/stats",Stats);
        if(app.serverwin) { XtDestroyWidget(app.serverwin); app.serverwin=NULL; }
        schedule_save(); render(); return;
    }
    if(kind==LinuxOffline) {
        app.connected=app.connecting=app.list_pending=app.list_again=app.stats_pending=0;
        while(app.wait) { Wait *w=app.wait; app.wait=w->next; if(w->chat) w->chat->pending=0; g_free(w); }
        if(app.questionwin) { XtDestroyWidget(app.questionwin); app.questionwin=NULL; }
        for(int i=0;i<app.count;i++) { app.chats[i]->pending=0; app.chats[i]->loaded=0; app.chats[i]->id[0]=0; cJSON_Delete(app.chats[i]->requests); app.chats[i]->requests=cJSON_CreateArray(); }
        notice(error); set(app.monitor,"Offline\n\nServer tasks continue.\nReconnect to resume."); render(); return;
    }
    if(kind==LinuxRest) {
        if(tag==Config && json) g_strlcpy(app.default_cwd,js(cJSON_GetObjectItemCaseSensitive(json,"terminal"),"cwd"),sizeof(app.default_cwd));
        if(tag==Stats) {
            app.stats_pending=0;
            if(json) {
                const cJSON *mem=cJSON_GetObjectItemCaseSensitive(json,"memory"),*cpu=cJSON_GetObjectItemCaseSensitive(json,"cpu_percent"),*rss=cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(json,"process"),"rss");
                const cJSON *used=cJSON_GetObjectItemCaseSensitive(mem,"used"),*total=cJSON_GetObjectItemCaseSensitive(mem,"total");
                char text[1024]; g_snprintf(text,sizeof(text),"HERMES %s\n%s\n\nCPU      %.1f %%\nMemory   %.1f / %.1f GiB\nProcess  %.1f MiB\n\nThese are server readings.",js(json,"hermes_version"),js(json,"hostname"),cpu?cpu->valuedouble:0,(used?used->valuedouble:0)/1073741824.,(total?total->valuedouble:0)/1073741824.,(rss?rss->valuedouble:0)/1048576.); set(app.monitor,text);
            }
        } return;
    }
    const char *method=js(json,"method"); const cJSON *id=cJSON_GetObjectItemCaseSensitive(json,"id");
    if(*method) {
        const cJSON *p=cJSON_GetObjectItemCaseSensitive(json,"params");
        if(!strcmp(method,"event")) {
            const char *type=js(p,"type"); Chat *c=find(js(p,"session_id"));
            if(!strcmp(type,"gateway.ready")) {
                const char *epoch=js(cJSON_GetObjectItemCaseSensitive(p,"payload"),"replay_epoch");
                if(strcmp(epoch,app.epoch)) { g_strlcpy(app.epoch,epoch,sizeof(app.epoch)); for(int i=0;i<app.count;i++) app.chats[i]->sequence=-1; }
            } else if(!strcmp(type,"sessions.changed")) list_chats();
            else if(c && chat_event(c,p)) { if(c!=app.current) c->unread=1; queue_render(); if(!strcmp(type,"message.complete") || !strcmp(type,"session.title")) { sidebar(); schedule_save(); } }
        } else if(id) {
            if(strcmp(method,"approval") && strcmp(method,"clarify")) { answer_request((cJSON *)json,NULL,"This request type is not supported by this client."); return; }
            Chat *c=find(js(p,"session_id"));
            if(!c) answer_request((cJSON *)json,NULL,"This session is not open in this client.");
            else {
                const cJSON *old; int duplicate=0;
                cJSON_ArrayForEach(old,c->requests) if(cJSON_Compare(cJSON_GetObjectItemCaseSensitive(old,"id"),id,1)) duplicate=1;
                if(!duplicate) cJSON_AddItemToArray(c->requests,cJSON_Duplicate(json,1));
                c->unread=c!=app.current; sidebar(); render();
            }
        } return;
    }
    if(!cJSON_IsNumber(id)) return;
    Wait **link=&app.wait;
    while(*link && (*link)->id!=id->valueint) link=&(*link)->next;
    if(!*link) return;
    Wait *w=*link; *link=w->next; Chat *c=w->chat;
    const cJSON *r=cJSON_GetObjectItemCaseSensitive(json,"result"),*e=cJSON_GetObjectItemCaseSensitive(json,"error");
    if(w->kind==List) { app.list_pending=0; }
    if(c) c->pending=0;
    if(e) { if(w->kind==Submit) { notice("Send result uncertain. Draft retained; check history before sending again."); resume(c,0); } else notice(js(e,"message")); }
    else if(w->kind==List) {
        const cJSON *rows=cJSON_GetObjectItemCaseSensitive(r,"sessions"),*row;
        if(cJSON_IsArray(rows)) {
            if(cJSON_GetArraySize(rows)<500) for(int i=0;i<app.count;i++) if(*app.chats[i]->stored) app.chats[i]->listed=0;
            unsigned long long order=(unsigned long long)cJSON_GetArraySize(rows);
            cJSON_ArrayForEach(row,rows) {
                const char *key=*js(row,"id")?js(row,"id"):js(row,"session_id");
                if(!*key) continue;
                Chat *found=find(key); if(!found) found=find(js(row,"resolved_id"));
                if(!found) { found=chat_new(key); if(!add(found)) continue; }
                chat_listing(found,row,order--);
            }
            sidebar(); schedule_save();
        }
    } else if(w->kind==Tree) {
        const cJSON *group,*row;
        cJSON_ArrayForEach(group,cJSON_GetObjectItemCaseSensitive(r,"projects")) cJSON_ArrayForEach(row,cJSON_GetObjectItemCaseSensitive(group,"previewSessions")) {
            Chat *found=find(js(row,"id")); if(found) chat_project(found,group,row);
        }
    } else if((w->kind==Resume || w->kind==ResumeSend || w->kind==Create) && c) {
        char cwd[1024]; g_strlcpy(cwd,c->cwd,sizeof(cwd)); chat_load(c,r); coalesce(c);
        if(w->kind==Create && *cwd && strcmp(cwd,c->cwd)) notice("Hermes chose a different working folder. Review before sending.");
        else if(w->kind!=Resume) send_prompt(c);
        sidebar(); schedule_save();
    } else if(w->kind==Submit && c) {
        cJSON *row=cJSON_CreateObject(); cJSON_AddStringToObject(row,"role","user"); cJSON_AddStringToObject(row,"text",c->draft);
        /* Events may already have started; place the user row before that turn's assistant. */
        if(c->stream>=0) { cJSON_InsertItemInArray(c->messages,c->stream,row); c->stream++; }
        else cJSON_AddItemToArray(c->messages,row);
        free(c->draft); c->draft=textdup(""); c->running=1; app.notice[0]=0; schedule_save();
    } else if(w->kind==Models) {
        cJSON_Delete(app.models); app.models=cJSON_Duplicate(r,1);
        if(app.modelwin) { char *catalog=cJSON_Print(r); set(app.modelcatalog,catalog?catalog:""); free(catalog); }
    } else if(w->kind==SetModel && c) { resume(c,0); }
    int again=w->kind==List && app.list_again; g_free(w);
    if(again) { app.list_again=0; list_chats(); }
    render();
}
static Widget text(Widget parent,const char *name,const char *value,int editable,int height) {
    return XtVaCreateManagedWidget(name,asciiTextWidgetClass,parent,XtNstring,value,XtNinternational,True,
        XtNeditType,editable?XawtextEdit:XawtextRead,XtNwrap,XawtextWrapWord,XtNscrollVertical,XawtextScrollWhenNeeded,
        XtNbackground,app.paper,XtNforeground,app.ink,XtNfontSet,app.fontset,XtNheight,height,XtNmin,editable?28:40,
        XtNdisplayCaret,editable?True:False,XtNleftMargin,8,XtNrightMargin,8,XtNtopMargin,6,XtNbottomMargin,6,NULL);
}
static Widget heading(Widget parent,const char *name,const char *value) {
    return XtVaCreateManagedWidget(name,labelWidgetClass,parent,XtNlabel,value,XtNjustify,XtJustifyLeft,
        XtNheight,28,XtNmin,28,XtNmax,28,XtNshowGrip,False,XtNborderWidth,0,XtNresize,False,NULL);
}
static void button(Widget parent,const char *name,const char *value,XtCallbackProc callback,XtPointer context) {
    Widget w=XtVaCreateManagedWidget(name,commandWidgetClass,parent,XtNlabel,value,XtNshapeStyle,XawShapeRectangle,
        XtNborderWidth,1,XtNinternalWidth,10,XtNinternalHeight,6,XtNbackground,app.face,NULL);
    XtAddCallback(w,XtNcallback,callback,context);
}
static void clicked(Widget w,XtPointer context,XtPointer call);
static Widget popup(const char *name,const char *title) {
    Widget shell=XtVaCreatePopupShell(name,transientShellWidgetClass,app.shell,XtNtitle,title,XtNwidth,540,XtNheight,440,XtNallowShellResize,True,NULL);
    return shell;
}
static void popup_close(Widget w,XtPointer context,XtPointer call) { (void)w; (void)call; Widget *shell=context; if(*shell) { XtDestroyWidget(*shell); *shell=NULL; } }
static void connect_selected(void) {
    char *url=get(app.url),*user=get(app.user),*password=get(app.password),*name=get(app.servername); Boolean allow=False; XtVaGetValues(app.http,XtNstate,&allow,NULL);
    capture_draft(); save(); app.connected=0; app.connecting=1; app.list_pending=app.stats_pending=0;
    cJSON *profiles=cJSON_GetObjectItemCaseSensitive(app.settings,"profiles"),*p;
    if(!profiles) { profiles=cJSON_CreateArray(); cJSON_AddItemToObject(app.settings,"profiles",profiles); }
    cJSON *chosen=NULL; cJSON_ArrayForEach(p,profiles) if(!strcmp(js(p,"url"),url) && !strcmp(js(p,"user"),user)) chosen=p;
    if(!chosen) { chosen=cJSON_CreateObject(); put(chosen,"url",url); put(chosen,"user",user); cJSON_AddItemToArray(profiles,chosen); }
    put(chosen,"name",name); cJSON_DeleteItemFromObjectCaseSensitive(chosen,"http"); cJSON_AddBoolToObject(chosen,"http",allow);
    /* Always reload cached runtime state when reconnecting. */
    profile_load(chosen); put(app.settings,"active_url",url); put(app.settings,"active_user",user);
    set(app.password,""); notice("Connecting to Hermes..."); render();
    linux_net_connect(app.net,url,user,password,allow,js(chosen,"cookie"));
    OPENSSL_cleanse(password,strlen(password)); g_free(password); g_free(url); g_free(user); g_free(name);
}
static void server_pick(Widget w,XtPointer context,XtPointer call) {
    (void)w; (void)context; XawListReturnStruct *row=call;
    cJSON *p=cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(app.settings,"profiles"),row->list_index); if(!p) return;
    set(app.url,js(p,"url")); set(app.user,js(p,"user")); set(app.servername,js(p,"name")); set(app.password,""); XtVaSetValues(app.http,XtNstate,jb(p,"http"),NULL);
}
static void free_names(Widget w,XtPointer context,XtPointer call) { (void)w; (void)call; g_strfreev(context); }
static void server_show(void) {
    if(app.serverwin) { XtPopup(app.serverwin,XtGrabNone); return; }
    app.serverwin=popup("serverManager","Hermes / Server manager");
    Widget pane=XtVaCreateManagedWidget("serverPane",panedWidgetClass,app.serverwin,NULL);
    heading(pane,"savedServers","Saved servers");
    Widget viewport=XtVaCreateManagedWidget("serverViewport",viewportWidgetClass,pane,XtNheight,80,XtNmin,70,XtNallowVert,True,NULL);
    cJSON *profiles=cJSON_GetObjectItemCaseSensitive(app.settings,"profiles"),*p; int n=cJSON_GetArraySize(profiles),i=0;
    char **names=g_new0(char *,n+2); cJSON_ArrayForEach(p,profiles) names[i++]=g_strdup_printf("%s  %s",js(p,"name"),js(p,"url"));
    if(!n) names[0]=g_strdup("Add your first Hermes server below");
    app.serverlist=XtVaCreateManagedWidget("servers",listWidgetClass,viewport,XtNlist,names,XtNnumberStrings,n?n:1,XtNdefaultColumns,1,XtNforceColumns,True,NULL);
    /* XawList borrows the array until destruction. */
    XtAddCallback(app.serverlist,XtNcallback,server_pick,NULL);
    XtAddCallback(app.serverlist,XtNdestroyCallback,free_names,names);
    heading(pane,"nameLabel","Name"); app.servername=text(pane,"serverName",app.profile?js(app.profile,"name"):"",1,32);
    heading(pane,"urlLabel","Address (https://server:port)"); app.url=text(pane,"serverURL",app.profile?js(app.profile,"url"):"",1,32);
    heading(pane,"userLabel","User"); app.user=text(pane,"serverUser",app.profile?js(app.profile,"user"):"",1,32);
    heading(pane,"passwordLabel","Password (never saved)"); app.password=text(pane,"serverPassword","",1,32);
    XtVaSetValues(XawTextGetSink(app.password),XtNecho,False,NULL);
    app.http=XtVaCreateManagedWidget("privateHTTP",toggleWidgetClass,pane,XtNlabel,"Allow HTTP on a trusted private network",XtNstate,app.profile?jb(app.profile,"http"):False,XtNheight,30,XtNmin,30,XtNmax,30,NULL);
    Widget controls=XtVaCreateManagedWidget("serverButtons",boxWidgetClass,pane,XtNheight,42,XtNmin,42,XtNmax,42,NULL);
    button(controls,"connect","Connect",clicked,(XtPointer)"connect"); button(controls,"closeServer","Close",popup_close,&app.serverwin);
    XtPopup(app.serverwin,XtGrabNone); XtSetKeyboardFocus(app.serverwin,app.url);
}
static void model_apply(Widget w,XtPointer context,XtPointer call) {
    (void)w; (void)context; (void)call; Chat *c=app.current; if(!c) return;
    char *model=get(app.model),*provider=get(app.provider),*cwd=get(app.cwd);
    if(strpbrk(model," \r\n") || strpbrk(provider," \r\n")) notice("Use model and provider identifiers without spaces.");
    else if(c->pending) notice("Wait for the current request before changing the model.");
    else {
        if(!*c->id && !*c->stored) g_strlcpy(c->cwd,cwd,sizeof(c->cwd));
        if(*c->id && *model && *provider) {
            cJSON *p=session(c); cJSON_AddStringToObject(p,"key","model");
            char *value=g_strdup_printf("%s --provider %s --session",model,provider); cJSON_AddStringToObject(p,"value",value); g_free(value);
            c->pending=1; rpc("config.set",p,SetModel,c);
        } else if(!*c->id && !*c->stored) { g_strlcpy(c->model,model,sizeof(c->model)); g_strlcpy(c->provider,provider,sizeof(c->provider)); }
        else notice("Resume this chat before changing its model.");
        schedule_save(); XtDestroyWidget(app.modelwin); app.modelwin=NULL;
    }
    g_free(model); g_free(provider); g_free(cwd);
}
static void model_show(void) {
    if(!app.current || app.modelwin) return;
    app.modelwin=popup("modelPicker","Hermes / Model and working folder");
    Widget pane=XtVaCreateManagedWidget("modelPane",panedWidgetClass,app.modelwin,NULL);
    heading(pane,"catalogTitle","Server model catalog"); app.modelcatalog=text(pane,"catalog","Connect to load configured model choices.",0,180);
    heading(pane,"modelTitle","Model identifier (blank uses server default)"); app.model=text(pane,"modelName",app.current->model,1,32);
    heading(pane,"providerTitle","Provider identifier"); app.provider=text(pane,"providerName",app.current->provider,1,32);
    heading(pane,"cwdTitle","Server working folder (new drafts only)"); app.cwd=text(pane,"workingFolder",app.current->cwd,1,32);
    XtSetSensitive(app.cwd,!*app.current->stored && !*app.current->id);
    Widget box=XtVaCreateManagedWidget("modelButtons",boxWidgetClass,pane,XtNheight,42,XtNmin,42,XtNmax,42,NULL);
    button(box,"applyModel","Apply",model_apply,NULL); button(box,"closeModel","Close",popup_close,&app.modelwin);
    XtPopup(app.modelwin,XtGrabNone);
    if(app.connected) { cJSON *p=cJSON_CreateObject(); cJSON_AddBoolToObject(p,"include_unconfigured",0); rpc("model.options",p,Models,NULL); }
}
static void question_reply(Widget w,XtPointer context,XtPointer call) {
    (void)w; (void)call; if(!app.questioncopy) return;
    const char *method=js(app.questioncopy,"method");
    if(!strcmp((char *)context,"cancel")) answer_request(app.questioncopy,NULL,"User cancelled clarification.");
    else if(!strcmp(method,"approval")) {
        cJSON *v=cJSON_CreateObject(); cJSON_AddStringToObject(v,"choice",(char *)context); answer_request(app.questioncopy,v,NULL);
    } else {
        char *answer=get(app.answer),key[40]; const cJSON *q=cJSON_GetArrayItem(app.questions,app.questionindex);
        g_snprintf(key,sizeof(key),"%d",app.questionindex); put(app.answers,*js(q,"qid")?js(q,"qid"):key,answer); g_free(answer);
        app.questionindex++;
        if(app.questionindex<cJSON_GetArraySize(app.questions)) { set(app.question,js(cJSON_GetArrayItem(app.questions,app.questionindex),"question")); set(app.answer,""); return; }
        cJSON *v=cJSON_CreateObject();
        if(app.questionindex==1 && !*js(q,"qid")) cJSON_AddStringToObject(v,"answer",js(app.answers,"0"));
        else cJSON_AddItemToObject(v,"answers",cJSON_Duplicate(app.answers,1));
        answer_request(app.questioncopy,v,NULL);
    }
    XtDestroyWidget(app.questionwin); app.questionwin=NULL;
    cJSON_Delete(app.questioncopy); app.questioncopy=NULL; cJSON_Delete(app.questions); app.questions=NULL; cJSON_Delete(app.answers); app.answers=NULL;
}
static void request_show(void) {
    if(!app.current || !cJSON_GetArraySize(app.current->requests) || app.questionwin) return;
    app.questioncopy=cJSON_Duplicate(cJSON_GetArrayItem(app.current->requests,0),1);
    const cJSON *p=cJSON_GetObjectItemCaseSensitive(app.questioncopy,"params"); const char *method=js(app.questioncopy,"method");
    if(strcmp(method,"approval") && strcmp(method,"clarify")) { answer_request(app.questioncopy,NULL,"Unsupported request type."); cJSON_Delete(app.questioncopy); app.questioncopy=NULL; return; }
    app.questionwin=popup("hermesRequest","Hermes / Your input is needed");
    Widget pane=XtVaCreateManagedWidget("requestPane",panedWidgetClass,app.questionwin,NULL);
    app.question=text(pane,"requestDescription","",0,260);
    Widget box;
    if(!strcmp(method,"approval")) {
        char *description=g_strdup_printf("%s\n\n%s",js(p,"description"),js(p,"command")); set(app.question,description); g_free(description);
        box=XtVaCreateManagedWidget("requestButtons",boxWidgetClass,pane,NULL);
        button(box,"deny","Deny",question_reply,(XtPointer)"deny");
        int once=!cJSON_HasObjectItem(p,"choices"); const cJSON *choice;
        cJSON_ArrayForEach(choice,cJSON_GetObjectItemCaseSensitive(p,"choices")) if(cJSON_IsString(choice) && !strcmp(choice->valuestring,"once")) once=1;
        if(once) button(box,"allowOnce","Allow once",question_reply,(XtPointer)"once");
    } else {
        const cJSON *questions=cJSON_GetObjectItemCaseSensitive(p,"questions");
        app.questions=cJSON_IsArray(questions)?cJSON_Duplicate(questions,1):cJSON_CreateArray();
        if(!cJSON_IsArray(questions)) cJSON_AddItemToArray(app.questions,cJSON_Duplicate(p,1));
        app.questionindex=0; app.answers=cJSON_CreateObject(); set(app.question,js(cJSON_GetArrayItem(app.questions,0),"question"));
        app.answer=text(pane,"clarificationAnswer","",1,100);
        box=XtVaCreateManagedWidget("questionButtons",boxWidgetClass,pane,NULL);
        button(box,"cancelQuestion","Cancel",question_reply,(XtPointer)"cancel"); button(box,"answerQuestion","Answer",question_reply,(XtPointer)"answer");
    }
    XtPopup(app.questionwin,XtGrabNone);
}
static void clicked(Widget w,XtPointer context,XtPointer call) {
    (void)w; (void)call; const char *action=context;
    if(!strcmp(action,"new")) fresh();
    else if(!strcmp(action,"server")) server_show();
    else if(!strcmp(action,"send")) submit();
    else if(!strcmp(action,"connect")) connect_selected();
    else if(!strcmp(action,"request")) request_show();
    else if(!strcmp(action,"models")) model_show();
    else if(!strcmp(action,"refresh")) { if(app.connected) list_chats(); else server_show(); }
    else if(!strcmp(action,"quit")) app.run=0;
}
static void action(Widget w,XEvent *event,String *params,Cardinal *count) {
    (void)w; (void)event; if(*count) clicked(NULL,params[0],NULL);
}
static void close_event(Widget w,XtPointer context,XEvent *event,Boolean *dispatch) {
    (void)w; (void)context; (void)dispatch;
    if(event->type==ClientMessage && (Atom)event->xclient.data.l[0]==app.wm_delete) app.run=0;
}
static gboolean tick(gpointer context) {
    (void)context; gint64 now=g_get_monotonic_time(); Wait **link=&app.wait;
    while(*link) {
        Wait *w=*link;
        if(now-w->at>90000000) {
            *link=w->next; if(w->chat) w->chat->pending=0; if(w->kind==List) app.list_pending=0;
            notice(w->kind==Submit?"Send timed out. Draft retained; check history before sending again.":"Request timed out. Reconnect to resume."); g_free(w); render();
        } else link=&w->next;
    }
    return G_SOURCE_CONTINUE;
}
static gboolean stats(gpointer context) {
    (void)context; if(app.connected && !app.stats_pending) { app.stats_pending=1; linux_net_rest(app.net,"/api/system/stats",Stats); }
    return G_SOURCE_CONTINUE;
}
static void noop_input(XtPointer context,int *fd,XtInputId *id) { (void)context; (void)fd; (void)id; }
static void noop_timer(XtPointer context,XtIntervalId *id) { (void)context; (void)id; }
/* Let Xt block on GLib's actual descriptors/deadlines: no per-frame idle polling. */
static void loop(void) {
    GMainContext *context=g_main_context_default();
    while(app.run) {
        while(XtAppPending(app.xt)) XtAppProcessEvent(app.xt,XtIMAll);
        if(g_main_context_pending(context)) { g_main_context_iteration(context,FALSE); continue; }
        gint priority,timeout;
        g_main_context_acquire(context); g_main_context_prepare(context,&priority);
        int n=g_main_context_query(context,priority,&timeout,NULL,0);
        GPollFD *fds=g_new0(GPollFD,n); XtInputId *ids=g_new0(XtInputId,n);
        int queried=g_main_context_query(context,priority,&timeout,fds,n);
        if(queried>n) { g_free(fds); g_free(ids); g_main_context_release(context); continue; }
        for(int i=0;i<queried;i++) {
            XtPointer mask=(XtPointer)(uintptr_t)(((fds[i].events&G_IO_IN)?XtInputReadMask:0)|((fds[i].events&G_IO_OUT)?XtInputWriteMask:0)|((fds[i].events&(G_IO_ERR|G_IO_HUP))?XtInputExceptMask:0));
            if(mask) ids[i]=XtAppAddInput(app.xt,fds[i].fd,mask,noop_input,NULL);
        }
        XtIntervalId timer=timeout>=0?XtAppAddTimeOut(app.xt,(unsigned long)timeout,noop_timer,NULL):0;
        g_main_context_release(context);
        XtAppProcessEvent(app.xt,XtIMAll);
        for(int i=0;i<queried;i++) if(ids[i]) XtRemoveInput(ids[i]);
        /* A fired Xt timeout has already been removed. Avoid removing it twice. */
        if(timer) XtRemoveTimeOut(timer);
        g_free(fds); g_free(ids); g_main_context_iteration(context,FALSE);
    }
}
static void demo(void) {
    app.demo=1; app.settings=cJSON_CreateObject(); fresh();
    Chat *c=app.current; g_strlcpy(c->title,"A small Unix workbench",sizeof(c->title));
    cJSON *row=cJSON_CreateObject(); cJSON_AddStringToObject(row,"role","user"); cJSON_AddStringToObject(row,"text","Can we keep the classic beige workbench on Linux?"); cJSON_AddItemToArray(c->messages,row);
    row=cJSON_CreateObject(); cJSON_AddStringToObject(row,"role","assistant"); cJSON_AddStringToObject(row,"text","Yes. Native C and Athena widgets, with the Windows palette.\n\nSquare controls, paper-colored text, a quiet activity pane.\nNo browser engine. No Windows runtime.\n\nUTF-8: café / λ / 日本語\n\nThis is an offline preview. Use Server to connect to Hermes."); cJSON_AddItemToArray(c->messages,row);
    g_strlcpy(c->activity,"Offline preview",sizeof(c->activity)); cJSON_AddItemToArray(c->activity_log,cJSON_CreateString("Native X11 / Xt / Athena"));
    fresh(); g_strlcpy(app.current->title,"Separate unsent draft",sizeof(app.current->title));
    free(app.current->draft); app.current->draft=textdup("Each conversation keeps its own draft.");
    app.current=c; sidebar(); render();
}
static void ui_init(int *argc,char **argv) {
    setlocale(LC_ALL,""); XtSetLanguageProc(NULL,NULL,NULL);
    String resources[]={"*background: #dedac7","*foreground: #2b302a","*borderColor: #7e806f","*international: True","*fontSet: -misc-fixed-medium-r-normal--14-*-*-*-*-*-*-*","*List.background: #fffdee","*Text.background: #fffdee","*Command.highlightThickness: 1","*Paned.internalBorderWidth: 2",NULL};
    app.shell=XtVaAppInitialize(&app.xt,"LCBHermes",NULL,0,argc,argv,resources,XtNtitle,"lcb-hermes / Unix workbench",XtNwidth,1060,XtNheight,700,XtNminWidth,700,XtNminHeight,460,NULL);
    Display *display=XtDisplay(app.shell); Colormap cmap=DefaultColormapOfScreen(XtScreen(app.shell)); XColor color,exact;
    XAllocNamedColor(display,cmap,"#dedac7",&color,&exact); app.face=color.pixel;
    XAllocNamedColor(display,cmap,"#fffdee",&color,&exact); app.paper=color.pixel;
    XAllocNamedColor(display,cmap,"#2b302a",&color,&exact); app.ink=color.pixel;
    XAllocNamedColor(display,cmap,"#c0cbb3",&color,&exact); app.accent=color.pixel;
    char **missing=NULL,*default_string=NULL; int missing_count;
    app.fontset=XCreateFontSet(display,"-misc-fixed-medium-r-normal--14-*-*-*-*-*-*-*",&missing,&missing_count,&default_string);
    if(missing) XFreeStringList(missing);
    if(!app.fontset) { fprintf(stderr,"Install X11 fixed bitmap fonts or set a usable fontSet resource.\n"); exit(1); }
    Widget root=XtVaCreateManagedWidget("workbench",panedWidgetClass,app.shell,NULL);
    Widget toolbar=XtVaCreateManagedWidget("toolbar",boxWidgetClass,root,XtNheight,42,XtNmin,42,XtNmax,42,XtNshowGrip,False,NULL);
    button(toolbar,"server","Server",clicked,(XtPointer)"server"); button(toolbar,"newChat","New chat",clicked,(XtPointer)"new");
    button(toolbar,"model","Model / Folder",clicked,(XtPointer)"models"); button(toolbar,"refresh","Refresh",clicked,(XtPointer)"refresh");
    button(toolbar,"quit","Exit",clicked,(XtPointer)"quit");
    Widget body=XtVaCreateManagedWidget("columns",panedWidgetClass,root,XtNorientation,XtorientHorizontal,XtNheight,625,NULL);
    Widget left=XtVaCreateManagedWidget("sidebar",panedWidgetClass,body,XtNwidth,230,XtNmin,150,NULL);
    heading(left,"conversations","Conversations");
    Widget viewport=XtVaCreateManagedWidget("chatViewport",viewportWidgetClass,left,XtNallowVert,True,XtNheight,540,NULL);
    app.list=XtVaCreateManagedWidget("chats",listWidgetClass,viewport,XtNdefaultColumns,1,XtNforceColumns,True,XtNverticalList,True,XtNrowSpacing,12,XtNfontSet,app.fontset,XtNbackground,app.paper,NULL);
    XtAddCallback(app.list,XtNcallback,selected,NULL);
    Widget center=XtVaCreateManagedWidget("conversation",panedWidgetClass,body,XtNwidth,610,XtNmin,320,NULL);
    app.title=heading(center,"chatTitle","new chat"); app.transcript=text(center,"transcript","",0,425);
    app.prompt=text(center,"prompt","",1,116); XtVaSetValues(app.prompt,XtNmin,70,NULL);
    XtAddCallback(XawTextGetSource(app.prompt),XtNcallback,changed,NULL);
    Widget composer=XtVaCreateManagedWidget("composerButtons",boxWidgetClass,center,XtNheight,44,XtNmin,44,XtNmax,44,XtNshowGrip,False,NULL);
    app.send=XtVaCreateManagedWidget("send",commandWidgetClass,composer,XtNlabel,"Send",XtNbackground,app.accent,XtNinternalWidth,18,XtNinternalHeight,7,NULL); XtAddCallback(app.send,XtNcallback,clicked,(XtPointer)"send");
    app.request=XtVaCreateManagedWidget("answerRequest",commandWidgetClass,composer,XtNlabel,"Answer Hermes request",XtNinternalWidth,10,XtNinternalHeight,7,NULL); XtAddCallback(app.request,XtNcallback,clicked,(XtPointer)"request");
    Widget right=XtVaCreateManagedWidget("monitorPane",panedWidgetClass,body,XtNwidth,218,XtNmin,150,NULL);
    heading(right,"monitorTitle","Server monitor"); app.monitor=text(right,"monitor","Offline\n\nConnect to Hermes to view\nserver resource readings.",0,200);
    heading(right,"activityTitle","Hermes activity"); app.activity=text(right,"activity","Ready",0,320);
    app.status=heading(root,"status","Offline / Ctrl+Enter sends / Ctrl+N starts a chat");
    XtActionsRec actions[]={{"hermes",action}}; XtAppAddActions(app.xt,actions,1);
    XtOverrideTranslations(app.prompt,XtParseTranslationTable("Ctrl<Key>Return: hermes(send)\nCtrl<Key>n: hermes(new)\nCtrl<Key>r: hermes(refresh)"));
    XtRealizeWidget(app.shell); app.wm_delete=XInternAtom(display,"WM_DELETE_WINDOW",False);
    XSetWMProtocols(display,XtWindow(app.shell),&app.wm_delete,1); XtAddEventHandler(app.shell,NoEventMask,True,close_event,NULL);
    app.net=linux_net_new(received,NULL); app.store=linux_store_new();
}
int main(int argc,char **argv) {
    int preview=0;
    for(int i=1;i<argc;i++) {
        if(!strcmp(argv[i],"--demo")) { preview=1; memmove(argv+i,argv+i+1,(size_t)(argc-i)*sizeof(*argv)); argc--; i--; }
        else if(!strcmp(argv[i],"--help")) { puts("lcb-hermes [--demo] [standard Xt options]\n--demo: offline preview; no server or persistent storage.\nServer opens the saved-server/login panel. Ctrl+Enter sends; Ctrl+N starts a chat."); return 0; }
    }
    ui_init(&argc,argv);
    if(preview) demo();
    else {
        char *error=NULL; app.settings=linux_store_read(app.store,&error);
        if(!app.settings) { app.corrupt=1; app.settings=cJSON_CreateObject(); notice(error); } g_free(error);
        cJSON *p; cJSON_ArrayForEach(p,cJSON_GetObjectItemCaseSensitive(app.settings,"profiles"))
            if(!strcmp(js(p,"url"),js(app.settings,"active_url")) && !strcmp(js(p,"user"),js(app.settings,"active_user"))) { profile_load(p); break; }
        if(!app.current) fresh();
        server_show();
    }
    g_timeout_add_seconds(1,tick,NULL); g_timeout_add_seconds(5,stats,NULL);
    app.run=1; render(); loop(); save(); linux_net_free(app.net); linux_store_free(app.store); clear_chats();
    cJSON_Delete(app.settings); cJSON_Delete(app.models); cJSON_Delete(app.questioncopy); cJSON_Delete(app.questions); cJSON_Delete(app.answers);
    g_strfreev(app.labels); Display *display=XtDisplay(app.shell); XtDestroyWidget(app.shell); XFreeFontSet(display,app.fontset); XtDestroyApplicationContext(app.xt); return 0;
}
