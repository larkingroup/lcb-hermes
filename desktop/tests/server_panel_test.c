#include "transport_win.h"
static int capture_write(Net *n,const char *method,const char *path,const cJSON *body,int tag,void *ctx);
static void capture_rest(Net *n,const char *path,int tag,void *ctx);
#define net_write capture_write
#define net_rest capture_rest
#define main previous_contracts_entry
#include "contracts_test.c"
#undef main
#undef net_write
#undef net_rest
static char method_sent[12],path_sent[1024];
static cJSON *body_sent;
static int write_tag,read_tag,writes,reads;
static void *write_context;
static int capture_write(Net *n,const char *method,const char *path,const cJSON *body,int tag,void *ctx) {
    (void)n; strcpy(method_sent,method); strcpy(path_sent,path); cJSON_Delete(body_sent); body_sent=body?cJSON_Duplicate(body,1):NULL; write_tag=tag; write_context=ctx; writes++; return 1;
}
static void capture_rest(Net *n,const char *path,int tag,void *ctx) { (void)n; (void)ctx; strcpy(path_sent,path); read_tag=tag; reads++; }
static void admin_reply(int tag,const char *json,const char *error) {
    NetMessage m={0}; m.tag=tag; m.context=(void *)(UINT_PTR)admin.serial; strcpy(m.url,app.url); strcpy(m.user,app.user); copytext(m.error,sizeof(m.error),error); m.json=json?cJSON_Parse(json):NULL; admin_result(&m); cJSON_Delete(m.json);
}
static void press(int id) { SendMessageW(admin.window,WM_COMMAND,id,0); }
int main(void) {
    wchar_t temp[MAX_PATH]; HWND w; int before; unsigned int oldserial; Chat *chat,*cron; const cJSON *p;
    app.settings=cJSON_CreateObject(); app.sidebar_width=290; app.show_monitor=1; GetTempPathW(MAX_PATH,temp); GetTempFileNameW(temp,L"lca",0,app.path);
    assert(create_window(GetModuleHandleW(NULL),SW_HIDE)); profile_load("",""); strcpy(app.url,"http://127.0.0.1:7777"); strcpy(app.user,"test"); app.connected=1;
    strcpy(app.server_cwd,"/opt/data/workspace"); newchat(); chat=app.current; assert(!strcmp(chat->cwd,app.server_cwd));
    strcpy(chat->stored,"default-chat"); strcpy(chat->folder,app.server_cwd); chat->folder_known=1; assert(!*chat_group(chat));
    chat->folder[0]=0; assert(!*chat_group(chat));
    strcpy(chat->folder,"/projects/research"); assert(!strcmp(chat_group(chat),"/projects/research"));
    cron=chat_new("cron"); strcpy(cron->source,"cron"); strcpy(cron->stored,"run-1"); addchat(cron); assert(!strcmp(chat_group(cron),"@cron")); sidebar(); assert(parent(cron)!=parent(chat));
    app.stats=cJSON_Parse("{\"hermes_version\":\"test\"}"); strcpy(app.default_model,"PRIVATE-MODEL-LABEL"); status_stats(); { char *label=gettext(app.monitor); assert(!strstr(label,"PRIVATE-MODEL-LABEL")&&!strstr(label,"DEFAULT MODEL")); free(label); }
    show_admin(); w=admin.window; assert(w&&TabCtrl_GetItemCount(GetDlgItem(w,899))==3&&reads==5);
    admin_reply(AdminEnv,"{\"OPENAI_API_KEY\":{\"category\":\"model\",\"is_set\":true},\"ANTHROPIC_API_KEY\":{}}",""); assert(admin.env_ok);
    SetDlgItemTextW(w,914,L"OPENAI_API_KEY"); SetDlgItemTextW(w,915,L"synthetic-test-key"); press(916);
    assert(!strcmp(method_sent,"PUT")&&!strcmp(path_sent,"/api/env")&&write_tag==AdminSaveKey);
    assert(!strcmp(js(body_sent,"value"),"synthetic-test-key")&&jb(body_sent,"provider_setup")&&GetWindowTextLengthW(GetDlgItem(w,915))==0);
    { char *saved=cJSON_PrintUnformatted(app.settings); assert(!strstr(saved,"synthetic-test-key")); free(saved); }
    before=writes; press(916); assert(writes==before); admin_reply(AdminSaveKey,NULL,"HTTP failure"); assert(!admin.busy);
    admin_reply(AdminOAuth,"{\"providers\":[{\"id\":\"openai-codex\",\"name\":\"Codex\",\"flow\":\"device_code\",\"status\":{\"logged_in\":true}},{\"id\":\"external\",\"name\":\"External\",\"flow\":\"external\",\"cli_command\":\"hermes auth external\"}]}","");
    press(903); assert(write_tag==AdminStart&&admin.auth_busy&&!strcmp(path_sent,"/api/providers/oauth/openai-codex/start"));
    /* Cancel while start is in flight; its eventual session is cancelled, never displayed. */
    press(904); assert(admin.auth_cancelled&&admin.auth_busy);
    admin_reply(AdminStart,"{\"session_id\":\"cancelled-flow\",\"flow\":\"device_code\",\"verification_url\":\"https://example.com/login\",\"user_code\":\"EXAMPLE\"}","");
    assert(write_tag==AdminCancel&&!strcmp(method_sent,"DELETE")&&!*admin.session&&!admin.auth_busy);
    press(903); admin_reply(AdminStart,"{\"session_id\":\"active-flow\",\"flow\":\"device_code\",\"verification_url\":\"https://example.com/login\",\"user_code\":\"EXAMPLE\",\"poll_interval\":5,\"expires_in\":600}","");
    assert(!strcmp(admin.session,"active-flow")&&!IsWindowEnabled(GetDlgItem(w,908))&&admin.poll_ms==5000);
    SendMessageW(w,WM_TIMER,1,0); before=reads; SendMessageW(w,WM_TIMER,1,0); assert(reads==before&&read_tag==AdminPoll);
    admin_reply(AdminPoll,"{\"status\":\"pending\"}",""); assert(*admin.session);
    admin_reply(AdminPoll,"{\"status\":\"approved\"}",""); assert(!*admin.session&&read_tag==AdminOAuth);
    SendDlgItemMessageW(w,902,CB_SETCURSEL,1,0); admin_provider_details(); assert(!IsWindowEnabled(GetDlgItem(w,903))); before=writes; press(903); assert(writes==before);
    /* PKCE when advertised by a server. Codes are cleared and never persisted. */
    admin_reply(AdminOAuth,"{\"providers\":[{\"id\":\"pkce-test\",\"name\":\"PKCE\",\"flow\":\"pkce\"}]}",""); press(903);
    admin_reply(AdminStart,"{\"session_id\":\"pkce-flow\",\"flow\":\"pkce\",\"auth_url\":\"https://example.com/oauth\",\"expires_in\":300}","");
    SetDlgItemTextW(w,908,L"synthetic-return-code"); press(909); assert(write_tag==AdminSubmit&&!strcmp(js(body_sent,"session_id"),"pkce-flow")&&!strcmp(js(body_sent,"code"),"synthetic-return-code")&&GetWindowTextLengthW(GetDlgItem(w,908))==0);
    admin_reply(AdminSubmit,"{\"status\":\"approved\",\"ok\":true}","");
    admin_reply(AdminEndpoints,"{\"endpoints\":[{\"id\":\"existing\",\"name\":\"Local\",\"base_url\":\"http://localhost:8080/v1\",\"model\":\"local-model\",\"api_mode\":\"chat_completions\"}]}","");
    SendDlgItemMessageW(w,932,CB_SETCURSEL,1,0); admin_endpoint_details(); press(942);
    assert(write_tag==AdminSaveEndpoint&&!strcmp(js(body_sent,"id"),"existing")&&!cJSON_HasObjectItem(body_sent,"api_key")&&!jb(body_sent,"make_default"));
    admin_reply(AdminSaveEndpoint,"{\"ok\":true}","");
    SendDlgItemMessageW(w,932,CB_SETCURSEL,0,0); admin_endpoint_details(); SetDlgItemTextW(w,934,L"New provider"); SetDlgItemTextW(w,936,L"https://example.com/v1"); SetDlgItemTextW(w,938,L"new-model"); SetDlgItemTextW(w,940,L"synthetic-endpoint-key"); press(942);
    assert(!cJSON_HasObjectItem(body_sent,"id")&&!strcmp(js(body_sent,"api_key"),"synthetic-endpoint-key")&&GetWindowTextLengthW(GetDlgItem(w,940))==0); admin_reply(AdminSaveEndpoint,"{\"ok\":true}","");
    admin_reply(AdminConfig,"{\"terminal\":{\"cwd\":\"/opt/data/workspace\"},\"auxiliary\":{\"title_generation\":{\"enabled\":true,\"model_upgrade_enabled\":true}}}","");
    SendDlgItemMessageW(w,955,BM_SETCHECK,BST_UNCHECKED,0); press(956); p=cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(body_sent,"config"),"auxiliary"),"title_generation");
    assert(write_tag==AdminSaveTitles&&cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(p,"model_upgrade_enabled"))&&!cJSON_HasObjectItem(p,"enabled")); admin_reply(AdminSaveTitles,"{\"ok\":true}","");
    SetDlgItemTextW(w,952,L"/new-default"); press(953); assert(!strcmp(js(wire,"method"),"config.set")); p=cJSON_GetObjectItemCaseSensitive(wire,"params"); assert(!strcmp(js(p,"key"),"terminal.cwd")&&!cJSON_HasObjectItem(p,"session_id"));
    response(RpcDefaultFolder,NULL,"{\"cwd\":\"/new-default\"}"); assert(!strcmp(app.server_cwd,"/new-default")&&!admin.busy); assert(!strcmp(chat->cwd,"/opt/data/workspace"));
    admin_reply(AdminUpdate,"{\"current_version\":\"0.21.5\",\"install_method\":\"managed-runtime\",\"can_apply\":false,\"update_available\":true,\"message\":\"Managed by container host\"}",""); assert(!admin.can_update&&!IsWindowEnabled(GetDlgItem(w,961))); before=writes; press(961); assert(writes==before);
    /* The auxiliary picker must not change the main default or current chat. */
    app.models=cJSON_Parse("{\"provider\":\"openai\",\"providers\":[{\"slug\":\"openai\",\"name\":\"OpenAI\",\"authenticated\":true,\"is_current\":true,\"models\":[\"title-model\"]}]}");
    press(957); assert(app.server_model==2&&app.modelwin); SendMessageW(app.modelwin,WM_COMMAND,713,0); assert(!strcmp(js(posted,"scope"),"auxiliary")&&!strcmp(js(posted,"task"),"title_generation"));
    /* Close/reopen creates a new request generation; late results cannot populate the new dialog. */
    oldserial=admin.serial; admin_close(); show_admin(); assert(admin.serial!=oldserial&&!admin.env_ok);
    { NetMessage m={0}; m.tag=AdminEnv; m.context=(void *)(UINT_PTR)oldserial; strcpy(m.url,app.url); strcpy(m.user,app.user); m.json=cJSON_Parse("{}"); admin_result(&m); cJSON_Delete(m.json); assert(!admin.env_ok); }
    before=writes; strcpy(admin.url,"http://different-server"); admin.env_ok=1; press(916); assert(writes==before);
    admin_close(); DestroyWindow(app.window); cleanup(); DeleteFileW(app.path); cJSON_Delete(wire); cJSON_Delete(posted); cJSON_Delete(body_sent);
    puts("Native provider/key/custom endpoint/OAuth cancellation/config/update guards and chat groups: passed"); return 0;
}
