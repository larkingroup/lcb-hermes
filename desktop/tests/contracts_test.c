/* Native controls with captured transport and Shell calls. No live server writes. */
#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include "transport_win.h"
#include <assert.h>
static int capture_send(Net *n,const cJSON *f);
static int capture_post(Net *n,const char *path,const cJSON *body,int tag,void *ctx);
static BOOL WINAPI capture_shell(DWORD action,PNOTIFYICONDATAW n);
#define net_send capture_send
#define net_post capture_post
#define Shell_NotifyIconW capture_shell
#define wWinMain unused_entry
#include "../src/winmain.c"
#undef wWinMain
#undef Shell_NotifyIconW
#undef net_post
#undef net_send
static cJSON *wire,*posted;
static int notices,adds,removes,shell_available=1;
static int capture_send(Net *n,const cJSON *f) { (void)n; cJSON_Delete(wire); wire=cJSON_Duplicate(f,1); return 1; }
static int capture_post(Net *n,const char *path,const cJSON *body,int tag,void *ctx) {
    (void)n; (void)ctx; assert(!strcmp(path,"/api/model/set")); assert(tag==RestSetDefault);
    cJSON_Delete(posted); posted=cJSON_Duplicate(body,1); return 1;
}
static BOOL WINAPI capture_shell(DWORD action,PNOTIFYICONDATAW n) {
    if(action==NIM_ADD) adds++; if(action==NIM_DELETE) removes++;
    if(action==NIM_MODIFY&&(n->uFlags&NIF_INFO)) notices++;
    return shell_available;
}
static void response(int kind,Chat *c,const char *json) {
    Pending p={0}; cJSON *r=cJSON_Parse(json); assert(r); p.kind=kind; p.chat=c; result(&p,r,""); cJSON_Delete(r);
}
static void snapshot(const char *json) { cJSON *j=cJSON_Parse(json); assert(j); cron_snapshot(j); cJSON_Delete(j); }
static HTREEITEM parent(Chat *c) { size_t i; for(i=0;i<app.nrows;i++) if(app.rows[i].chat==c) return app.rows[i].parent; assert(0); return NULL; }
int main(void) {
    wchar_t temp[MAX_PATH]; Chat *c,*restored; cJSON *saved,*event; HTREEITEM folder; int before;
    app.settings=cJSON_CreateObject(); app.sidebar_width=270; app.show_monitor=1;
    GetTempPathW(MAX_PATH,temp); GetTempFileNameW(temp,L"lct",0,app.path);
    assert(create_window(GetModuleHandleW(NULL),SW_HIDE)); profile_load("","");
    KillTimer(app.window,IdHeartbeat); KillTimer(app.window,IdStats); KillTimer(app.window,IdCron); KillTimer(app.window,IdTimeout);
    app.connected=1;
    response(RpcList,NULL,"{\"sessions\":[{\"id\":\"saved\",\"title\":\"Keep this unfiled\"}]}");
    c=findchat("saved"); assert(c); app.current=c;
    response(RpcProjectTree,NULL,"{\"projects\":[{\"isNoProject\":true,\"previewSessions\":[{\"id\":\"saved\",\"cwd\":null}]}]}");
    folder=parent(c); assert(c->folder_known&&!*c->folder);
    response(RpcResume,c,"{\"session_id\":\"runtime\",\"session_key\":\"saved\",\"info\":{\"cwd\":\"/opt/data/workspace\",\"project\":{\"id\":\"fallback\",\"name\":\"workspace\"}},\"messages\":[]}");
    assert(!strcmp(c->cwd,"/opt/data/workspace")); assert(!*c->folder&&!*c->project_id); assert(parent(c)==folder);
    event=cJSON_Parse("{\"session_id\":\"runtime\",\"seq\":1,\"type\":\"session.info\",\"payload\":{\"cwd\":\"/fallback2\",\"project\":{\"id\":\"fallback\",\"name\":\"workspace\"}}}");
    incoming_event(event); cJSON_Delete(event); assert(parent(c)==folder&&!*c->folder);
    /* Tree updates are authoritative even when already loaded, including a move back to unfiled. */
    response(RpcProjectTree,NULL,"{\"projects\":[{\"id\":\"real\",\"label\":\"Research\",\"previewSessions\":[{\"id\":\"saved\",\"cwd\":\"/research\"}]}]}");
    assert(!strcmp(c->folder,"/research")&&!strcmp(c->project_name,"Research")&&!strcmp(c->cwd,"/fallback2"));
    saved=chat_json(c); restored=chat_restore(saved); assert(restored->folder_known&&!strcmp(restored->folder,"/research")); chat_free(restored); cJSON_Delete(saved);
    response(RpcProjectTree,NULL,"{\"projects\":[{\"isNoProject\":true,\"previewSessions\":[{\"id\":\"saved\",\"cwd\":null}]}]}");
    assert(!*c->folder&&!*c->project_name); choose_workspace("/new-chats-only"); assert(!strcmp(c->cwd,"/fallback2")&&!*c->folder);
    saved=cJSON_Parse("{\"key\":\"old\",\"stored\":\"old\",\"cwd\":\"/possibly-fallback\",\"project_name\":\"workspace\"}");
    restored=chat_restore(saved); assert(!restored->folder_known&&!*restored->project_name); chat_free(restored); cJSON_Delete(saved);
    /* Baseline, completion, duplicate, failure, numeric stamps, profile isolation. */
    app.notifications=1;
    snapshot("[{\"id\":\"job\",\"last_run_at\":\"yesterday\",\"last_status\":\"success\"}]"); assert(notices==0);
    snapshot("[{\"id\":\"job\",\"last_run_at\":\"today\",\"last_status\":\"running\"}]"); assert(notices==0);
    snapshot("[{\"id\":\"job\",\"last_run_at\":\"today\",\"last_status\":\"success\"}]"); assert(notices==1);
    snapshot("[{\"id\":\"job\",\"last_run_at\":\"today\",\"last_status\":\"success\"}]"); assert(notices==1);
    snapshot("[{\"id\":\"job\",\"last_run_at\":12345,\"last_status\":\"error\"}]"); assert(notices==2);
    snapshot("{\"error\":\"unavailable\"}"); assert(notices==2);
    app.notifications=0; shell_notify("test","muted",c,0); assert(notices==2); app.notifications=1;
    /* Native minimize/restore messages and Explorer restart. */
    app.min_tray=1; ShowWindow(app.window,SW_SHOW); SendMessageW(app.window,WM_SIZE,SIZE_MINIMIZED,0); assert(!IsWindowVisible(app.window));
    SendMessageW(app.window,SHELL_MESSAGE,1,WM_LBUTTONUP); assert(IsWindowVisible(app.window));
    before=adds; SendMessageW(app.window,app.taskbar_created,0,0); assert(adds==before+1);
    shell_notify("Reply","Ready",c,0); SendMessageW(app.window,SHELL_MESSAGE,1,NIN_BALLOONUSERCLICK); assert(app.current==c&&IsWindowVisible(app.window));
    shell_available=0; ShowWindow(app.window,SW_HIDE); SendMessageW(app.window,app.taskbar_created,0,0); assert(IsWindowVisible(app.window)); shell_available=1;
    /* Server model uses REST main scope, never the session model RPC. */
    strcpy(app.url,"http://127.0.0.1:7777"); strcpy(app.default_model,"old-model");
    app.models=cJSON_Parse("{\"provider\":\"openai\",\"providers\":[{\"slug\":\"openai\",\"name\":\"OpenAI\",\"authenticated\":true,\"is_current\":true,\"models\":[\"new-model\"]}]}");
    SendMessageW(app.window,WM_COMMAND,IdServerModel,0); assert(app.modelwin&&app.server_model&&!app.model_chat);
    assert(!IsWindowEnabled(GetDlgItem(app.modelwin,704)));
    ListView_SetItemState(GetDlgItem(app.modelwin,703),0,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);
    SendMessageW(app.modelwin,WM_COMMAND,713,0);
    assert(app.default_pending&&!app.modelwin&&!strcmp(js(posted,"scope"),"main")&&!strcmp(js(posted,"model"),"new-model"));
    assert(!cJSON_HasObjectItem(posted,"session_id")&&!cJSON_HasObjectItem(posted,"reasoning_effort"));
    { NetMessage *m=calloc(1,sizeof(*m)); m->kind=NetRest; m->tag=RestSetDefault; strcpy(m->url,app.url); m->json=cJSON_Parse("{\"ok\":true}"); on_net(m); }
    assert(!app.default_pending&&!strcmp(js(wire,"method"),"config.get"));
    profile_load("other-server","other-user"); snapshot("[{\"id\":\"job\",\"last_run_at\":99999,\"last_status\":\"success\"}]"); assert(notices==3);
    shell_add(); before=removes; SendMessageW(app.window,WM_CLOSE,0,0); assert(!IsWindow(app.window)&&removes==before+1);
    cleanup(); DeleteFileW(app.path); cJSON_Delete(wire); cJSON_Delete(posted);
    puts("Workspace resume, loaded moves, cache migration, cron dedup, tray lifecycle, server model scope: passed"); return 0;
}
