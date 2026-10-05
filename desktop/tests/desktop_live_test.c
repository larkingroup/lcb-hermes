#define wWinMain hermes_entry
#include "../src/winmain.c"
#undef wWinMain
#include <assert.h>
static void pump(void) {
    MSG msg;
    while(PeekMessageW(&msg,NULL,0,0,PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    MsgWaitForMultipleObjects(0,NULL,FALSE,50,QS_ALLINPUT);
}
int main(int argc,char **argv) {
    char fixture[8192]; cJSON *access; wchar_t dir[MAX_PATH]; ULONGLONG start; Chat *work,*draft; Upload *upload; int count;
    setvbuf(stdout,NULL,_IONBF,0); assert(argc==2); assert(fgets(fixture,sizeof(fixture),stdin)); access=cJSON_Parse(fixture); assert(access);
    app.settings=cJSON_CreateObject(); GetTempPathW(MAX_PATH,dir); GetTempFileNameW(dir,L"lcg",0,app.path);
    assert(create_window(GetModuleHandleW(NULL),SW_HIDE)); profile_load("",""); assert(!*app.url&&!*app.user&&!*app.cookie);
    app.serverwin=CreateWindowExW(WS_EX_DLGMODALFRAME,L"LCBHermesServer",L"server",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU,0,0,px(510),px(566),app.window,NULL,app.instance,NULL);
    assert(app.serverwin); settext(GetDlgItem(app.serverwin,502),js(access,"url")); settext(GetDlgItem(app.serverwin,503),js(access,"username")); settext(GetDlgItem(app.serverwin,504),js(access,"password"));
    SendMessageW(GetDlgItem(app.serverwin,506),BM_SETCHECK,BST_CHECKED,0); SendMessageW(app.serverwin,WM_COMMAND,505,0);
    SecureZeroMemory(fixture,sizeof(fixture)); { cJSON *password=cJSON_GetObjectItemCaseSensitive(access,"password"); SecureZeroMemory(password->valuestring,strlen(password->valuestring)); } cJSON_Delete(access);
    start=GetTickCount64(); while(!app.connected&&GetTickCount64()-start<40000) pump(); assert(app.connected); assert(jb(app.profile,"http")); assert(GetWindowTextLengthW(GetDlgItem(app.serverwin,504))==0);
    puts("desktop server controls + fresh defaults + login: OK"); DestroyWindow(app.serverwin); newchat(); work=app.current;
    upload=calloc(1,sizeof(*upload)); assert(upload); upload->chat=work; { wchar_t *path=wide(argv[1]); wcscpy(upload->path,path); free(path); }
    app.uploads++; net_upload(app.net,upload->path,RestUpload,upload); start=GetTickCount64(); while(app.uploads&&GetTickCount64()-start<40000) pump();
    assert(!app.uploads); assert(cJSON_GetArraySize(work->attachments)==1); render(); assert(app.npreviews==1); puts("desktop image upload + inline thumbnail: OK");
    SetWindowTextW(app.prompt,L"Briefly describe the attached image. End with LCB_UI_OK."); SendMessageW(app.window,WM_COMMAND,MAKEWPARAM(IdPrompt,EN_CHANGE),(LPARAM)app.prompt); SendMessageW(app.window,WM_COMMAND,IdSend,0);
    assert(work->pending); newchat(); draft=app.current; SetWindowTextW(app.prompt,L"keep this separate draft"); SendMessageW(app.window,WM_COMMAND,MAKEWPARAM(IdPrompt,EN_CHANGE),(LPARAM)app.prompt);
    app.serverwin=CreateWindowExW(0,L"LCBHermesServer",L"server",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU,0,0,px(510),px(566),app.window,NULL,app.instance,NULL); DestroyWindow(app.serverwin); assert(app.current==draft);
    start=GetTickCount64(); while((work->pending||work->running)&&GetTickCount64()-start<180000) pump();
    if(!work->unread) fprintf(stderr,"Submit check: %s / %d messages / %d requests\n",app.notice,cJSON_GetArraySize(work->messages),cJSON_GetArraySize(work->requests));
    assert(!work->pending&&!work->running); assert(app.current==draft); assert(!strcmp(draft->draft,"keep this separate draft")); assert(work->unread); assert(*work->stored);
    count=cJSON_GetArraySize(work->messages); assert(count>=2); { char *text=row_text(cJSON_GetArrayItem(work->messages,count-1)); assert(strstr(text,"LCB_UI_OK")); free(text); }
    puts("desktop submit + image understanding + background chat + server return: OK");
    resume(work); start=GetTickCount64(); while(work->pending&&GetTickCount64()-start<20000) pump(); assert(!work->pending); assert(cJSON_GetArraySize(work->messages)>=2); assert(app.current==draft);
    assert(cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(work->messages,0),"attachments"))==1); puts("desktop saved chat resume + image metadata: OK");
    net_rest(app.net,"/api/system/stats",RestStats,NULL); start=GetTickCount64(); while(!app.stats&&GetTickCount64()-start<20000) pump(); assert(app.stats); puts("desktop authenticated server stats: OK");
    save(); { cJSON *saved=store_read(app.path); assert(saved); assert(*js(saved,"active_url")); cJSON_Delete(saved); }
    { char key[65]; copytext(key,sizeof(key),work->key); work->pending=1; rpc("session.close",sessionparams(work),RpcCloseDelete,work); start=GetTickCount64(); while(findchat(key)&&GetTickCount64()-start<20000) pump(); assert(!findchat(key)); assert(app.current==draft); puts("desktop delete of its own test chat + selection: OK"); }
    DeleteFileW(app.path); DestroyWindow(app.window); cleanup(); puts("LIVE_DESKTOP_UI_OK"); return 0;
}
