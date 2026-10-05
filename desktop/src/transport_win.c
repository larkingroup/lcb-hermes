#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <objbase.h>
#include <winhttp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "transport_win.h"

typedef struct Job {
    int kind, tag, allow;
    char *url, *user, *password, *cookie, *path, *wire;
    wchar_t *file;
    void *context;
    struct Job *next;
} Job;
struct Net {
    HWND target;
    CRITICAL_SECTION lock;
    HANDLE work, stop, thread, reader;
    Job *first, *last;
    HINTERNET session, connection, socket, request;
    char url[1024], user[256], cookie[8192];
    wchar_t host[256], connecthost[256];
    INTERNET_PORT port;
    int secure, closing;
};
wchar_t *wide(const char *s) {
    int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s,-1,NULL,0); wchar_t *w;
    if(!n) return NULL; w=malloc((size_t)n*sizeof(*w)); if(w) MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s,-1,w,n); return w;
}
char *utf8(const wchar_t *w) {
    int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,w,-1,NULL,0,NULL,NULL); char *s;
    if(!n) return NULL; s=malloc((size_t)n); if(s) WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,w,-1,s,n,NULL,NULL); return s;
}
static int private_address(const SOCKADDR *a) {
    if(a->sa_family==AF_INET) { unsigned long v=ntohl(((const SOCKADDR_IN *)a)->sin_addr.s_addr);
        return (v>>24)==10 || (v>>24)==127 || (v>>20)==0xac1 || (v>>16)==0xc0a8 || (v>>22)==0x191 || (v>>16)==0xa9fe;
    }
    if(a->sa_family==AF_INET6) { const unsigned char *p=((const SOCKADDR_IN6 *)a)->sin6_addr.s6_addr;
        static const unsigned char loop[16]={0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1};
        if(!memcmp(p,loop,16) || (p[0]&0xfe)==0xfc || (p[0]==0xfe&&(p[1]&0xc0)==0x80)) return 1;
        if(!memcmp(p,"\0\0\0\0\0\0\0\0\0\0\xff\xff",12)) { SOCKADDR_IN v={0}; v.sin_family=AF_INET; memcpy(&v.sin_addr,p+12,4); return private_address((SOCKADDR *)&v); }
    }
    return 0;
}
static int endpoint(Net *n, const char *url, int allow, char *error, size_t size) {
    URL_COMPONENTSW u={0}; wchar_t *w=wide(url); size_t len; int ok=0;
    if(!w) goto invalid; len=wcslen(w); u.dwStructSize=sizeof(u);
    u.dwHostNameLength=u.dwUrlPathLength=u.dwUserNameLength=u.dwPasswordLength=u.dwExtraInfoLength=(DWORD)-1;
    if(!WinHttpCrackUrl(w,0,0,&u)||u.dwHostNameLength>=256||!u.dwHostNameLength||u.dwUserNameLength||u.dwPasswordLength||u.dwExtraInfoLength) goto invalid;
    if(u.dwUrlPathLength && !(u.dwUrlPathLength==1 && u.lpszUrlPath[0]==L'/')) goto invalid;
    if(u.nScheme!=INTERNET_SCHEME_HTTP && u.nScheme!=INTERNET_SCHEME_HTTPS) goto invalid;
    n->secure=u.nScheme==INTERNET_SCHEME_HTTPS; n->port=u.nPort;
    memcpy(n->host,u.lpszHostName,u.dwHostNameLength*sizeof(wchar_t)); n->host[u.dwHostNameLength]=0; wcscpy(n->connecthost,n->host);
    if(!n->secure) {
        ADDRINFOW hints={0}, *addresses=NULL, *a; int first=1;
        if(!allow) { snprintf(error,size,"Check private HTTP for a trusted private server."); goto done; }
        hints.ai_socktype=SOCK_STREAM; hints.ai_family=AF_UNSPEC;
        if(GetAddrInfoW(n->host,NULL,&hints,&addresses)) { snprintf(error,size,"Could not resolve the server."); goto done; }
        for(a=addresses;a;a=a->ai_next) {
            if(!private_address(a->ai_addr)) { FreeAddrInfoW(addresses); snprintf(error,size,"HTTP is limited to private network addresses."); goto done; }
            if(first) { DWORD cap=256; SOCKADDR_STORAGE addr; memcpy(&addr,a->ai_addr,a->ai_addrlen);
                if(WSAAddressToStringW((SOCKADDR *)&addr,(DWORD)a->ai_addrlen,NULL,n->connecthost,&cap)) { FreeAddrInfoW(addresses); goto invalid; }
                if(n->connecthost[0]==L'[') { wchar_t *end=wcschr(n->connecthost,L']'); if(end) { *end=0; memmove(n->connecthost,n->connecthost+1,wcslen(n->connecthost)*sizeof(wchar_t)); } }
                first=0;
            }
        }
        FreeAddrInfoW(addresses); if(first) goto invalid;
    }
    while(len && w[len-1]==L'/') w[--len]=0;
    snprintf(n->url,sizeof(n->url),"%s",url); len=strlen(n->url); while(len&&n->url[len-1]=='/') n->url[--len]=0;
    ok=1; goto done;
invalid:
    snprintf(error,size,"Use a server URL like https://hermes.example.com:7777, without a path.");
done:
    free(w); return ok;
}
int endpoint_valid(const char *url,int allow,char *error,size_t size) {
    Net n={0}; WSADATA data; int ok; WSAStartup(MAKEWORD(2,2),&data); ok=endpoint(&n,url,allow,error,size); WSACleanup(); return ok;
}
static void deliver(Net *n, NetMessage *m) { if(!PostMessageW(n->target,NET_MESSAGE,0,(LPARAM)m)) { cJSON_Delete(m->json); free(m); } }
static NetMessage *message(int kind) { NetMessage *m=calloc(1,sizeof(*m)); if(m) m->kind=kind; return m; }
static void failure(Net *n,const char *text) { NetMessage *m=message(NetOffline); if(m) { snprintf(m->error,sizeof(m->error),"%s",text); deliver(n,m); } }
static void winerror(char *error,size_t size,DWORD code) {
    if(code==ERROR_WINHTTP_SECURE_FAILURE) snprintf(error,size,"TLS verification failed. Check the server certificate.");
    else snprintf(error,size,"Connection failed (%lu). Check the server and network.",(unsigned long)code);
}
static void cookies(Net *n,HINTERNET req) {
    DWORD index=0, length; wchar_t value[8192];
    while(1) {
        char *line, *semi; size_t used, part; length=sizeof(value);
        if(!WinHttpQueryHeaders(req,WINHTTP_QUERY_SET_COOKIE,NULL,value,&length,&index)) break;
        line=utf8(value); if(!line) continue; semi=strchr(line,';'); if(semi) *semi=0;
        if(strchr(line,'\r')||strchr(line,'\n')) { free(line); continue; }
        used=strlen(n->cookie); part=strlen(line);
        /* The auth cookie names are stable; retain only the newest value for a name. */
        { char *equals=strchr(line,'='); if(equals) {
            char rebuilt[8192]={0}, old[8192], *cursor, *next; size_t name=(size_t)(equals-line);
            snprintf(old,sizeof(old),"%s",n->cookie); cursor=old;
            while(cursor&&*cursor) { next=strstr(cursor,"; "); if(next) { *next=0; next+=2; }
                if(strncmp(cursor,line,name)||cursor[name]!='=') { if(*rebuilt) strcat(rebuilt,"; "); if(strlen(rebuilt)+strlen(cursor)<sizeof(rebuilt)) strcat(rebuilt,cursor); }
                cursor=next;
            }
            snprintf(n->cookie,sizeof(n->cookie),"%s",rebuilt); used=strlen(n->cookie);
        } }
        if(used+part+3<sizeof(n->cookie)) { if(used) strcat(n->cookie,"; "); strcat(n->cookie,line); } free(line);
    }
}
static void close_request(Net *n,HINTERNET req) {
    EnterCriticalSection(&n->lock);
    if(req && n->request==req) { n->request=NULL; WinHttpCloseHandle(req); }
    LeaveCriticalSection(&n->lock);
}
static HINTERNET open_request(Net *n,const wchar_t *verb,const wchar_t *path,const wchar_t *type,int upgrade,const void *body,DWORD size,char *error,size_t cap,DWORD *status) {
    HINTERNET req; wchar_t *origin=wide(n->url), *cookie=wide(n->cookie), headers[12000]; DWORD option=WINHTTP_DISABLE_REDIRECTS|WINHTTP_DISABLE_COOKIES;
    req=WinHttpOpenRequest(n->connection,verb,path,NULL,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,n->secure?WINHTTP_FLAG_SECURE:0);
    if(!req) goto failed;
    EnterCriticalSection(&n->lock); n->request=req; LeaveCriticalSection(&n->lock);
    if(WaitForSingleObject(n->stop,0)==WAIT_OBJECT_0) goto failed;
    WinHttpSetOption(req,WINHTTP_OPTION_DISABLE_FEATURE,&option,sizeof(option));
    swprintf(headers,12000,L"Origin: %ls\r\nHost: %ls:%u\r\n%ls%ls%ls",origin,n->host,n->port,*n->cookie?L"Cookie: ":L"",*n->cookie?cookie:L"",*n->cookie?L"\r\n":L"");
    if(!WinHttpAddRequestHeaders(req,headers,(DWORD)-1,WINHTTP_ADDREQ_FLAG_ADD|WINHTTP_ADDREQ_FLAG_REPLACE)) goto failed;
    if(type) { swprintf(headers,12000,L"Content-Type: %ls\r\n",type); WinHttpAddRequestHeaders(req,headers,(DWORD)-1,WINHTTP_ADDREQ_FLAG_ADD|WINHTTP_ADDREQ_FLAG_REPLACE); }
    if(upgrade && !WinHttpSetOption(req,WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET,NULL,0)) goto failed;
    if(!WinHttpSendRequest(req,WINHTTP_NO_ADDITIONAL_HEADERS,0,(void *)body,size,size,0)||!WinHttpReceiveResponse(req,NULL)) goto failed;
    { DWORD length=sizeof(*status); if(!WinHttpQueryHeaders(req,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,NULL,status,&length,NULL)) goto failed; }
    cookies(n,req); free(origin); free(cookie); return req;
failed:
    winerror(error,cap,GetLastError()); close_request(n,req); free(origin); free(cookie); return NULL;
}
static cJSON *http(Net *n,const char *path,const void *body,DWORD size,const wchar_t *type,char *error,size_t cap) {
    wchar_t *wp=wide(path); HINTERNET req; DWORD status=0, got; char chunk[16384], *wire=malloc(1); size_t used=0; cJSON *json=NULL;
    req=open_request(n,body?L"POST":L"GET",wp,type,0,body,size,error,cap,&status); free(wp);
    if(!req) { free(wire); return NULL; }
    if(wire) wire[0]=0;
    while(wire && WinHttpReadData(req,chunk,sizeof(chunk),&got) && got) {
        char *next; if(used+got>WIRE_LIMIT) { snprintf(error,cap,"Server response is too large."); break; }
        next=realloc(wire,used+got+1); if(!next) break; wire=next; memcpy(wire+used,chunk,got); used+=got; wire[used]=0;
    }
    if(status>=200 && status<300 && wire) json=cJSON_ParseWithLength(wire,used);
    if(!json) { if(status==401||status==403) snprintf(error,cap,"Sign in again. Check your server login.");
        else if(!*error) snprintf(error,cap,"Server request failed (HTTP %lu).",(unsigned long)status); }
    free(wire); close_request(n,req); return json;
}
static void received(void *context,cJSON *frame) { NetMessage *m=message(NetFrame); if(!m) { cJSON_Delete(frame); return; } m->json=frame; deliver(context,m); }
static DWORD WINAPI receive_loop(void *context) {
    Net *n=context; char chunk[16384], *wire=malloc(1); size_t used=0; DWORD got, code; WINHTTP_WEB_SOCKET_BUFFER_TYPE type;
    while(wire && WaitForSingleObject(n->stop,0)!=WAIT_OBJECT_0) {
        code=WinHttpWebSocketReceive(n->socket,chunk,sizeof(chunk),&got,&type);
        if(code || type==WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) break;
        if(type!=WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE && type!=WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE) break;
        if(used+got>WIRE_LIMIT) break;
        { char *next=realloc(wire,used+got+1); if(!next) break; wire=next; memcpy(wire+used,chunk,got); used+=got; wire[used]=0; }
        if(type==WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE) { if(frames_parse(wire,used,received,n)<0) break; used=0; }
    }
    free(wire); if(!n->closing && WaitForSingleObject(n->stop,0)!=WAIT_OBJECT_0) failure(n,"Disconnected. Your tasks stay on the server; reconnect to resume."); return 0;
}
static void disconnect(Net *n) {
    n->closing=1;
    if(n->socket) WinHttpCloseHandle(n->socket);
    if(n->reader) { WaitForSingleObject(n->reader,INFINITE); CloseHandle(n->reader); n->reader=NULL; }
    n->socket=NULL; if(n->connection) WinHttpCloseHandle(n->connection); if(n->session) WinHttpCloseHandle(n->session); n->connection=n->session=NULL;
}
static void connect_job(Net *n,Job *j) {
    char error[256]={0}, *body=NULL; cJSON *login=NULL, *reply=NULL, *ticket=NULL; HINTERNET req=NULL; wchar_t path[2048]; DWORD status=0;
    disconnect(n); n->closing=0;
    if(!endpoint(n,j->url,j->allow,error,sizeof(error))) goto done;
    snprintf(n->user,sizeof(n->user),"%s",j->user); snprintf(n->cookie,sizeof(n->cookie),"%s",j->cookie);
    n->session=WinHttpOpen(L"lcb-hermes/0.4",n->secure?WINHTTP_ACCESS_TYPE_DEFAULT_PROXY:WINHTTP_ACCESS_TYPE_NO_PROXY,NULL,NULL,0);
    if(!n->session) { winerror(error,sizeof(error),GetLastError()); goto done; }
    WinHttpSetTimeouts(n->session,10000,10000,30000,30000);
    n->connection=WinHttpConnect(n->session,n->connecthost,n->port,0); if(!n->connection) goto done;
    reply=http(n,"/api/status",NULL,0,NULL,error,sizeof(error));
    if(!reply || !*js(reply,"version")) { if(!*error) snprintf(error,sizeof(error),"This endpoint is not a Hermes server."); goto done; }
    cJSON_Delete(reply); reply=NULL;
    if(*j->password) {
        login=cJSON_CreateObject(); cJSON_AddStringToObject(login,"provider","basic"); cJSON_AddStringToObject(login,"username",j->user); cJSON_AddStringToObject(login,"password",j->password);
        body=cJSON_PrintUnformatted(login); reply=http(n,"/auth/password-login",body,(DWORD)strlen(body),L"application/json",error,sizeof(error));
        SecureZeroMemory(body,strlen(body)); free(body); body=NULL; if(!reply) goto done;
    }
    ticket=http(n,"/api/auth/ws-ticket","{}",2,L"application/json",error,sizeof(error)); if(!ticket || !*js(ticket,"ticket")) goto done;
    /* Tickets are server-generated URL-safe opaque strings. Reject delimiters. */
    if(strpbrk(js(ticket,"ticket"),"?#&\r\n ")) { snprintf(error,sizeof(error),"Invalid connection ticket."); goto done; }
    { wchar_t *t=wide(js(ticket,"ticket")); swprintf(path,2048,L"/api/ws?ticket=%ls",t); free(t); }
    req=open_request(n,L"GET",path,NULL,1,NULL,0,error,sizeof(error),&status);
    if(!req||status!=101) { if(!*error) snprintf(error,sizeof(error),"Could not open the Hermes connection."); goto done; }
    n->socket=WinHttpWebSocketCompleteUpgrade(req,0); if(!n->socket) goto done;
    close_request(n,req); req=NULL;
    { DWORD keepalive=20000; WinHttpSetOption(n->socket,WINHTTP_OPTION_WEB_SOCKET_KEEPALIVE_INTERVAL,&keepalive,sizeof(keepalive)); }
    n->reader=CreateThread(NULL,0,receive_loop,n,0,NULL); if(!n->reader) goto done;
    { NetMessage *m=message(NetOnline); if(m) { snprintf(m->url,sizeof(m->url),"%s",n->url); snprintf(m->user,sizeof(m->user),"%s",n->user); snprintf(m->cookie,sizeof(m->cookie),"%s",n->cookie); deliver(n,m); } }
done:
    close_request(n,req); if(login) { cJSON *password=cJSON_GetObjectItemCaseSensitive(login,"password"); if(cJSON_IsString(password)) SecureZeroMemory(password->valuestring,strlen(password->valuestring)); } cJSON_Delete(login); cJSON_Delete(reply); cJSON_Delete(ticket);
    if(!n->socket||!n->reader) { disconnect(n); failure(n,*error?error:"Could not connect to Hermes."); }
}
static void freejob(Job *j) { if(!j) return; if(j->password) SecureZeroMemory(j->password,strlen(j->password)); free(j->url); free(j->user); free(j->password); free(j->cookie); free(j->path); free(j->wire); free(j->file); free(j); }
static void upload(Net *n,Job *j,NetMessage *m) {
    HANDLE file=CreateFileW(j->file,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL); LARGE_INTEGER length; DWORD got; char *data, *name; const wchar_t *base=wcsrchr(j->file,L'\\'); size_t prefix, total;
    char boundary[80], ending[110], header[4096]; GUID guid; cJSON *files; const char *root;
    if(file==INVALID_HANDLE_VALUE) { snprintf(m->error,sizeof(m->error),"Could not open the attachment."); return; }
    if(!GetFileSizeEx(file,&length)||length.QuadPart<=0||length.QuadPart>20*1024*1024) { CloseHandle(file); snprintf(m->error,sizeof(m->error),"Choose an attachment under 20 MiB."); return; }
    name=utf8(base?base+1:j->file); if(!name || strpbrk(name,"\r\n\"")) { free(name); CloseHandle(file); snprintf(m->error,sizeof(m->error),"Unsupported attachment name."); return; }
    files=http(n,"/api/files",NULL,0,NULL,m->error,sizeof(m->error)); root=js(files,"path");
    if(!files||!*root||strlen(root)>1024||strlen(name)>512) { cJSON_Delete(files); free(name); CloseHandle(file); return; }
    CoCreateGuid(&guid); snprintf(boundary,sizeof(boundary),"lcb%08lx%04x%04x%llu",(unsigned long)guid.Data1,guid.Data2,guid.Data3,(unsigned long long)GetTickCount64());
    snprintf(ending,sizeof(ending),"\r\n--%s--\r\n",boundary);
    snprintf(header,sizeof(header),"--%s\r\nContent-Disposition: form-data; name=\"path\"\r\n\r\n%s/lcb-hermes/uploads/%s-%s\r\n--%s\r\nContent-Disposition: form-data; name=\"overwrite\"\r\n\r\nfalse\r\n--%s\r\nContent-Disposition: form-data; name=\"file\"; filename=\"%s\"\r\nContent-Type: application/octet-stream\r\n\r\n",boundary,root,boundary,name,boundary,boundary,name); cJSON_Delete(files); free(name); prefix=strlen(header); total=prefix+(size_t)length.QuadPart+strlen(ending); data=malloc(total);
    if(!data) { CloseHandle(file); return; } memcpy(data,header,prefix);
    if(ReadFile(file,data+prefix,(DWORD)length.QuadPart,&got,NULL)&&got==(DWORD)length.QuadPart) { wchar_t type[160]; memcpy(data+prefix+got,ending,strlen(ending)); swprintf(type,160,L"multipart/form-data; boundary=%hs",boundary); m->json=http(n,"/api/files/upload-stream",data,(DWORD)total,type,m->error,sizeof(m->error)); }
    else snprintf(m->error,sizeof(m->error),"Could not read the attachment."); free(data); CloseHandle(file);
}
static DWORD WINAPI worker(void *context) {
    Net *n=context; WSADATA wsa; HANDLE events[2]={n->stop,n->work}; WSAStartup(MAKEWORD(2,2),&wsa);
    while(WaitForMultipleObjects(2,events,FALSE,INFINITE)==WAIT_OBJECT_0+1) {
        Job *j;
        EnterCriticalSection(&n->lock); j=n->first; if(j) { n->first=j->next; if(!n->first) n->last=NULL; } if(!n->first) ResetEvent(n->work); LeaveCriticalSection(&n->lock);
        if(!j) continue;
        if(j->kind==1) connect_job(n,j);
        else if(j->kind==2) { if(!n->socket || WinHttpWebSocketSend(n->socket,WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,j->wire,(DWORD)strlen(j->wire))) failure(n,"Message could not be sent. It was not retried."); }
        else { NetMessage *m=message(NetRest); if(m) { m->tag=j->tag; m->context=j->context;
            if(!n->connection) snprintf(m->error,sizeof(m->error),"Connect first.");
            else if(j->kind==4) upload(n,j,m);
            else m->json=http(n,j->path,NULL,0,NULL,m->error,sizeof(m->error)); deliver(n,m);
        } }
        freejob(j);
    }
    disconnect(n); WSACleanup(); return 0;
}
static void queue(Net *n,Job *j) { if(!j) return; EnterCriticalSection(&n->lock); if(n->last) n->last->next=j; else n->first=j; n->last=j; SetEvent(n->work); LeaveCriticalSection(&n->lock); }
Net *net_new(HWND target) { Net *n=calloc(1,sizeof(*n)); if(!n) return NULL; n->target=target; InitializeCriticalSection(&n->lock); n->work=CreateEventW(NULL,TRUE,FALSE,NULL); n->stop=CreateEventW(NULL,TRUE,FALSE,NULL); n->thread=CreateThread(NULL,0,worker,n,0,NULL); return n; }
void net_connect(Net *n,const char *url,const char *user,const char *password,int allow,const char *cookie) { Job *j=calloc(1,sizeof(*j)); if(!j) return; j->kind=1; j->allow=allow; j->url=textdup(url); j->user=textdup(user); j->password=textdup(password); j->cookie=textdup(cookie); queue(n,j); }
int net_send(Net *n,const cJSON *frame) { Job *j=calloc(1,sizeof(*j)); if(!j) return 0; j->kind=2; j->wire=cJSON_PrintUnformatted(frame); if(!j->wire) { free(j); return 0; } queue(n,j); return 1; }
void net_rest(Net *n,const char *path,int tag,void *ctx) { Job *j=calloc(1,sizeof(*j)); if(!j) return; j->kind=3; j->path=textdup(path); j->tag=tag; j->context=ctx; queue(n,j); }
void net_upload(Net *n,const wchar_t *path,int tag,void *ctx) { Job *j=calloc(1,sizeof(*j)); size_t size=(wcslen(path)+1)*sizeof(wchar_t); if(!j) return; j->kind=4; j->file=malloc(size); if(j->file) memcpy(j->file,path,size); j->tag=tag; j->context=ctx; queue(n,j); }
void net_free(Net *n) { Job *j; if(!n) return; SetEvent(n->stop); EnterCriticalSection(&n->lock); if(n->request) { WinHttpCloseHandle(n->request); n->request=NULL; } LeaveCriticalSection(&n->lock); WaitForSingleObject(n->thread,INFINITE); CloseHandle(n->thread); while((j=n->first)!=NULL) { n->first=j->next; freejob(j); } CloseHandle(n->work); CloseHandle(n->stop); DeleteCriticalSection(&n->lock); SecureZeroMemory(n,sizeof(*n)); free(n); }
