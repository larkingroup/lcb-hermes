/* Native server settings. Included here to share the small Win32 control helpers. */
enum { AdminEnv=100,AdminOAuth,AdminEndpoints,AdminConfig,AdminUpdate,AdminSaveKey,AdminSaveEndpoint,AdminSaveTitles,AdminStart,AdminPoll,AdminSubmit,AdminCancel,AdminApplyUpdate,AdminAction };
static struct {
    HWND window; cJSON *oauth,*endpoints; unsigned int serial;
    char url[1024],user[256],session[160],provider[160],auth_url[4096],action[160];
    int page,busy,auth_busy,auth_cancelled,action_pending,env_ok,oauth_ok,endpoints_ok,config_ok,can_update,poll_ms;
    ULONGLONG expires;
} admin;
static HWND admin_window(void) { return admin.window; }
static void admin_note(const char *text) { settext(GetDlgItem(admin.window,990),text); }
static int admin_connected(void) { return app.connected&&!strcmp(admin.url,app.url)&&!strcmp(admin.user,app.user); }
static void secret_free(char *text) { if(text) SecureZeroMemory(text,strlen(text)); free(text); }
static void secret_json_free(cJSON *o) { cJSON *v; if(!o) return; cJSON_ArrayForEach(v,o) if(cJSON_IsString(v)&&v->valuestring) SecureZeroMemory(v->valuestring,strlen(v->valuestring)); cJSON_Delete(o); }
static int path_piece(const char *s) { if(!*s||strlen(s)>159) return 0; for(;*s;s++) if(!((*s>='a'&&*s<='z')||(*s>='A'&&*s<='Z')||(*s>='0'&&*s<='9')||*s=='-'||*s=='_')) return 0; return 1; }
static void admin_get(const char *path,int tag) { if(admin_connected()) net_rest(app.net,path,tag,(void *)(UINT_PTR)admin.serial); }
static int admin_write(const char *method,const char *path,const cJSON *body,int tag) {
    if(!admin_connected()) { admin_note("Reconnect to this server before saving."); return 0; }
    if(!net_write(app.net,method,path,body,tag,(void *)(UINT_PTR)admin.serial)) { admin_note("Could not queue the request. Nothing was retried."); return 0; } return 1;
}
static const cJSON *admin_provider(void) { LRESULT i=SendDlgItemMessageW(admin.window,902,CB_GETCURSEL,0,0); return i==CB_ERR?NULL:cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(admin.oauth,"providers"),(int)i); }
static const cJSON *admin_endpoint(void) { LRESULT i=SendDlgItemMessageW(admin.window,932,CB_GETCURSEL,0,0); return i<=0?NULL:cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(admin.endpoints,"endpoints"),(int)i-1); }
static void admin_enable(void) {
    const cJSON *p=admin_provider(); int ready=admin_connected()&&!admin.busy;
    EnableWindow(GetDlgItem(admin.window,903),ready&&admin.oauth_ok&&p&&strcmp(js(p,"flow"),"external")&&!*admin.session&&!admin.auth_busy);
    EnableWindow(GetDlgItem(admin.window,904),ready&&(*admin.session||admin.auth_busy));
    EnableWindow(GetDlgItem(admin.window,907),*admin.auth_url!=0);
    EnableWindow(GetDlgItem(admin.window,909),ready&&*admin.session&&!admin.auth_busy&&IsWindowEnabled(GetDlgItem(admin.window,908)));
    EnableWindow(GetDlgItem(admin.window,916),ready&&admin.env_ok);
    EnableWindow(GetDlgItem(admin.window,942),ready&&admin.endpoints_ok);
    EnableWindow(GetDlgItem(admin.window,953),ready&&admin.config_ok);
    EnableWindow(GetDlgItem(admin.window,956),ready&&admin.config_ok);
    EnableWindow(GetDlgItem(admin.window,957),ready&&admin.config_ok);
    EnableWindow(GetDlgItem(admin.window,961),ready&&admin.can_update);
    EnableWindow(GetDlgItem(admin.window,902),!*admin.session&&!admin.auth_busy);
    EnableWindow(GetDlgItem(admin.window,932),!admin.busy);
}
static void admin_provider_details(void) {
    const cJSON *p=admin_provider(); char text[800];
    snprintf(text,sizeof(text),"%s%s%s",jb(cJSON_GetObjectItemCaseSensitive(p,"status"),"logged_in")?"Connected on server.":"Not connected.",!strcmp(js(p,"flow"),"external")?" Server command: ":"",!strcmp(js(p,"flow"),"external")?js(p,"cli_command"):"");
    settext(GetDlgItem(admin.window,905),text); admin_enable();
}
static void admin_page(void) {
    int id; admin.page=TabCtrl_GetCurSel(GetDlgItem(admin.window,899));
    for(id=900;id<980;id++) { HWND h=GetDlgItem(admin.window,id); if(h) ShowWindow(h,(id<930?0:id<950?1:2)==admin.page?SW_SHOW:SW_HIDE); }
    admin_enable();
}
static void admin_cancel(void) {
    char path[400]; KillTimer(admin.window,1);
    if(*admin.session&&path_piece(admin.session)) { snprintf(path,sizeof(path),"/api/providers/oauth/sessions/%s",admin.session); admin_write("DELETE",path,NULL,AdminCancel); }
    admin.session[0]=admin.provider[0]=admin.auth_url[0]=0; admin.auth_cancelled=1;
    settext(GetDlgItem(admin.window,906),""); settext(GetDlgItem(admin.window,908),""); EnableWindow(GetDlgItem(admin.window,908),FALSE); admin_enable();
}
static void admin_endpoint_details(void) {
    const cJSON *p=admin_endpoint(); int mode=0; const char *m=js(p,"api_mode");
    settext(GetDlgItem(admin.window,934),js(p,"name")); settext(GetDlgItem(admin.window,936),js(p,"base_url")); settext(GetDlgItem(admin.window,938),js(p,"model")); settext(GetDlgItem(admin.window,940),"");
    if(!strcmp(m,"chat_completions")) mode=1; else if(!strcmp(m,"codex_responses")) mode=2; else if(!strcmp(m,"anthropic_messages")) mode=3;
    SendDlgItemMessageW(admin.window,941,CB_SETCURSEL,mode,0);
}
static void admin_folder_result(const char *error) {
    admin.busy=0; if(!admin.window) return;
    admin_note(*error?error:"Default workspace saved. Existing chats keep their folders.");
    if(!*error) settext(GetDlgItem(admin.window,952),app.server_cwd); admin_enable();
}
static void admin_result(NetMessage *m) {
    const cJSON *r=m->json,*p; char text[1400]; HWND w=admin.window;
    if(!w||(unsigned int)(UINT_PTR)m->context!=admin.serial||strcmp(m->url,admin.url)||strcmp(m->user,admin.user)) {
        /* Closing during /start must not leave a newly-created login flow active. */
        if(m->tag==AdminStart&&app.connected&&!strcmp(m->url,app.url)&&!strcmp(m->user,app.user)&&path_piece(js(r,"session_id"))) { snprintf(text,sizeof(text),"/api/providers/oauth/sessions/%s",js(r,"session_id")); net_write(app.net,"DELETE",text,NULL,AdminCancel,NULL); }
        return;
    }
    if(m->tag>=AdminSaveKey&&m->tag<=AdminSaveTitles) admin.busy=0;
    if(m->tag==AdminAction) admin.action_pending=0;
    if(m->tag==AdminStart||m->tag==AdminPoll||m->tag==AdminSubmit) admin.auth_busy=0;
    if(admin.auth_cancelled&&(m->tag==AdminStart||m->tag==AdminPoll||m->tag==AdminSubmit)) {
        if(m->tag==AdminStart&&path_piece(js(r,"session_id"))) { snprintf(text,sizeof(text),"/api/providers/oauth/sessions/%s",js(r,"session_id")); admin_write("DELETE",text,NULL,AdminCancel); }
        admin_enable(); return;
    }
    if(!r||*m->error) { admin_note(*m->error?m->error:"Server did not confirm the request."); if(m->tag==AdminPoll) KillTimer(w,1); if(m->tag==AdminApplyUpdate||m->tag==AdminAction) admin.busy=0; admin_enable(); return; }
    switch(m->tag) {
    case AdminEnv:
        SendDlgItemMessageW(w,914,CB_RESETCONTENT,0,0);
        cJSON_ArrayForEach(p,r) if(p->string&&(!strcmp(js(p,"category"),"model")||*js(p,"provider")||strstr(p->string,"API_KEY"))) { wchar_t *name=wide(p->string); SendDlgItemMessageW(w,914,CB_ADDSTRING,0,(LPARAM)name); free(name); }
        admin.env_ok=cJSON_IsObject(r); SendDlgItemMessageW(w,914,CB_SETCURSEL,0,0); break;
    case AdminOAuth:
        cJSON_Delete(admin.oauth); admin.oauth=cJSON_Duplicate(r,1); SendDlgItemMessageW(w,902,CB_RESETCONTENT,0,0);
        cJSON_ArrayForEach(p,cJSON_GetObjectItemCaseSensitive(r,"providers")) { wchar_t *name=wide(js(p,"name")); SendDlgItemMessageW(w,902,CB_ADDSTRING,0,(LPARAM)name); free(name); }
        admin.oauth_ok=cJSON_IsArray(cJSON_GetObjectItemCaseSensitive(r,"providers")); SendDlgItemMessageW(w,902,CB_SETCURSEL,0,0); admin_provider_details(); break;
    case AdminEndpoints:
        cJSON_Delete(admin.endpoints); admin.endpoints=cJSON_Duplicate(r,1); SendDlgItemMessageW(w,932,CB_RESETCONTENT,0,0); SendDlgItemMessageW(w,932,CB_ADDSTRING,0,(LPARAM)L"New endpoint");
        cJSON_ArrayForEach(p,cJSON_GetObjectItemCaseSensitive(r,"endpoints")) { wchar_t *name=wide(js(p,"name")); SendDlgItemMessageW(w,932,CB_ADDSTRING,0,(LPARAM)name); free(name); }
        admin.endpoints_ok=cJSON_IsArray(cJSON_GetObjectItemCaseSensitive(r,"endpoints")); SendDlgItemMessageW(w,932,CB_SETCURSEL,0,0); admin_endpoint_details(); break;
    case AdminConfig:
        admin.config_ok=cJSON_IsObject(r); settext(GetDlgItem(w,952),js(cJSON_GetObjectItemCaseSensitive(r,"terminal"),"cwd"));
        p=cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(r,"auxiliary"),"title_generation");
        SendDlgItemMessageW(w,955,BM_SETCHECK,!cJSON_HasObjectItem(p,"model_upgrade_enabled")||jb(p,"model_upgrade_enabled")?BST_CHECKED:BST_UNCHECKED,0); break;
    case AdminUpdate:
        admin.can_update=jb(r,"can_apply")&&jb(r,"update_available");
        snprintf(text,sizeof(text),"Hermes %s / %s\r\n%s",js(r,"current_version"),js(r,"install_method"),*js(r,"message")?js(r,"message"):jb(r,"update_available")?"An update is available.":"No update is reported."); settext(GetDlgItem(w,959),text); break;
    case AdminSaveKey: case AdminSaveEndpoint: case AdminSaveTitles:
        if(jb(r,"ok")) { admin_note(m->tag==AdminSaveKey?"Credential saved on the server. Choose its model from the model picker.":m->tag==AdminSaveEndpoint?"Endpoint saved. Choose it in the model picker; the default was not changed.":"Automatic title setting saved."); if(m->tag==AdminSaveEndpoint) admin_get("/api/providers/custom-endpoints",AdminEndpoints); if(m->tag==AdminSaveKey) admin_get("/api/env",AdminEnv); if(!app.modelwin) { cJSON_Delete(app.models); app.models=NULL; } }
        else admin_note("Server did not confirm the save. Refresh before retrying."); break;
    case AdminStart:
        if(!path_piece(js(r,"session_id"))) { admin_note("Server returned an invalid sign-in session."); break; }
        copytext(admin.session,sizeof(admin.session),js(r,"session_id"));
        copytext(admin.auth_url,sizeof(admin.auth_url),*js(r,"verification_url")?js(r,"verification_url"):js(r,"auth_url"));
        snprintf(text,sizeof(text),"%s%s%s",admin.auth_url,*js(r,"user_code")?"\r\nCode: ":"",js(r,"user_code")); settext(GetDlgItem(w,906),text);
        EnableWindow(GetDlgItem(w,908),!strcmp(js(r,"flow"),"pkce"));
        admin.poll_ms=(int)number(r,"poll_interval")*1000; if(admin.poll_ms<2000) admin.poll_ms=5000; if(admin.poll_ms>30000) admin.poll_ms=30000;
        { double seconds=number(r,"expires_in"); if(seconds<=0||seconds>3600) seconds=600; admin.expires=GetTickCount64()+(ULONGLONG)(seconds*1000); }
        SetTimer(w,1,(UINT)admin.poll_ms,NULL); admin_note("Open sign-in on this PC. Hermes stores the completed login on the server."); break;
    case AdminSubmit: case AdminPoll:
        if(!strcmp(js(r,"status"),"approved")) { KillTimer(w,1); admin.session[0]=admin.provider[0]=admin.auth_url[0]=0; settext(GetDlgItem(w,906),""); settext(GetDlgItem(w,908),""); EnableWindow(GetDlgItem(w,908),FALSE); admin_note("Sign-in confirmed by the server."); admin_get("/api/providers/oauth",AdminOAuth); if(!app.modelwin) { cJSON_Delete(app.models); app.models=NULL; } }
        else if(strcmp(js(r,"status"),"pending")) { snprintf(text,sizeof(text),"Sign-in %s. %s",js(r,"status"),js(r,"error_message")); admin_cancel(); admin_note(text); }
        break;
    case AdminApplyUpdate:
        admin.busy=0; if(jb(r,"ok")&&path_piece(js(r,"name"))) { copytext(admin.action,sizeof(admin.action),js(r,"name")); admin.busy=1; SetTimer(w,2,3000,NULL); admin_note("Update started on server. Waiting for completion."); } else admin_note(*js(r,"message")?js(r,"message"):"Update was not started."); break;
    case AdminAction:
        if(!jb(r,"running")) { const cJSON *exitcode=cJSON_GetObjectItemCaseSensitive(r,"exit_code"); KillTimer(w,2); admin.busy=0; admin_note(cJSON_IsNumber(exitcode)&&exitcode->valueint==0?"Server update command completed. Reconnect and check the version.":"Server update did not confirm success. Check the server logs."); } break;
    default: break;
    } admin_enable();
}
static HWND admin_control(HWND w,const wchar_t *class_name,const wchar_t *text,DWORD style,int id,int x,int y,int width,int height) { HWND h=child(w,class_name,text,style,id); move(h,x,y,width,height); return h; }
static LRESULT CALLBACK admin_proc(HWND w,UINT msg,WPARAM wp,LPARAM lp) {
    switch(msg) {
    case WM_CREATE: {
        TCITEMW tab={0}; HWND h; admin.window=w;
        h=admin_control(w,WC_TABCONTROLW,L"",WS_TABSTOP,899,16,12,642,28); tab.mask=TCIF_TEXT;
        tab.pszText=L"Accounts and keys"; TabCtrl_InsertItem(h,0,&tab); tab.pszText=L"Custom endpoint"; TabCtrl_InsertItem(h,1,&tab); tab.pszText=L"Server"; TabCtrl_InsertItem(h,2,&tab);
        admin_control(w,L"STATIC",L"Provider account",0,900,24,56,600,22);
        admin_control(w,L"COMBOBOX",L"",WS_TABSTOP|CBS_DROPDOWNLIST|WS_VSCROLL,902,24,84,412,250);
        admin_control(w,L"BUTTON",L"Sign in...",WS_TABSTOP|BS_OWNERDRAW,903,446,84,96,27); admin_control(w,L"BUTTON",L"Cancel login",WS_TABSTOP|BS_OWNERDRAW,904,550,84,100,27);
        admin_control(w,L"STATIC",L"Reading provider support...",0,905,24,122,626,44);
        admin_control(w,L"EDIT",L"",ES_MULTILINE|ES_READONLY|WS_TABSTOP,906,24,174,626,58);
        admin_control(w,L"BUTTON",L"Open sign-in",WS_TABSTOP|BS_OWNERDRAW,907,24,244,128,28);
        admin_control(w,L"EDIT",L"",WS_TABSTOP|ES_AUTOHSCROLL|ES_PASSWORD,908,164,244,368,28); SendDlgItemMessageW(w,908,EM_SETCUEBANNER,0,(LPARAM)L"Return URL / code, if requested"); EnableWindow(GetDlgItem(w,908),FALSE);
        admin_control(w,L"BUTTON",L"Complete",WS_TABSTOP|BS_OWNERDRAW,909,542,244,108,28);
        admin_control(w,L"STATIC",L"API key / server environment",0,912,24,310,610,22);
        admin_control(w,L"COMBOBOX",L"",WS_TABSTOP|CBS_DROPDOWN|CBS_AUTOHSCROLL|WS_VSCROLL,914,24,340,626,240);
        admin_control(w,L"EDIT",L"",WS_TABSTOP|ES_AUTOHSCROLL|ES_PASSWORD,915,24,380,474,28); SendDlgItemMessageW(w,915,EM_SETCUEBANNER,0,(LPARAM)L"Paste a new key; saved keys are never displayed");
        admin_control(w,L"BUTTON",L"Save key",WS_TABSTOP|BS_OWNERDRAW,916,510,380,140,28);
        admin_control(w,L"STATIC",L"Keys and OAuth tokens live on Hermes. LCB does not save provider secrets.\r\nExternal CLI sign-ins show the required server command above.",0,917,24,432,626,50);
        admin_control(w,L"STATIC",L"OpenAI / Responses / Anthropic compatible server",0,930,24,56,610,22);
        admin_control(w,L"COMBOBOX",L"",WS_TABSTOP|CBS_DROPDOWNLIST|WS_VSCROLL,932,24,84,626,240);
        admin_control(w,L"STATIC",L"Name",0,933,24,132,130,22); admin_control(w,L"EDIT",L"",WS_TABSTOP|ES_AUTOHSCROLL,934,164,128,486,28);
        admin_control(w,L"STATIC",L"Base URL",0,935,24,174,130,22); admin_control(w,L"EDIT",L"",WS_TABSTOP|ES_AUTOHSCROLL,936,164,170,486,28);
        admin_control(w,L"STATIC",L"Model ID",0,937,24,216,130,22); admin_control(w,L"EDIT",L"",WS_TABSTOP|ES_AUTOHSCROLL,938,164,212,486,28);
        admin_control(w,L"STATIC",L"API key (optional)",0,939,24,258,134,22); admin_control(w,L"EDIT",L"",WS_TABSTOP|ES_AUTOHSCROLL|ES_PASSWORD,940,164,254,486,28);
        h=admin_control(w,L"COMBOBOX",L"",WS_TABSTOP|CBS_DROPDOWNLIST,941,164,296,486,180);
        SendMessageW(h,CB_ADDSTRING,0,(LPARAM)L"Automatic transport"); SendMessageW(h,CB_ADDSTRING,0,(LPARAM)L"Chat Completions"); SendMessageW(h,CB_ADDSTRING,0,(LPARAM)L"Responses"); SendMessageW(h,CB_ADDSTRING,0,(LPARAM)L"Anthropic Messages"); SendMessageW(h,CB_SETCURSEL,0,0);
        admin_control(w,L"BUTTON",L"Save endpoint",WS_TABSTOP|BS_OWNERDRAW,942,490,348,160,29);
        admin_control(w,L"STATIC",L"Blank API key preserves an existing key. Model discovery stays enabled.\r\nSaving adds the endpoint to Hermes; choose its model separately.",0,943,24,402,626,60);
        admin_control(w,L"STATIC",L"Default working folder on the server",0,950,24,56,610,22);
        admin_control(w,L"EDIT",L"",WS_TABSTOP|ES_AUTOHSCROLL,952,24,86,460,28); admin_control(w,L"BUTTON",L"Save folder",WS_TABSTOP|BS_OWNERDRAW,953,496,86,154,28);
        admin_control(w,L"STATIC",L"This is a folder inside Hermes, not your Windows Downloads folder.\r\nNew default chats use it. Existing chats and files are not moved.",0,954,24,126,626,48);
        admin_control(w,L"BUTTON",L"Improve titles with a model",WS_TABSTOP|BS_AUTOCHECKBOX,955,24,202,292,26);
        admin_control(w,L"BUTTON",L"Save setting",WS_TABSTOP|BS_OWNERDRAW,956,330,200,154,28); admin_control(w,L"BUTTON",L"Title model...",WS_TABSTOP|BS_OWNERDRAW,957,496,200,154,28);
        admin_control(w,L"STATIC",L"Simple titles from your first message stay available when this is off.\r\nModel titles use a separate request from the chat's reasoning setting.",0,958,24,242,626,44);
        admin_control(w,L"STATIC",L"Check the server's installation and update support.",0,959,24,318,626,82);
        admin_control(w,L"BUTTON",L"Check update",WS_TABSTOP|BS_OWNERDRAW,960,330,414,154,28); admin_control(w,L"BUTTON",L"Update Hermes...",WS_TABSTOP|BS_OWNERDRAW,961,496,414,154,28);
        admin_control(w,L"STATIC",L"",0,990,24,494,626,54); admin_control(w,L"BUTTON",L"Close",WS_TABSTOP|BS_OWNERDRAW,IDCANCEL,546,560,104,28);
        admin_page(); return 0;
    }
    case WM_NOTIFY: if(((NMHDR *)lp)->idFrom==899&&((NMHDR *)lp)->code==TCN_SELCHANGE) admin_page(); return 0;
    case WM_COMMAND: {
        int id=LOWORD(wp); if(id==IDCANCEL) { admin_close(); return 0; }
        if(id==902&&HIWORD(wp)==CBN_SELCHANGE) { admin_provider_details(); return 0; }
        if(id==932&&HIWORD(wp)==CBN_SELCHANGE) { admin_endpoint_details(); return 0; }
        if(id==907) { if(!strncmp(admin.auth_url,"https://",8)) { wchar_t *url=wide(admin.auth_url); if(url) ShellExecuteW(w,L"open",url,NULL,NULL,SW_SHOWNORMAL); free(url); } else admin_note("Server did not provide an HTTPS sign-in URL."); return 0; }
        if(!admin_connected()||admin.busy) return 0;
        if(id==904) { admin_cancel(); admin_note("Login cancelled."); }
        else if(id==903&&!admin.auth_busy&&!*admin.session) {
            const cJSON *p=admin_provider(); char path[400]; if(!p||!path_piece(js(p,"id"))||!strcmp(js(p,"flow"),"external")) return 0;
            admin.auth_cancelled=0; copytext(admin.provider,sizeof(admin.provider),js(p,"id")); snprintf(path,sizeof(path),"/api/providers/oauth/%s/start",admin.provider); admin.auth_busy=admin_write("POST",path,NULL,AdminStart); admin_note("Starting provider sign-in...");
        } else if(id==909&&*admin.session&&!admin.auth_busy) {
            char *code=gettext(GetDlgItem(w,908)),path[400]; cJSON *body=params(); if(*code) { put(body,"session_id",admin.session); put(body,"code",code); snprintf(path,sizeof(path),"/api/providers/oauth/%s/submit",admin.provider); admin.auth_busy=admin_write("POST",path,body,AdminSubmit); } secret_free(code); secret_json_free(body); SetDlgItemTextW(w,908,L"");
        } else if(id==916&&admin.env_ok) {
            char *key=gettext(GetDlgItem(w,914)),*value=gettext(GetDlgItem(w,915)); cJSON *body=params(); int valid=*key&&strlen(key)<160;
            for(const char *p=key;*p;p++) if(!((*p>='A'&&*p<='Z')||(*p>='0'&&*p<='9')||*p=='_')) valid=0;
            if(!valid||!*value) admin_note("Choose an environment variable and paste a new key.");
            else { put(body,"key",key); put(body,"value",value); cJSON_AddBoolToObject(body,"provider_setup",1); admin.busy=admin_write("PUT","/api/env",body,AdminSaveKey); if(admin.busy) { SetDlgItemTextW(w,915,L""); admin_note("Saving credential on the server..."); } }
            secret_free(value); free(key); secret_json_free(body);
        } else if(id==942&&admin.endpoints_ok) {
            static const char *modes[]={"","chat_completions","codex_responses","anthropic_messages"};
            char *name=gettext(GetDlgItem(w,934)),*url=gettext(GetDlgItem(w,936)),*model=gettext(GetDlgItem(w,938)),*key=gettext(GetDlgItem(w,940)); cJSON *body=params(); const cJSON *p=admin_endpoint(); LRESULT mode=SendDlgItemMessageW(w,941,CB_GETCURSEL,0,0);
            if(!*name||!*model||(strncmp(url,"https://",8)&&strncmp(url,"http://",7))) admin_note("Enter a name, HTTP(S) endpoint URL and model ID.");
            else { if(p) put(body,"id",js(p,"id")); put(body,"name",name); put(body,"base_url",url); put(body,"model",model); if(*key) put(body,"api_key",key); if(mode>=0&&mode<4) put(body,"api_mode",modes[mode]); cJSON_AddBoolToObject(body,"make_default",0); cJSON_AddBoolToObject(body,"discover_models",1); admin.busy=admin_write("POST","/api/providers/custom-endpoints",body,AdminSaveEndpoint); if(admin.busy) { SetDlgItemTextW(w,940,L""); admin_note("Saving endpoint on the server..."); } }
            free(name); free(url); free(model); secret_free(key); secret_json_free(body);
        } else if(id==953&&admin.config_ok) { char *folder=gettext(GetDlgItem(w,952)); cJSON *p=params(); if(*folder) { put(p,"key","terminal.cwd"); put(p,"value",folder); admin.busy=rpc("config.set",p,RpcDefaultFolder,NULL)!=0; admin_note("Checking and saving the server folder..."); } else { cJSON_Delete(p); admin_note("Enter an existing folder on the server."); } free(folder);
        } else if(id==956&&admin.config_ok) { cJSON *p=params(),*cfg=params(),*aux=params(),*title=params(); cJSON_AddBoolToObject(title,"model_upgrade_enabled",SendDlgItemMessageW(w,955,BM_GETCHECK,0,0)==BST_CHECKED); cJSON_AddItemToObject(aux,"title_generation",title); cJSON_AddItemToObject(cfg,"auxiliary",aux); cJSON_AddItemToObject(p,"config",cfg); admin.busy=admin_write("PUT","/api/config",p,AdminSaveTitles); cJSON_Delete(p);
        } else if(id==957) SendMessageW(app.window,WM_COMMAND,IdTitleModel,0);
        else if(id==960) { admin.can_update=0; settext(GetDlgItem(w,959),"Checking installation and updates..."); admin_get("/api/hermes/update/check?force=true",AdminUpdate); }
        else if(id==961&&admin.can_update&&MessageBoxW(w,L"Apply the server's available Hermes update? The connection may close during restart.",L"Update Hermes",MB_OKCANCEL|MB_DEFBUTTON2|MB_ICONQUESTION)==IDOK) { admin.busy=admin_write("POST","/api/hermes/update",NULL,AdminApplyUpdate); }
        admin_enable(); return 0;
    }
    case WM_TIMER:
        if(wp==1&&*admin.session&&!admin.auth_busy&&admin_connected()) { char path[500]; if(GetTickCount64()>=admin.expires) { admin_cancel(); admin_note("Sign-in expired. Start again when ready."); } else { snprintf(path,sizeof(path),"/api/providers/oauth/%s/poll/%s",admin.provider,admin.session); admin.auth_busy=1; admin_get(path,AdminPoll); } }
        else if(wp==2&&*admin.action&&admin_connected()&&!admin.action_pending) { char path[400]; snprintf(path,sizeof(path),"/api/actions/%s/status?lines=0",admin.action); admin.action_pending=1; admin_get(path,AdminAction); } return 0;
    case WM_DRAWITEM: draw_button((DRAWITEMSTRUCT *)lp); return TRUE;
    case WM_CLOSE: admin_close(); return 0;
    case WM_DESTROY: admin.window=NULL; cJSON_Delete(admin.oauth); admin.oauth=NULL; cJSON_Delete(admin.endpoints); admin.endpoints=NULL; return 0;
    case WM_CTLCOLORSTATIC: case WM_CTLCOLOREDIT: case WM_CTLCOLORLISTBOX: return color_control(msg,wp,lp);
    case WM_ERASEBKGND: { RECT r; GetClientRect(w,&r); FillRect((HDC)wp,&r,app.face); return 1; }
    } return DefWindowProcW(w,msg,wp,lp);
}
static void admin_close(void) { if(admin.window) { admin_cancel(); DestroyWindow(admin.window); } }
static void show_admin(void) {
    WNDCLASSW wc={0}; RECT r; unsigned int serial=admin.serial+1;
    if(admin.window) { SetForegroundWindow(admin.window); return; } if(!app.connected) { notice("Connect to a server first."); return; }
    memset(&admin,0,sizeof(admin)); admin.serial=serial; copytext(admin.url,sizeof(admin.url),app.url); copytext(admin.user,sizeof(admin.user),app.user);
    wc.hInstance=app.instance; wc.lpfnWndProc=admin_proc; wc.lpszClassName=L"LCBHermesSettings"; wc.hCursor=LoadCursorW(NULL,IDC_ARROW); wc.hIcon=LoadIconW(app.instance,MAKEINTRESOURCEW(101)); RegisterClassW(&wc);
    GetWindowRect(app.window,&r); admin.window=CreateWindowExW(WS_EX_DLGMODALFRAME,wc.lpszClassName,L"Hermes server settings",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU,r.left+px(50),r.top+px(35),px(694),px(641),app.window,NULL,app.instance,NULL);
    if(!admin.window) return; ShowWindow(admin.window,SW_SHOW); admin_note(app.url);
    admin_get("/api/env",AdminEnv); admin_get("/api/providers/oauth",AdminOAuth); admin_get("/api/providers/custom-endpoints",AdminEndpoints); admin_get("/api/config",AdminConfig); admin_get("/api/hermes/update/check",AdminUpdate);
}
