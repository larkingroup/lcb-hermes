#define UNICODE
#define _UNICODE
#include "transport_win.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static Net *net; static int stage,done,failed,browser; static char session[160],stored[160];
static void sendrpc(int id,const char *method,cJSON *p) { cJSON *m=cJSON_CreateObject(); cJSON_AddStringToObject(m,"jsonrpc","2.0"); cJSON_AddNumberToObject(m,"id",id); cJSON_AddStringToObject(m,"method",method); cJSON_AddItemToObject(m,"params",p); assert(net_send(net,m)); cJSON_Delete(m); }
static cJSON *sessionparam(void) { cJSON *p=cJSON_CreateObject(); cJSON_AddStringToObject(p,"session_id",session); return p; }
static LRESULT CALLBACK proc(HWND w,UINT msg,WPARAM wp,LPARAM lp) {
    (void)wp;
    if(msg==NET_MESSAGE) { NetMessage *m=(NetMessage *)lp;
        if(m->kind==NetOffline) { fprintf(stderr,"%s\n",m->error); failed=done=1; }
        if(m->kind==NetOnline) { cJSON *p=cJSON_CreateObject(); cJSON_AddBoolToObject(p,"server_requests",1); sendrpc(1,"client.capabilities",p); puts("native C login + WebSocket: OK"); }
        if(m->kind==NetFrame) {
            const cJSON *r=cJSON_GetObjectItemCaseSensitive(m->json,"result"),*id=cJSON_GetObjectItemCaseSensitive(m->json,"id"),*e=cJSON_GetObjectItemCaseSensitive(m->json,"error");
            if(e) { fprintf(stderr,"RPC failed: %s\n",js(e,"message")); failed=done=1; }
            if(cJSON_IsNumber(id)&&!e) {
                if(id->valueint==1) { cJSON *p=cJSON_CreateObject(); cJSON_AddStringToObject(p,"source","desktop"); cJSON_AddBoolToObject(p,"close_on_disconnect",0); sendrpc(2,"session.create",p); }
                if(id->valueint==2) { cJSON *p; const cJSON *info=cJSON_GetObjectItemCaseSensitive(r,"info"); snprintf(session,sizeof(session),"%s",js(r,"session_id")); snprintf(stored,sizeof(stored),"%s",js(info,"stored_session_id")); assert(*session); p=sessionparam(); cJSON_AddStringToObject(p,"text","Use the local browser to open https://example.com and report the page title. End with LCB_PC_OK."); cJSON_AddStringToObject(p,"surface","desktop"); sendrpc(3,"prompt.submit",p); }
                if(id->valueint==4) { assert(cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(r,"messages"))>=2); puts("native C saved chat resume: OK"); net_rest(net,"/api/system/stats",1,NULL); }
            }
            if(!strcmp(js(m->json,"method"),"event")) { const cJSON *event=cJSON_GetObjectItemCaseSensitive(m->json,"params"),*p=cJSON_GetObjectItemCaseSensitive(event,"payload");
                if(!strcmp(js(event,"session_id"),session)) {
                    if(!strcmp(js(event,"type"),"session.info")&&*js(p,"stored_session_id")) snprintf(stored,sizeof(stored),"%s",js(p,"stored_session_id"));
                    if(!strcmp(js(event,"type"),"tool.start") && strstr(js(p,"name"),"browser")) browser=1;
                    if(!strcmp(js(event,"type"),"message.complete")&&stage==0) { cJSON *next=cJSON_CreateObject(); assert(strstr(js(p,"text"),"LCB_PC_OK")); assert(browser); stage=1; puts("native C streaming reply + browser: OK"); cJSON_AddStringToObject(next,"session_id",*stored?stored:session); cJSON_AddStringToObject(next,"source","desktop"); cJSON_AddBoolToObject(next,"close_on_disconnect",0); sendrpc(4,"session.resume",next); }
                }
            }
        }
        if(m->kind==NetRest) { if(*m->error) { fprintf(stderr,"%s\n",m->error); failed=1; } else puts("native C authenticated stats: OK"); done=1; }
        cJSON_Delete(m->json); SecureZeroMemory(m->cookie,sizeof(m->cookie)); free(m); return 0;
    }
    return DefWindowProcW(w,msg,wp,lp);
}
int main(void) {
    char fixture[8192]; cJSON *access; WNDCLASSW wc={0}; HWND window; MSG msg; ULONGLONG start;
    /* Credentials enter via stdin, never command-line arguments or a tracked fixture. */
    setvbuf(stdout,NULL,_IONBF,0); assert(fgets(fixture,sizeof(fixture),stdin)); access=cJSON_Parse(fixture); assert(access); wc.hInstance=GetModuleHandleW(NULL); wc.lpfnWndProc=proc; wc.lpszClassName=L"LCBHermesLiveTest"; RegisterClassW(&wc); window=CreateWindowW(wc.lpszClassName,L"",0,0,0,0,0,HWND_MESSAGE,NULL,wc.hInstance,NULL); assert(window); net=net_new(window);
    net_connect(net,js(access,"url"),js(access,"username"),js(access,"password"),1,""); SecureZeroMemory(fixture,sizeof(fixture)); cJSON_Delete(access); start=GetTickCount64();
    while(!done&&GetTickCount64()-start<240000) { while(PeekMessageW(&msg,NULL,0,0,PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); } MsgWaitForMultipleObjects(0,NULL,FALSE,100,QS_ALLINPUT); }
    net_free(net); DestroyWindow(window); assert(done&&!failed); puts("LIVE_NATIVE_C_OK"); return 0;
}
