#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <richedit.h>
#include <dwmapi.h>
#include <wincodec.h>
#include <shlwapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "core.h"
#include "transport_win.h"
#include "store_win.h"
#include "classic_win.h"
#include "markdown.h"

enum { IdTree=100,IdPrompt,IdSend,IdNew,IdServer,IdModel,IdSearch,IdAttach,IdRequest,IdExport,IdReconnect,IdRefresh,IdRename,IdDelete,IdForget,IdTheme,IdAbout,IdDraft,IdRemoveAttachment,IdRender=300,IdSave,IdHeartbeat,IdTimeout };
enum { RpcCaps=1,RpcList,RpcResume,RpcCreate,RpcSubmit,RpcStop,RpcModels,RpcAttach,RpcRename,RpcDelete,RpcPing,RpcCloseDelete,RpcResumeSend };
enum { RestStats=1,RestUpload };
typedef struct Pending { int id, kind; Chat *chat; ULONGLONG started; struct Pending *next; } Pending;
typedef struct Preview { HBITMAP bitmap; LONG anchor; int width,height; } Preview;
typedef struct Upload { Chat *chat; wchar_t path[32768]; } Upload;
static struct {
    HINSTANCE instance; HWND window,tree,search,prompt,transcript,send,fresh,server,model,title,status,attach,request,serverwin,tooltip;
    HFONT normal,fixed,heading; HBRUSH face,paper; HIMAGELIST icons; HICON servericon; HMODULE rich;
    int dpi,dark,width,height,connected,connecting,restoring,nextid,treebusy,models_pending,models_open,uploads,corrupt;
    COLORREF ink,muted,bg,fg,accent; wchar_t path[1024]; char url[1024],user[256],cookie[8192],notice[512],version[80],newmodel[160],newprovider[160],epoch[160];
    Net *net; cJSON *settings,*profile,*stats,*models; Pending *pending; Chat **chats,*current; size_t count,capacity;
    Preview previews[128]; int npreviews; char *status_text;
} app;
static void render(void), sidebar(void), save(void), controls(void), show_server(void), resume(Chat *c), submit(void), start_send(Chat *c), request_dialog(void);
static int px(int n) { return MulDiv(n,app.dpi,96); }
static void copytext(char *dst,size_t cap,const char *src) { snprintf(dst,cap,"%s",src); }
static void settext(HWND w,const char *s) { wchar_t *v=wide(s); if(v) { SetWindowTextW(w,v); free(v); } }
static char *gettext(HWND w) { int n=GetWindowTextLengthW(w); wchar_t *v=calloc((size_t)n+1,sizeof(*v)); char *s; if(!v) return textdup(""); GetWindowTextW(w,v,n+1); s=utf8(v); free(v); return s?s:textdup(""); }
static void put(cJSON *o,const char *key,const char *s) { cJSON_DeleteItemFromObjectCaseSensitive(o,key); cJSON_AddStringToObject(o,key,s); }
static cJSON *params(void) { return cJSON_CreateObject(); }
static cJSON *sessionparams(Chat *c) { cJSON *p=params(); cJSON_AddStringToObject(p,"session_id",*c->id?c->id:c->stored); return p; }
static void schedule_save(void) { if(!app.restoring) SetTimer(app.window,IdSave,750,NULL); }
static void notice(const char *s) { copytext(app.notice,sizeof(app.notice),s); controls(); }
static void freshkey(char out[65]) { GUID id; if(SUCCEEDED(CoCreateGuid(&id))) snprintf(out,65,"%08lx%04x%04x%02x%02x%02x%02x%02x%02x%02x%02x",(unsigned long)id.Data1,id.Data2,id.Data3,id.Data4[0],id.Data4[1],id.Data4[2],id.Data4[3],id.Data4[4],id.Data4[5],id.Data4[6],id.Data4[7]); else snprintf(out,65,"%lu-%llu",(unsigned long)GetCurrentProcessId(),(unsigned long long)GetTickCount64()); }
static void addchat(Chat *c) { if(!c) return; if(app.count==app.capacity) { size_t cap=app.capacity?app.capacity*2:16; Chat **next=realloc(app.chats,cap*sizeof(*next)); if(!next) { chat_free(c); return; } app.chats=next; app.capacity=cap; } app.chats[app.count++]=c; }
static Chat *findchat(const char *id) { size_t i; if(!*id) return NULL; for(i=0;i<app.count;i++) if(!strcmp(id,app.chats[i]->id)||!strcmp(id,app.chats[i]->stored)||!strcmp(id,app.chats[i]->key)) return app.chats[i]; return NULL; }
static void newchat(void) { char key[65]; freshkey(key); addchat(chat_new(key)); app.current=app.chats[app.count-1]; app.notice[0]=0; sidebar(); render(); schedule_save(); SetFocus(app.prompt); }
static void clear_chats(void) { size_t i; for(i=0;i<app.count;i++) chat_free(app.chats[i]); free(app.chats); app.chats=NULL; app.count=app.capacity=0; app.current=NULL; }
static void profile_load(const char *url,const char *user) {
    cJSON *profiles=cJSON_GetObjectItemCaseSensitive(app.settings,"profiles"),*p,*a,*v; const char *active;
    clear_chats(); app.profile=NULL;
    if(!cJSON_IsArray(profiles)) { profiles=cJSON_CreateArray(); cJSON_AddItemToObject(app.settings,"profiles",profiles); }
    cJSON_ArrayForEach(p,profiles) if(!strcmp(js(p,"url"),url)&&!strcmp(js(p,"user"),user)) { app.profile=p; break; }
    if(!app.profile) { app.profile=cJSON_CreateObject(); put(app.profile,"url",url); put(app.profile,"user",user); cJSON_AddItemToArray(profiles,app.profile); }
    a=cJSON_GetObjectItemCaseSensitive(app.profile,"chats"); cJSON_ArrayForEach(v,a) if(app.count<1000) addchat(chat_restore(v));
    active=js(app.profile,"active"); app.current=findchat(active); if(!app.current&&app.count) app.current=app.chats[0]; if(!app.current) newchat();
    put(app.settings,"active_url",url); put(app.settings,"active_user",user);
}
static void save(void) {
    cJSON *a; size_t i; KillTimer(app.window,IdSave); if(!app.settings||app.corrupt) return;
    cJSON_DeleteItemFromObjectCaseSensitive(app.settings,"dark"); cJSON_AddBoolToObject(app.settings,"dark",app.dark);
    if(app.profile) { a=cJSON_CreateArray(); for(i=0;i<app.count;i++) cJSON_AddItemToArray(a,chat_json(app.chats[i])); cJSON_DeleteItemFromObjectCaseSensitive(app.profile,"chats"); cJSON_AddItemToObject(app.profile,"chats",a); put(app.profile,"active",app.current?app.current->key:""); }
    if(!store_write(app.path,app.settings)) notice("Could not save settings. Your server chats are safe.");
}
static int rpc(const char *method,cJSON *p,int kind,Chat *chat) {
    cJSON *frame; Pending *wait;
    if(!app.connected) { cJSON_Delete(p); notice("Connect to your server first."); return 0; }
    wait=calloc(1,sizeof(*wait)); if(!wait) { cJSON_Delete(p); return 0; } wait->id=++app.nextid; wait->kind=kind; wait->chat=chat; wait->started=GetTickCount64(); wait->next=app.pending; app.pending=wait;
    frame=cJSON_CreateObject(); cJSON_AddStringToObject(frame,"jsonrpc","2.0"); cJSON_AddNumberToObject(frame,"id",wait->id); cJSON_AddStringToObject(frame,"method",method); cJSON_AddItemToObject(frame,"params",p); net_send(app.net,frame); cJSON_Delete(frame); return wait->id;
}
static void listchats(void) { cJSON *p=params(); cJSON_AddNumberToObject(p,"limit",500); rpc("session.list",p,RpcList,NULL); }
static void resume(Chat *c) { cJSON *p; if(!c||!*c->stored||c->pending||!app.connected) return; p=params(); cJSON_AddStringToObject(p,"session_id",c->stored); cJSON_AddStringToObject(p,"source","desktop"); cJSON_AddBoolToObject(p,"close_on_disconnect",0); c->pending=1; rpc("session.resume",p,RpcResume,c); controls(); }
static void status_stats(void) {
    char buffer[2048]; const cJSON *s=app.stats,*memory=cJSON_GetObjectItemCaseSensitive(s,"memory"), *process=cJSON_GetObjectItemCaseSensitive(s,"process"),*disk=cJSON_GetObjectItemCaseSensitive(s,"disk");
    HWND view=app.serverwin?GetDlgItem(app.serverwin,507):NULL;
    if(!view) return;
    if(!s) { settext(view,app.connected?"Connected. Refresh for server stats.":"Connect to view server stats."); return; }
    snprintf(buffer,sizeof(buffer),"Hermes  %s\r\nHost  %s\r\nSystem  %s %s\r\n\r\nCPU  %.1f%%   /   %.0f logical CPUs\r\nSystem memory  %.1f / %.1f GiB\r\nAvailable  %.1f GiB\r\nHermes process  %.1f MiB   /   %.0f threads\r\nData volume  %.1f / %.1f GiB\r\n\r\nStats are supplied by the server; system figures may describe the NAS.",js(s,"hermes_version"),js(s,"hostname"),js(s,"os"),js(s,"arch"),cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(s,"cpu_percent")),cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(s,"cpu_count")),cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(memory,"used"))/1073741824.0,cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(memory,"total"))/1073741824.0,cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(memory,"available"))/1073741824.0,cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(process,"rss"))/1048576.0,cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(process,"num_threads")),cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(disk,"used"))/1073741824.0,cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(disk,"total"))/1073741824.0);
    settext(view,buffer);
}
static void controls(void) {
    Chat *c=app.current; char line[1200]; int ready=app.connected&&c&&!c->pending&&!app.uploads;
    EnableWindow(app.send,ready); EnableWindow(app.attach,ready&&c&&!c->running); SetWindowTextW(app.send,c&&c->running?L"stop":L"send");
    snprintf(line,sizeof(line),"%s%s%s",app.connecting?"connecting":app.connected?"connected":"offline",*app.notice?"  /  ":c&&c->running?"  /  Hermes is working":"",app.notice);
    settext(app.status,line); SetWindowTextW(app.request,c&&cJSON_GetArraySize(c->requests)?L"answer request":L"export chat");
    settext(app.model,c&&*c->model?c->model:*app.newmodel?app.newmodel:"server default");
    settext(app.title,c?c->title:"new chat");
    if(app.serverwin) { settext(GetDlgItem(app.serverwin,508),line); EnableWindow(GetDlgItem(app.serverwin,505),!app.connecting); }
    InvalidateRect(app.window,NULL,FALSE);
}
static void sidebar(void) {
    size_t i; char *filter=gettext(app.search); wchar_t *query=wide(filter); HTREEITEM selected=NULL; free(filter);
    app.treebusy=1; SendMessageW(app.tree,WM_SETREDRAW,FALSE,0); TreeView_DeleteAllItems(app.tree);
    for(i=0;i<app.count;i++) {
        Chat *c=app.chats[i]; TVINSERTSTRUCTW item={0}; wchar_t label[360], *title=wide(c->title); HTREEITEM h;
        if(!title) continue;
        if(query&&*query&&!StrStrIW(title,query)) { free(title); continue; }
        swprintf(label,360,L"%ls%ls%ls",c->unread?L"* ":L"",title,c->running?L"  ...":L""); free(title);
        item.hParent=TVI_ROOT; item.hInsertAfter=TVI_LAST; item.item.mask=TVIF_TEXT|TVIF_PARAM|TVIF_IMAGE|TVIF_SELECTEDIMAGE; item.item.pszText=label; item.item.lParam=(LPARAM)c; item.item.iImage=item.item.iSelectedImage=c->running?ClassicRun:ClassicDocument;
        h=TreeView_InsertItem(app.tree,&item); if(c==app.current) selected=h;
    }
    if(selected) TreeView_SelectItem(app.tree,selected); SendMessageW(app.tree,WM_SETREDRAW,TRUE,0); InvalidateRect(app.tree,NULL,TRUE); app.treebusy=0; free(query);
}
static void setformat(unsigned style) {
    CHARFORMAT2W f={0}; f.cbSize=sizeof(f); f.dwMask=CFM_COLOR|CFM_FACE|CFM_BOLD|CFM_ITALIC|CFM_SIZE|CFM_BACKCOLOR; f.crTextColor=(style&MdQuote)?app.muted:app.ink; f.crBackColor=app.fg; f.yHeight=(style&MdHeading)?260:200;
    if(style&(MdBold|MdHeading)) f.dwEffects|=CFE_BOLD; if(style&MdItalic) f.dwEffects|=CFE_ITALIC;
    wcscpy(f.szFaceName,style&MdCode?L"Consolas":L"Segoe UI"); SendMessageW(app.transcript,EM_SETCHARFORMAT,SCF_SELECTION,(LPARAM)&f);
}
static void emit_markdown(void *ctx,const wchar_t *text,unsigned style) { (void)ctx; setformat(style); SendMessageW(app.transcript,EM_REPLACESEL,FALSE,(LPARAM)text); }
static void append(const wchar_t *text,unsigned style) { emit_markdown(NULL,text,style); }
static HBITMAP image_bitmap(const wchar_t *path,int *outw,int *outh) {
    IWICImagingFactory *factory=NULL; IWICBitmapDecoder *decoder=NULL; IWICBitmapFrameDecode *frame=NULL; IWICFormatConverter *converter=NULL; IWICBitmapScaler *scaler=NULL; UINT w=0,h=0; HBITMAP bitmap=NULL; BITMAPINFO info={0}; void *pixels; HDC dc;
    if(FAILED(CoCreateInstance(&CLSID_WICImagingFactory,NULL,CLSCTX_INPROC_SERVER,&IID_IWICImagingFactory,(void **)&factory))) return NULL;
    if(FAILED(IWICImagingFactory_CreateDecoderFromFilename(factory,path,NULL,GENERIC_READ,WICDecodeMetadataCacheOnDemand,&decoder))||FAILED(IWICBitmapDecoder_GetFrame(decoder,0,&frame))||FAILED(IWICBitmapFrameDecode_GetSize(frame,&w,&h))||!w||!h||w>30000||h>30000) goto done;
    { double ratio=240.0/(double)w; if((double)h*ratio>150.0) ratio=150.0/(double)h; if(ratio>1.0) ratio=1.0; w=(UINT)((double)w*ratio); h=(UINT)((double)h*ratio); if(!w) w=1; if(!h) h=1; }
    if(FAILED(IWICImagingFactory_CreateBitmapScaler(factory,&scaler))||FAILED(IWICBitmapScaler_Initialize(scaler,(IWICBitmapSource *)frame,w,h,WICBitmapInterpolationModeFant))||FAILED(IWICImagingFactory_CreateFormatConverter(factory,&converter))||FAILED(IWICFormatConverter_Initialize(converter,(IWICBitmapSource *)scaler,&GUID_WICPixelFormat32bppBGR,WICBitmapDitherTypeNone,NULL,0,WICBitmapPaletteTypeCustom))) goto done;
    info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER); info.bmiHeader.biWidth=(LONG)w; info.bmiHeader.biHeight=-(LONG)h; info.bmiHeader.biPlanes=1; info.bmiHeader.biBitCount=32; info.bmiHeader.biCompression=BI_RGB;
    dc=GetDC(NULL); bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,NULL,0); ReleaseDC(NULL,dc);
    if(bitmap&&FAILED(IWICFormatConverter_CopyPixels(converter,NULL,w*4,w*h*4,pixels))) { DeleteObject(bitmap); bitmap=NULL; }
    if(bitmap) { *outw=(int)w; *outh=(int)h; }
done:
    if(converter) IWICFormatConverter_Release(converter); if(scaler) IWICBitmapScaler_Release(scaler); if(frame) IWICBitmapFrameDecode_Release(frame); if(decoder) IWICBitmapDecoder_Release(decoder); IWICImagingFactory_Release(factory); return bitmap;
}
static LRESULT CALLBACK transcript_proc(HWND w,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR ref) {
    LRESULT result=DefSubclassProc(w,msg,wp,lp); int i; (void)id;(void)ref;
    if(msg==WM_PAINT) { HDC dc=GetDC(w); RECT client; GetClientRect(w,&client);
        for(i=0;i<app.npreviews;i++) { Preview *p=&app.previews[i]; POINTL pos={0}; SendMessageW(w,EM_POSFROMCHAR,(WPARAM)&pos,p->anchor);
            if(pos.y>=-px(150)&&pos.y<client.bottom) { HDC source=CreateCompatibleDC(dc); HGDIOBJ old=SelectObject(source,p->bitmap); SetStretchBltMode(dc,HALFTONE); StretchBlt(dc,pos.x,pos.y,px(p->width),px(p->height),source,0,0,p->width,p->height,SRCCOPY); SelectObject(source,old); DeleteDC(source); }
        } ReleaseDC(w,dc);
    }
    return result;
}
static void previews_clear(void) { int i; for(i=0;i<app.npreviews;i++) DeleteObject(app.previews[i].bitmap); app.npreviews=0; }
static void show_attachments(const cJSON *array) {
    const cJSON *a; cJSON_ArrayForEach(a,array) {
        wchar_t *name=wide(js(a,"name")), *local=wide(js(a,"local")); HBITMAP bitmap=NULL; int w=0,h=0;
        if(local && !strncmp(js(a,"mime"),"image/",6) && app.npreviews<128) bitmap=image_bitmap(local,&w,&h);
        if(bitmap) { CHARRANGE range={0}; int line; Preview *p=&app.previews[app.npreviews++]; SendMessageW(app.transcript,EM_EXGETSEL,0,(LPARAM)&range); p->bitmap=bitmap; p->width=w; p->height=h; p->anchor=range.cpMax; for(line=0;line<(h+12)/13+2;line++) append(L"\r\n",0); }
        if(name) { append(name,MdQuote); append(L"\r\n",0); } free(name); free(local);
    }
}
static void render(void) {
    const cJSON *row; Chat *c=app.current; POINT scroll={0}; CHARRANGE selected={0}; BOOL atbottom=TRUE; SCROLLINFO info={sizeof(info),SIF_ALL,0,0,0,0,0}; wchar_t *draft;
    KillTimer(app.window,IdRender); SendMessageW(app.transcript,EM_GETSCROLLPOS,0,(LPARAM)&scroll); SendMessageW(app.transcript,EM_EXGETSEL,0,(LPARAM)&selected);
    if(GetScrollInfo(app.transcript,SB_VERT,&info)) atbottom=info.nPos+(int)info.nPage>=info.nMax-4;
    previews_clear(); SendMessageW(app.transcript,WM_SETREDRAW,FALSE,0); SetWindowTextW(app.transcript,L"");
    SendMessageW(app.transcript,EM_SETBKGNDCOLOR,0,app.fg);
    if(!c || !cJSON_GetArraySize(c->messages)) {
        append(L"lcb-hermes\r\n",MdHeading); append(app.connected?L"A fresh conversation.\r\n":L"Connect to your Hermes server to begin.\r\n",MdQuote);
    } else cJSON_ArrayForEach(row,c->messages) {
        char *text=row_text(row); wchar_t *w=wide(text?text:""); const char *role=js(row,"role");
        if(strcmp(role,"user")&&strcmp(role,"assistant")&&strcmp(role,"system")) { free(text); free(w); continue; }
        append(!strcmp(role,"user")?L"you\r\n":!strcmp(role,"assistant")?L"hermes\r\n":L"system\r\n",MdBold|MdQuote);
        show_attachments(cJSON_GetObjectItemCaseSensitive(row,"attachments")); if(w) markdown_render(w,emit_markdown,NULL); append(L"\r\n\r\n",0); free(text); free(w);
    }
    if(c && cJSON_GetArraySize(c->attachments)) { append(L"attached for your next message\r\n",MdQuote); show_attachments(c->attachments); }
    SendMessageW(app.transcript,WM_SETREDRAW,TRUE,0);
    if(atbottom) { SendMessageW(app.transcript,EM_SETSEL,(WPARAM)-1,(LPARAM)-1); SendMessageW(app.transcript,EM_SCROLLCARET,0,0); }
    else { SendMessageW(app.transcript,EM_EXSETSEL,0,(LPARAM)&selected); SendMessageW(app.transcript,EM_SETSCROLLPOS,0,(LPARAM)&scroll); }
    InvalidateRect(app.transcript,NULL,TRUE); app.restoring=1; draft=wide(c?c->draft:""); if(draft) { char *old=gettext(app.prompt); if(strcmp(old,c?c->draft:"")) SetWindowTextW(app.prompt,draft); free(old); free(draft); } app.restoring=0; controls();
}
static void send_prompt(Chat *c) {
    cJSON *p=sessionparams(c),*row=cJSON_CreateObject(); char *wire=textdup(*c->draft?c->draft:"Please look at the attachment."); const cJSON *a;
    cJSON_ArrayForEach(a,c->attachments) if(strncmp(js(a,"mime"),"image/",6)) { size_t n=strlen(wire)+strlen(js(a,"path"))+30; char *next=malloc(n); if(next) { snprintf(next,n,"%s\n\n[Attached file: %s]",wire,js(a,"path")); free(wire); wire=next; } }
    cJSON_AddStringToObject(p,"text",wire); cJSON_AddStringToObject(p,"surface","desktop"); free(wire);
    { int ordinal=0; char key[40]; const cJSON *m; cJSON_ArrayForEach(m,c->messages) if(!strcmp(js(m,"role"),"user")) ordinal++; snprintf(key,sizeof(key),"%d",ordinal); cJSON_DeleteItemFromObjectCaseSensitive(c->media,key); cJSON_AddItemToObject(c->media,key,cJSON_Duplicate(c->attachments,1)); }
    cJSON_AddStringToObject(row,"role","user"); cJSON_AddStringToObject(row,"text",c->draft); cJSON_AddItemToObject(row,"attachments",cJSON_Duplicate(c->attachments,1)); cJSON_AddItemToArray(c->messages,row);
    cJSON_Delete(c->attachments); c->attachments=cJSON_CreateArray(); c->running=1; c->pending=1; c->stream=-1;
    rpc("prompt.submit",p,RpcSubmit,c); if(c==app.current) render(); sidebar(); controls();
}
static void start_send(Chat *c) {
    c->pending=1;
    const cJSON *a; int attached=0;
    cJSON_ArrayForEach(a,c->attachments) if(!strncmp(js(a,"mime"),"image/",6) && !jb(a,"queued")) { cJSON *p=sessionparams(c); cJSON_AddStringToObject(p,"path",js(a,"path")); rpc("image.attach",p,RpcAttach,c); attached=1; break; }
    if(!attached) send_prompt(c);
}
static void submit(void) {
    Chat *c=app.current; cJSON *p;
    if(!c||c->pending||app.uploads||!app.connected) return;
    if(c->running) { rpc("session.interrupt",sessionparams(c),RpcStop,c); return; }
    if(!*c->draft&&!cJSON_GetArraySize(c->attachments)) return; c->pending=1;
    if(!*c->id && *c->stored) { p=params(); cJSON_AddStringToObject(p,"session_id",c->stored); cJSON_AddStringToObject(p,"source","desktop"); cJSON_AddBoolToObject(p,"close_on_disconnect",0); rpc("session.resume",p,RpcResumeSend,c); }
    else if(!*c->id) { p=params(); cJSON_AddStringToObject(p,"source","desktop"); cJSON_AddBoolToObject(p,"close_on_disconnect",0); if(*app.newmodel) { cJSON_AddStringToObject(p,"model",app.newmodel); cJSON_AddStringToObject(p,"provider",app.newprovider); } rpc("session.create",p,RpcCreate,c); }
    else start_send(c); controls();
}
static void incoming_event(cJSON *e) {
    Chat *c=findchat(js(e,"session_id")); const char *type=js(e,"type"); const cJSON *p=cJSON_GetObjectItemCaseSensitive(e,"payload");
    if(!strcmp(type,"gateway.ready")) { const char *epoch=js(p,"replay_epoch"); if(strcmp(app.epoch,epoch)) { size_t i; copytext(app.epoch,sizeof(app.epoch),epoch); for(i=0;i<app.count;i++) app.chats[i]->sequence=-1; } return; }
    if(!strcmp(type,"sessions.changed")) { listchats(); return; }
    if(!c) return;
    if(!strncmp(type,"tool.",5)) { if(c==app.current) { char s[512]; snprintf(s,sizeof(s),"%s %s",!strcmp(type,"tool.complete")?"finished":"using",js(p,"name")); notice(s); } return; }
    if(chat_event(c,e)) {
        if(c==app.current) SetTimer(app.window,IdRender,40,NULL); else c->unread=1;
        if(!strcmp(type,"message.complete")) { c->unread=c!=app.current; app.notice[0]=0; sidebar(); schedule_save();
            if(GetForegroundWindow()!=app.window || c!=app.current) { FLASHWINFO flash={sizeof(flash),app.window,FLASHW_TRAY,2,0}; FlashWindowEx(&flash); MessageBeep(MB_OK); }
            if(c==app.current && *js(p,"error")) notice(js(p,"error")); listchats();
        } controls();
    }
}
static void incoming_request(cJSON *r) {
    const cJSON *p=cJSON_GetObjectItemCaseSensitive(r,"params"),*old; Chat *c=findchat(js(p,"session_id"));
    if(c) cJSON_ArrayForEach(old,c->requests) if(cJSON_Compare(cJSON_GetObjectItemCaseSensitive(old,"id"),cJSON_GetObjectItemCaseSensitive(r,"id"),1)) return;
    if(!c) c=app.current; if(!c) return; cJSON_AddItemToArray(c->requests,cJSON_Duplicate(r,1)); c->unread=c!=app.current; sidebar(); controls();
}
static void result(Pending *wait,const cJSON *r,const char *error) {
    Chat *c=wait->chat; cJSON *v,*array;
    if(*error) { if(c) c->pending=0; if(wait->kind==RpcSubmit) { if(c) { c->running=0; resume(c); } notice("Send result was uncertain. The message was not retried; reconnect to check the chat."); }
        else notice(error); if(wait->kind==RpcModels) app.models_pending=0;
        if(c&&wait->kind==RpcAttach) { cJSON *a; cJSON_ArrayForEach(a,c->attachments) if(jb(a,"queued")) { cJSON *p=sessionparams(c); cJSON_AddStringToObject(p,"path",js(a,"path")); rpc("image.detach",p,0,NULL); cJSON_DeleteItemFromObjectCaseSensitive(a,"queued"); } }
        controls(); return; }
    switch(wait->kind) {
    case RpcList:
        array=cJSON_GetObjectItemCaseSensitive(r,"sessions"); cJSON_ArrayForEach(v,array) { const char *id=js(v,"session_id"); Chat *found; if(!*id) id=js(v,"id"); if(!*id) continue; found=findchat(id);
            if(!found && app.count<1000) { char key[65]; freshkey(key); found=chat_new(key); copytext(found->stored,sizeof(found->stored),id); addchat(found); }
            if(found && *js(v,"title")) copytext(found->title,sizeof(found->title),js(v,"title"));
        } sidebar(); break;
    case RpcResume: if(c) { chat_load(c,r); if(c==app.current) { c->unread=0; render(); } sidebar(); schedule_save(); } break;
    case RpcCreate: case RpcResumeSend: if(c) { chat_load(c,r); start_send(c); } break;
    case RpcSubmit: if(c) { c->pending=0; free(c->draft); c->draft=textdup(""); if(c==app.current) { app.restoring=1; SetWindowTextW(app.prompt,L""); app.restoring=0; } schedule_save(); } break;
    case RpcAttach:
        if(c) { const cJSON *a; cJSON_ArrayForEach(a,c->attachments) if(!jb(a,"queued") && !strncmp(js(a,"mime"),"image/",6)) { cJSON_AddBoolToObject((cJSON *)a,"queued",1); break; } start_send(c); } break;
    case RpcModels: cJSON_Delete(app.models); app.models=cJSON_Duplicate(r,1); app.models_pending=0; PostMessageW(app.window,WM_COMMAND,IdModel,0); break;
    case RpcRename: if(c) { copytext(c->title,sizeof(c->title),js(r,"title")); listchats(); schedule_save(); } break;
    case RpcCloseDelete: if(c) { cJSON *p=params(); c->id[0]=0; cJSON_AddStringToObject(p,"session_id",c->stored); rpc("session.delete",p,RpcDelete,c); } break;
    case RpcDelete: if(c) { size_t i; Pending **link=&app.pending; while(*link) { Pending *p=*link; if(p->chat==c) { *link=p->next; free(p); } else link=&p->next; } for(i=0;i<app.count;i++) if(app.chats[i]==c) { memmove(app.chats+i,app.chats+i+1,(app.count-i-1)*sizeof(*app.chats)); app.count--; break; } if(app.current==c) app.current=app.count?app.chats[0]:NULL; chat_free(c); if(!app.current) newchat(); else { if(*app.current->stored) resume(app.current); render(); sidebar(); } save(); } break;
    default: break;
    } controls();
}
static void frame(cJSON *m) {
    cJSON *id=cJSON_GetObjectItemCaseSensitive(m,"id");
    if(*js(m,"method")) { if(!strcmp(js(m,"method"),"event")) incoming_event(cJSON_GetObjectItemCaseSensitive(m,"params")); else if(id) incoming_request(m); return; }
    if(cJSON_IsNumber(id)) { Pending **link=&app.pending; while(*link) { Pending *p=*link; if(p->id==id->valueint) { const cJSON *error=cJSON_GetObjectItemCaseSensitive(m,"error"); *link=p->next; result(p,cJSON_GetObjectItemCaseSensitive(m,"result"),error?js(error,"message"):""); free(p); return; } link=&p->next; } }
}
static void on_net(NetMessage *m) {
    if(m->kind==NetOnline) {
        int changed=strcmp(app.url,m->url)||strcmp(app.user,m->user); size_t i;
        if(changed) { save(); profile_load(m->url,m->user); }
        copytext(app.url,sizeof(app.url),m->url); copytext(app.user,sizeof(app.user),m->user); copytext(app.cookie,sizeof(app.cookie),m->cookie);
        if(!app.profile) profile_load(app.url,app.user); put(app.profile,"cookie",app.cookie);
        if(changed) { cJSON_DeleteItemFromObjectCaseSensitive(app.profile,"http"); cJSON_AddBoolToObject(app.profile,"http",jb(app.settings,"private_http")); }
        app.connected=1; app.connecting=0; app.notice[0]=0;
        { cJSON *p=params(); cJSON_AddBoolToObject(p,"server_requests",1); rpc("client.capabilities",p,RpcCaps,NULL); }
        listchats(); for(i=0;i<app.count;i++) if(app.chats[i]->running || app.chats[i]==app.current) { app.chats[i]->pending=0; resume(app.chats[i]); }
        save(); sidebar(); render(); if(app.serverwin) net_rest(app.net,"/api/system/stats",RestStats,NULL);
    } else if(m->kind==NetOffline) {
        Pending *p; size_t i; app.connected=app.connecting=0; app.models_pending=0;
        while((p=app.pending)!=NULL) { app.pending=p->next; if(p->chat) p->chat->pending=0; free(p); }
        for(i=0;i<app.count;i++) app.chats[i]->pending=0;
        notice(m->error); if(app.serverwin) status_stats();
    } else if(m->kind==NetFrame) { if(app.connected) frame(m->json); }
    else if(m->kind==NetRest) {
        if(m->tag==RestStats) { cJSON_Delete(app.stats); app.stats=m->json?cJSON_Duplicate(m->json,1):NULL; if(*m->error) notice(m->error); status_stats(); }
        else if(m->tag==RestUpload) { Upload *u=m->context; app.uploads--; if(u) {
            if(m->json && *js(m->json,"path")) { cJSON *a=cJSON_CreateObject(); char *local=utf8(u->path),*name=utf8(wcsrchr(u->path,L'\\')?wcsrchr(u->path,L'\\')+1:u->path); const wchar_t *ext=wcsrchr(u->path,L'.');
                cJSON_AddStringToObject(a,"path",js(m->json,"path")); cJSON_AddStringToObject(a,"local",local); cJSON_AddStringToObject(a,"name",name); cJSON_AddStringToObject(a,"mime",ext&&(!_wcsicmp(ext,L".png")||!_wcsicmp(ext,L".jpg")||!_wcsicmp(ext,L".jpeg")||!_wcsicmp(ext,L".webp")||!_wcsicmp(ext,L".gif"))?"image/local":"application/octet-stream");
                cJSON_AddItemToArray(u->chat->attachments,a); free(local); free(name); if(u->chat==app.current) render(); schedule_save();
            } else notice(*m->error?m->error:"Attachment upload failed."); free(u);
        } controls(); }
    }
    cJSON_Delete(m->json); SecureZeroMemory(m->cookie,sizeof(m->cookie)); free(m);
}
static HWND child(HWND owner,const wchar_t *class,const wchar_t *text,DWORD style,int id) {
    HWND w=CreateWindowExW((!wcscmp(class,L"EDIT")||!wcscmp(class,WC_TREEVIEWW)||!wcscmp(class,MSFTEDIT_CLASS))?WS_EX_CLIENTEDGE:0,class,text,WS_CHILD|WS_VISIBLE|style,0,0,0,0,owner,(HMENU)(INT_PTR)id,app.instance,NULL);
    SendMessageW(w,WM_SETFONT,(WPARAM)app.normal,TRUE); return w;
}
static void move(HWND w,int x,int y,int width,int height) { MoveWindow(w,px(x),px(y),px(width),px(height),TRUE); }
static void layout(void) {
    int w=MulDiv(app.width,96,app.dpi),h=MulDiv(app.height,96,app.dpi),left=w<700?175:220; RECT box;
    move(app.fresh,10,12,left-20,29); move(app.search,10,51,left-20,26); move(app.tree,10,86,left-20,h-124);
    move(app.server,w-44,12,34,29); move(app.title,left+12,17,w-left-310,25); move(app.model,w-278,12,224,29); move(app.transcript,left+12,51,w-left-22,h-195);
    move(app.prompt,left+12,h-134,w-left-108,85); move(app.send,w-86,h-134,76,36); move(app.attach,w-86,h-88,76,30);
    move(app.request,left+12,h-39,140,25); move(app.status,160+left,h-39,w-left-175,25);
    box.left=px(14); box.top=px(12); box.right=px(w-left-36); box.bottom=px(h-221); SendMessageW(app.transcript,EM_SETRECT,0,(LPARAM)&box);
}
static void theme(void) {
    BOOL dark=app.dark; app.bg=app.dark?RGB(41,47,47):RGB(231,232,214); app.fg=app.dark?RGB(24,29,29):RGB(255,255,239); app.ink=app.dark?RGB(226,235,225):RGB(38,52,48); app.muted=app.dark?RGB(151,170,160):RGB(95,115,106); app.accent=app.dark?RGB(59,77,73):RGB(197,216,209);
    if(app.face) DeleteObject(app.face); if(app.paper) DeleteObject(app.paper); app.face=CreateSolidBrush(app.bg); app.paper=CreateSolidBrush(app.fg);
    TreeView_SetBkColor(app.tree,app.fg); TreeView_SetTextColor(app.tree,app.ink); DwmSetWindowAttribute(app.window,20,&dark,sizeof(dark));
    if(app.serverwin) DwmSetWindowAttribute(app.serverwin,20,&dark,sizeof(dark)); render(); InvalidateRect(app.window,NULL,TRUE); if(app.serverwin) InvalidateRect(app.serverwin,NULL,TRUE);
}
static LRESULT color_control(UINT msg,WPARAM wp,LPARAM lp) {
    HDC dc=(HDC)wp; HWND w=(HWND)lp; SetTextColor(dc,app.ink); SetBkColor(dc,msg==WM_CTLCOLOREDIT || w==app.transcript?app.fg:app.bg); return (LRESULT)(msg==WM_CTLCOLOREDIT?app.paper:app.face);
}
static void connect_to(const char *url,const char *user,const char *password,int allow,const char *cookie) {
    Pending *p; if(app.connecting) return; save(); app.connected=0; app.connecting=1;
    while((p=app.pending)!=NULL) { app.pending=p->next; if(p->chat) p->chat->pending=0; free(p); }
    app.notice[0]=0; net_connect(app.net,url,user,password,allow,cookie); controls();
}
static LRESULT CALLBACK server_proc(HWND w,UINT msg,WPARAM wp,LPARAM lp) {
    switch(msg) {
    case WM_CREATE: {
        HWND h; child(w,L"STATIC",L"server",0,0); move(GetWindow(w,GW_CHILD),18,12,480,28);
        child(w,L"STATIC",L"URL",0,501); move(GetDlgItem(w,501),18,49,70,24); h=child(w,L"EDIT",L"",WS_TABSTOP|ES_AUTOHSCROLL,502); move(h,90,45,386,28); settext(h,app.url); SendMessageW(h,EM_SETCUEBANNER,FALSE,(LPARAM)L"http://server:7777");
        h=child(w,L"EDIT",L"",WS_TABSTOP|ES_AUTOHSCROLL,503); move(h,18,87,200,28); settext(h,app.user); SendMessageW(h,EM_SETCUEBANNER,FALSE,(LPARAM)L"username");
        h=child(w,L"EDIT",L"",WS_TABSTOP|ES_PASSWORD|ES_AUTOHSCROLL,504); move(h,230,87,246,28); SendMessageW(h,EM_SETCUEBANNER,FALSE,(LPARAM)L"password");
        h=child(w,L"BUTTON",L"private HTTP",WS_TABSTOP|BS_AUTOCHECKBOX,506); move(h,18,127,180,26); SendMessageW(h,BM_SETCHECK,jb(app.profile,"http")?BST_CHECKED:BST_UNCHECKED,0);
        h=child(w,L"BUTTON",L"connect",WS_TABSTOP,505); move(h,376,125,100,29);
        h=child(w,L"EDIT",L"",ES_MULTILINE|ES_READONLY|WS_VSCROLL,507); move(h,18,197,458,235);
        h=child(w,L"STATIC",L"",0,508); move(h,18,164,458,26);
        h=child(w,L"BUTTON",L"refresh stats",WS_TABSTOP,IdRefresh); move(h,18,447,114,29);
        h=child(w,L"BUTTON",L"light / dark",WS_TABSTOP,IdTheme); move(h,140,447,114,29);
        h=child(w,L"BUTTON",L"about",WS_TABSTOP,IdAbout); move(h,262,447,94,29);
        h=child(w,L"BUTTON",L"done",WS_TABSTOP,IDCANCEL); move(h,364,447,112,29);
        h=child(w,L"BUTTON",L"forget saved login",WS_TABSTOP,IdForget); move(h,18,489,180,27); break;
    }
    case WM_COMMAND:
        switch(LOWORD(wp)) {
        case 505: {
            char *url=gettext(GetDlgItem(w,502)), *user=gettext(GetDlgItem(w,503)), *password=gettext(GetDlgItem(w,504)); int allow=SendMessageW(GetDlgItem(w,506),BM_GETCHECK,0,0)==BST_CHECKED;
            if(!*url||!*user) { notice("Enter a server URL and username."); }
            else { cJSON *profiles=cJSON_GetObjectItemCaseSensitive(app.settings,"profiles"),*p; const char *cookie=""; cJSON_ArrayForEach(p,profiles) if(!strcmp(js(p,"url"),url)&&!strcmp(js(p,"user"),user)) cookie=js(p,"cookie");
                if(app.profile && !strcmp(js(app.profile,"url"),url)) { cJSON_DeleteItemFromObjectCaseSensitive(app.profile,"http"); cJSON_AddBoolToObject(app.profile,"http",allow); }
                put(app.settings,"connecting_url",url); cJSON_DeleteItemFromObjectCaseSensitive(app.settings,"private_http"); cJSON_AddBoolToObject(app.settings,"private_http",allow);
                connect_to(url,user,password,allow,cookie); }
            SetWindowTextW(GetDlgItem(w,504),L""); SecureZeroMemory(password,strlen(password)); free(password); free(url); free(user); break;
        }
        case IdRefresh: if(app.connected) net_rest(app.net,"/api/system/stats",RestStats,NULL); break;
        case IdTheme: app.dark=!app.dark; theme(); schedule_save(); break;
        case IdAbout: MessageBoxW(w,L"lcb-hermes 0.4.0\n\nLarkin Computing Bureau\nNative C / Windows client.\n\nInterface pieces from lcb-ai.\nJSON parsing: cJSON (MIT).\nNo ads. No purchases.",L"about",MB_OK); break;
        case IdForget:
            if(MessageBoxW(w,L"Forget the saved login on this PC? Server chats stay saved.",L"forget login",MB_OKCANCEL|MB_ICONQUESTION)==IDOK) { if(app.profile) put(app.profile,"cookie",""); SecureZeroMemory(app.cookie,sizeof(app.cookie)); save(); notice("Saved login removed. It will be required next time."); } break;
        case IDCANCEL: DestroyWindow(w); break;
        } return 0;
    case WM_CLOSE: DestroyWindow(w); return 0;
    case WM_DESTROY: app.serverwin=NULL; SetFocus(app.prompt); return 0;
    case WM_CTLCOLORSTATIC: case WM_CTLCOLOREDIT: return color_control(msg,wp,lp);
    case WM_ERASEBKGND: { RECT r; GetClientRect(w,&r); FillRect((HDC)wp,&r,app.face); return 1; }
    }
    return DefWindowProcW(w,msg,wp,lp);
}
static void show_server(void) {
    if(app.serverwin) { SetForegroundWindow(app.serverwin); return; }
    app.serverwin=CreateWindowExW(WS_EX_DLGMODALFRAME,L"LCBHermesServer",L"server",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU,0,0,px(510),px(566),app.window,NULL,app.instance,NULL);
    { RECT parent; GetWindowRect(app.window,&parent); SetWindowPos(app.serverwin,NULL,parent.left+px(120),parent.top+px(55),0,0,SWP_NOSIZE|SWP_NOZORDER); }
    ShowWindow(app.serverwin,SW_SHOW); controls(); status_stats(); if(app.connected) net_rest(app.net,"/api/system/stats",RestStats,NULL);
}
static void models(void) {
    HMENU menu; const cJSON *provider,*model; char names[512][160],providers[512][160]; int count=1,choice; RECT rect;
    if(app.models_pending||app.models_open) return;
    if(!app.models) { cJSON *p=params(); cJSON_AddBoolToObject(p,"include_unconfigured",0); app.models_pending=1; rpc("model.options",p,RpcModels,NULL); return; }
    menu=CreatePopupMenu(); AppendMenuW(menu,MF_STRING,1,L"server default"); names[0][0]=providers[0][0]=0;
    cJSON_ArrayForEach(provider,cJSON_GetObjectItemCaseSensitive(app.models,"providers")) if(jb(provider,"authenticated")||jb(provider,"is_current")) cJSON_ArrayForEach(model,cJSON_GetObjectItemCaseSensitive(provider,"models")) {
        wchar_t *label; if(!cJSON_IsString(model)||count>=512) continue; copytext(names[count],160,model->valuestring); copytext(providers[count],160,js(provider,"slug")); label=wide(model->valuestring); AppendMenuW(menu,MF_STRING,(UINT_PTR)count+1,label); free(label); count++;
    }
    app.models_open=1; GetWindowRect(app.model,&rect); choice=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_NONOTIFY,rect.left,rect.bottom,0,app.window,NULL); app.models_open=0; DestroyMenu(menu); cJSON_Delete(app.models); app.models=NULL;
    if(choice>0&&choice<=count) { if(app.current && *app.current->stored) { if(MessageBoxW(app.window,L"Use this model in a new chat? The current chat stays saved.",L"model",MB_OKCANCEL)!=IDOK) return; newchat(); }
        copytext(app.newmodel,sizeof(app.newmodel),names[choice-1]); copytext(app.newprovider,sizeof(app.newprovider),providers[choice-1]); controls(); }
}
static void answer(cJSON *request,cJSON *value,const char *error) {
    cJSON *frame=params(); size_t i; cJSON_AddStringToObject(frame,"jsonrpc","2.0"); cJSON_AddItemToObject(frame,"id",cJSON_Duplicate(cJSON_GetObjectItemCaseSensitive(request,"id"),1));
    if(error) { cJSON *e=params(); cJSON_AddNumberToObject(e,"code",-32000); cJSON_AddStringToObject(e,"message",error); cJSON_AddItemToObject(frame,"error",e); cJSON_Delete(value); }
    else cJSON_AddItemToObject(frame,"result",value);
    net_send(app.net,frame); cJSON_Delete(frame);
    for(i=0;i<app.count;i++) { int j; cJSON *list=app.chats[i]->requests; for(j=cJSON_GetArraySize(list)-1;j>=0;j--) { cJSON *item=cJSON_GetArrayItem(list,j); if(cJSON_Compare(cJSON_GetObjectItemCaseSensitive(item,"id"),cJSON_GetObjectItemCaseSensitive(request,"id"),1)) cJSON_DeleteItemFromArray(list,j); } }
    controls();
}
typedef struct Question { HWND window; cJSON *request,*questions,*answers; int index,done; } Question;
static LRESULT CALLBACK question_proc(HWND w,UINT msg,WPARAM wp,LPARAM lp) {
    Question *q=(Question *)GetWindowLongPtrW(w,GWLP_USERDATA);
    if(msg==WM_CREATE) { q=((CREATESTRUCTW *)lp)->lpCreateParams; SetWindowLongPtrW(w,GWLP_USERDATA,(LONG_PTR)q); child(w,L"STATIC",L"",0,600); move(GetDlgItem(w,600),18,18,444,185); child(w,L"EDIT",L"",WS_TABSTOP|ES_MULTILINE|ES_AUTOVSCROLL|WS_VSCROLL,601); move(GetDlgItem(w,601),18,214,444,82); child(w,L"BUTTON",L"answer",WS_TABSTOP,IDOK); move(GetDlgItem(w,IDOK),244,312,105,30); child(w,L"BUTTON",L"cancel",WS_TABSTOP,IDCANCEL); move(GetDlgItem(w,IDCANCEL),357,312,105,30); return 0; }
    if(msg==WM_COMMAND && q) {
        if(LOWORD(wp)==IDOK) { const cJSON *item=cJSON_GetArrayItem(q->questions,q->index); char *text=gettext(GetDlgItem(w,601)), key[40]; const char *qid=js(item,"qid"); snprintf(key,sizeof(key),"%d",q->index); put(q->answers,*qid?qid:key,text); free(text); q->index++;
            if(q->index>=cJSON_GetArraySize(q->questions)) { cJSON *value=params(); if(q->index==1 && !*js(item,"qid")) cJSON_AddStringToObject(value,"answer",js(q->answers,"0")); else cJSON_AddItemToObject(value,"answers",cJSON_Duplicate(q->answers,1)); answer(q->request,value,NULL); q->done=1; DestroyWindow(w); }
            else { settext(GetDlgItem(w,600),js(cJSON_GetArrayItem(q->questions,q->index),"question")); SetWindowTextW(GetDlgItem(w,601),L""); } return 0;
        }
        if(LOWORD(wp)==IDCANCEL) { answer(q->request,NULL,"User cancelled clarification."); q->done=1; DestroyWindow(w); return 0; }
    }
    if(msg==WM_CLOSE && q) { answer(q->request,NULL,"User cancelled clarification."); q->done=1; DestroyWindow(w); return 0; }
    if(msg==WM_CTLCOLORSTATIC||msg==WM_CTLCOLOREDIT) return color_control(msg,wp,lp);
    return DefWindowProcW(w,msg,wp,lp);
}
static void request_dialog(void) {
    cJSON *r,*p,*copy; Chat *c=app.current; if(!c || !cJSON_GetArraySize(c->requests)) return;
    r=cJSON_GetArrayItem(c->requests,0); copy=cJSON_Duplicate(r,1); p=cJSON_GetObjectItemCaseSensitive(copy,"params");
    if(!strcmp(js(r,"method"),"approval")) {
        wchar_t *description=wide(js(p,"description")), *command=wide(js(p,"command")); wchar_t text[4096]; cJSON *choices=cJSON_GetObjectItemCaseSensitive(p,"choices"),*choice; int once=!choices,selected; cJSON *v=params();
        cJSON_ArrayForEach(choice,choices) if(cJSON_IsString(choice)&&!strcmp(choice->valuestring,"once")) once=1;
        swprintf(text,4096,L"%ls\n\n%ls\n\n%ls",description?description:L"Allow this action?",command?command:L"",once?L"Yes allows this action once. No denies it.":L"Only deny is available in this client."); free(description); free(command);
        selected=MessageBoxW(app.window,text,L"Hermes asks for approval",once?MB_YESNO|MB_DEFBUTTON2|MB_ICONQUESTION:MB_OK|MB_ICONINFORMATION);
        cJSON_AddStringToObject(v,"choice",once&&selected==IDYES?"once":"deny"); answer(copy,v,NULL);
    } else if(!strcmp(js(r,"method"),"clarify")) {
        Question q={0}; MSG msg; q.request=copy; q.questions=cJSON_GetObjectItemCaseSensitive(p,"questions"); if(!cJSON_IsArray(q.questions)) { q.questions=cJSON_CreateArray(); cJSON_AddItemToArray(q.questions,cJSON_Duplicate(p,1)); } else q.questions=cJSON_Duplicate(q.questions,1); q.answers=params();
        q.window=CreateWindowExW(WS_EX_DLGMODALFRAME,L"LCBHermesQuestion",L"Hermes has a question",WS_CAPTION|WS_SYSMENU,100,100,px(495),px(390),app.window,NULL,app.instance,&q);
        settext(GetDlgItem(q.window,600),js(cJSON_GetArrayItem(q.questions,0),"question")); EnableWindow(app.window,FALSE); ShowWindow(q.window,SW_SHOW);
        while(!q.done&&GetMessageW(&msg,NULL,0,0)>0) { if(!IsDialogMessageW(q.window,&msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); } }
        EnableWindow(app.window,TRUE); SetForegroundWindow(app.window); cJSON_Delete(q.questions); cJSON_Delete(q.answers);
    } else answer(copy,NULL,"This request type is not supported by this client.");
    cJSON_Delete(copy);
}
static void attach(void) {
    OPENFILENAMEW ofn={0}; wchar_t path[32768]=L""; Upload *u;
    if(!app.connected||!app.current||app.current->running||app.current->pending) return;
    ofn.lStructSize=sizeof(ofn); ofn.hwndOwner=app.window; ofn.lpstrFile=path; ofn.nMaxFile=32768; ofn.lpstrFilter=L"Images and files\0*.png;*.jpg;*.jpeg;*.webp;*.gif;*.txt;*.md;*.pdf\0All files\0*.*\0"; ofn.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
    if(!GetOpenFileNameW(&ofn)) return; u=calloc(1,sizeof(*u)); if(!u) return; u->chat=app.current; wcscpy(u->path,path); app.uploads++; controls(); net_upload(app.net,path,RestUpload,u);
}
static void export_chat(void) {
    OPENFILENAMEW ofn={0}; wchar_t path[32768]=L"chat.md"; HANDLE file; DWORD written; const cJSON *r; char *output=NULL; size_t used=0;
    if(!app.current) return; ofn.lStructSize=sizeof(ofn); ofn.hwndOwner=app.window; ofn.lpstrFile=path; ofn.nMaxFile=32768; ofn.lpstrFilter=L"Markdown\0*.md\0"; ofn.lpstrDefExt=L"md"; ofn.Flags=OFN_OVERWRITEPROMPT|OFN_NOCHANGEDIR;
    if(!GetSaveFileNameW(&ofn)) return;
    cJSON_ArrayForEach(r,app.current->messages) { char *text=row_text(r); size_t n=strlen(text)+strlen(js(r,"role"))+10; char *next=realloc(output,used+n); if(!next) { free(text); free(output); return; } output=next; used+=(size_t)snprintf(output+used,n,"## %s\n\n%s\n\n",js(r,"role"),text); free(text); }
    file=CreateFileW(path,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL); if(file==INVALID_HANDLE_VALUE || !WriteFile(file,output,(DWORD)used,&written,NULL)||written!=used) notice("Could not export this chat."); if(file!=INVALID_HANDLE_VALUE) CloseHandle(file); free(output);
}
static LRESULT CALLBACK prompt_proc(HWND w,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR ref) {
    (void)id;(void)ref;
    if(msg==WM_KEYDOWN&&wp==VK_RETURN&&GetKeyState(VK_CONTROL)<0) { submit(); return 0; }
    return DefSubclassProc(w,msg,wp,lp);
}
static LRESULT CALLBACK window_proc(HWND w,UINT msg,WPARAM wp,LPARAM lp) {
    switch(msg) {
    case WM_CREATE:
        app.window=w; app.dpi=(int)GetDpiForWindow(w); app.rich=LoadLibraryW(L"Msftedit.dll");
        app.normal=CreateFontW(-px(13),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI"); app.fixed=CreateFontW(-px(13),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,FIXED_PITCH,L"Consolas");
        app.fresh=child(w,L"BUTTON",L"+  new chat",WS_TABSTOP,IdNew); app.server=child(w,L"BUTTON",L"",WS_TABSTOP|BS_ICON,IdServer); app.title=child(w,L"STATIC",L"new chat",SS_ENDELLIPSIS,0); app.model=child(w,L"BUTTON",L"server default",WS_TABSTOP,IdModel); app.search=child(w,L"EDIT",L"",WS_TABSTOP|ES_AUTOHSCROLL,IdSearch); SendMessageW(app.search,EM_SETCUEBANNER,FALSE,(LPARAM)L"search chats");
        app.tree=child(w,WC_TREEVIEWW,L"",WS_TABSTOP|TVS_SHOWSELALWAYS|TVS_INFOTIP,IdTree); app.icons=classic_icons(app.dpi); TreeView_SetImageList(app.tree,app.icons,TVSIL_NORMAL); app.servericon=ImageList_GetIcon(app.icons,ClassicOutput,ILD_NORMAL); SendMessageW(app.server,BM_SETIMAGE,IMAGE_ICON,(LPARAM)app.servericon);
        app.tooltip=CreateWindowExW(WS_EX_TOPMOST,TOOLTIPS_CLASSW,NULL,WS_POPUP|TTS_ALWAYSTIP,0,0,0,0,w,NULL,app.instance,NULL); { TOOLINFOW t={0}; t.cbSize=sizeof(t); t.uFlags=TTF_IDISHWND|TTF_SUBCLASS; t.hwnd=w; t.uId=(UINT_PTR)app.server; t.lpszText=L"server / stats / appearance"; SendMessageW(app.tooltip,TTM_ADDTOOLW,0,(LPARAM)&t); }
        app.transcript=child(w,MSFTEDIT_CLASS,L"",WS_TABSTOP|ES_MULTILINE|ES_READONLY|WS_VSCROLL,0); SendMessageW(app.transcript,EM_EXLIMITTEXT,0,WIRE_LIMIT); SetWindowSubclass(app.transcript,transcript_proc,1,0); SendMessageW(app.transcript,EM_SETOPTIONS,ECOOP_OR,ECO_NOHIDESEL);
        app.prompt=child(w,L"EDIT",L"",WS_TABSTOP|ES_MULTILINE|ES_AUTOVSCROLL|WS_VSCROLL,IdPrompt); SendMessageW(app.prompt,EM_SETLIMITTEXT,256*1024,0); SetWindowSubclass(app.prompt,prompt_proc,1,0);
        app.send=child(w,L"BUTTON",L"send",WS_TABSTOP,IdSend); app.attach=child(w,L"BUTTON",L"attach",WS_TABSTOP,IdAttach); app.request=child(w,L"BUTTON",L"export chat",WS_TABSTOP,IdRequest); app.status=child(w,L"STATIC",L"offline",SS_LEFTNOWORDWRAP,0); app.net=net_new(w);
        theme(); SetTimer(w,IdHeartbeat,20000,NULL); SetTimer(w,IdTimeout,1000,NULL); return 0;
    case WM_SIZE: app.width=LOWORD(lp); app.height=HIWORD(lp); if(app.width&&app.height) layout(); return 0;
    case WM_GETMINMAXINFO: ((MINMAXINFO *)lp)->ptMinTrackSize.x=px(620); ((MINMAXINFO *)lp)->ptMinTrackSize.y=px(450); return 0;
    case WM_DPICHANGED: {
        RECT *r=(RECT *)lp; app.dpi=LOWORD(wp); SetWindowPos(w,NULL,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE); return 0;
    }
    case NET_MESSAGE: on_net((NetMessage *)lp); return 0;
    case WM_COMMAND:
        switch(LOWORD(wp)) {
        case IdNew: newchat(); break;
        case IdServer: show_server(); break;
        case IdModel: if(app.connected) models(); else notice("Connect before choosing a model."); break;
        case IdSend: submit(); break;
        case IdAttach: attach(); break;
        case IdRequest: if(app.current&&cJSON_GetArraySize(app.current->requests)) request_dialog(); else export_chat(); break;
        case IdPrompt: if(HIWORD(wp)==EN_CHANGE && !app.restoring && app.current) { free(app.current->draft); app.current->draft=gettext(app.prompt); schedule_save(); } break;
        case IdSearch: if(HIWORD(wp)==EN_CHANGE) sidebar(); break;
        case IdReconnect: if(*app.url) connect_to(app.url,app.user,"",jb(app.profile,"http"),app.cookie); else show_server(); break;
        case IdRefresh: if(app.connected) listchats(); break;
        case IdRemoveAttachment: if(app.current && !app.current->pending) { cJSON_Delete(app.current->attachments); app.current->attachments=cJSON_CreateArray(); render(); schedule_save(); } break;
        case IdDelete:
            if(app.current && !app.current->running && !app.current->pending && !app.uploads && app.connected) {
                if(MessageBoxW(w,L"Delete this chat on the server? This cannot be undone.",L"delete chat",MB_OKCANCEL|MB_DEFBUTTON2|MB_ICONQUESTION)==IDOK) {
                    Chat *c=app.current; c->pending=1;
                    if(!*c->stored) { Pending local={0}; local.kind=RpcDelete; local.chat=c; result(&local,NULL,""); }
                    else if(*c->id) rpc("session.close",sessionparams(c),RpcCloseDelete,c);
                    else { cJSON *p=params(); cJSON_AddStringToObject(p,"session_id",c->stored); rpc("session.delete",p,RpcDelete,c); }
                }
            } break;
        }
        return 0;
    case WM_NOTIFY: {
        NMHDR *h=(NMHDR *)lp;
        if(h->hwndFrom==app.tree && h->code==TVN_SELCHANGEDW && !app.treebusy) { Chat *c=(Chat *)((NMTREEVIEWW *)lp)->itemNew.lParam; if(c&&c!=app.current) { app.current=c; c->unread=0; app.notice[0]=0; if(*c->stored&&!c->loaded) resume(c); render(); schedule_save(); } }
        if(h->hwndFrom==app.tree && h->code==TVN_KEYDOWN && ((NMTVKEYDOWN *)lp)->wVKey==VK_DELETE) PostMessageW(w,WM_COMMAND,IdDelete,0);
        return 0;
    }
    case WM_CONTEXTMENU: {
        if((HWND)wp==app.tree) { HMENU menu=CreatePopupMenu(); POINT point={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
            if(point.x==-1) { RECT r; GetWindowRect(app.tree,&r); point.x=r.left+20; point.y=r.top+20; }
            AppendMenuW(menu,MF_STRING,IdRefresh,L"refresh chats"); AppendMenuW(menu,MF_STRING,IdDelete,L"delete chat..."); AppendMenuW(menu,MF_STRING,IdRemoveAttachment,L"clear draft attachments"); TrackPopupMenu(menu,TPM_RIGHTBUTTON,point.x,point.y,0,w,NULL); DestroyMenu(menu);
        } return 0;
    }
    case WM_TIMER:
        if(wp==IdRender) render(); else if(wp==IdSave) save(); else if(wp==IdHeartbeat&&app.connected) rpc("ping",params(),RpcPing,NULL);
        else if(wp==IdTimeout) { Pending **link=&app.pending; ULONGLONG now=GetTickCount64(); while(*link) { Pending *p=*link; if(now-p->started>90000) { *link=p->next; result(p,NULL,"Server request timed out. The request was not retried."); free(p); } else link=&p->next; } } return 0;
    case WM_CTLCOLORSTATIC: case WM_CTLCOLOREDIT: return color_control(msg,wp,lp);
    case WM_ERASEBKGND: { RECT r; GetClientRect(w,&r); FillRect((HDC)wp,&r,app.face); return 1; }
    case WM_PAINT: { PAINTSTRUCT paint; HDC dc=BeginPaint(w,&paint); RECT r; GetClientRect(app.tree,&r); MapWindowPoints(app.tree,w,(POINT *)&r,2); InflateRect(&r,2,2); classic_edge(dc,r,0); EndPaint(w,&paint); return 0; }
    case WM_CLOSE: save(); if(app.serverwin) DestroyWindow(app.serverwin); DestroyWindow(w); return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(w,msg,wp,lp);
}
static int create_window(HINSTANCE instance,int show) {
    WNDCLASSEXW wc={0}; INITCOMMONCONTROLSEX cc={sizeof(cc),ICC_TREEVIEW_CLASSES|ICC_STANDARD_CLASSES}; HWND w;
    app.instance=instance; app.dpi=96; InitCommonControlsEx(&cc); CoInitializeEx(NULL,COINIT_APARTMENTTHREADED);
    wc.cbSize=sizeof(wc); wc.hInstance=instance; wc.hCursor=LoadCursorW(NULL,IDC_ARROW); wc.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(101)); wc.lpszClassName=L"LCBHermes"; wc.lpfnWndProc=window_proc; RegisterClassExW(&wc);
    wc.lpszClassName=L"LCBHermesServer"; wc.lpfnWndProc=server_proc; RegisterClassExW(&wc); wc.lpszClassName=L"LCBHermesQuestion"; wc.lpfnWndProc=question_proc; RegisterClassExW(&wc);
    w=CreateWindowExW(0,L"LCBHermes",L"lcb-hermes",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,1000,700,NULL,NULL,instance,NULL); if(!w) return 0; ShowWindow(w,show); UpdateWindow(w); return 1;
}
static void cleanup(void) {
    MSG msg; Pending *p; net_free(app.net); while(PeekMessageW(&msg,app.window,NET_MESSAGE,NET_MESSAGE,PM_REMOVE)) { NetMessage *m=(NetMessage *)msg.lParam; if(m->tag==RestUpload) free(m->context); cJSON_Delete(m->json); free(m); }
    while((p=app.pending)!=NULL) { app.pending=p->next; free(p); } clear_chats(); previews_clear(); cJSON_Delete(app.settings); cJSON_Delete(app.stats); cJSON_Delete(app.models);
    if(app.servericon) DestroyIcon(app.servericon); if(app.icons) ImageList_Destroy(app.icons); if(app.normal) DeleteObject(app.normal); if(app.fixed) DeleteObject(app.fixed); if(app.face) DeleteObject(app.face); if(app.paper) DeleteObject(app.paper); if(app.rich) FreeLibrary(app.rich); CoUninitialize();
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE previous,PWSTR command,int show) {
    MSG msg; cJSON *profiles,*p; (void)previous;(void)command;
    if(!store_path(app.path,1024)) return 1; app.settings=store_read(app.path); if(!app.settings) { app.corrupt=1; app.settings=cJSON_CreateObject(); }
    app.dark=jb(app.settings,"dark"); copytext(app.url,sizeof(app.url),js(app.settings,"active_url")); copytext(app.user,sizeof(app.user),js(app.settings,"active_user"));
    if(!create_window(instance,show)) return 1;
    profile_load(app.url,app.user); sidebar(); render(); if(app.corrupt) notice("Saved settings could not be read. They were preserved; reconnect without saving.");
    profiles=cJSON_GetObjectItemCaseSensitive(app.settings,"profiles"); cJSON_ArrayForEach(p,profiles) if(!strcmp(js(p,"url"),app.url)&&!strcmp(js(p,"user"),app.user)) { copytext(app.cookie,sizeof(app.cookie),js(p,"cookie")); break; }
    if(*app.url&&*app.cookie) connect_to(app.url,app.user,"",jb(app.profile,"http"),app.cookie);
    while(GetMessageW(&msg,NULL,0,0)>0) { if(app.serverwin&&IsDialogMessageW(app.serverwin,&msg)) continue;
        if(msg.message==WM_KEYDOWN&&GetKeyState(VK_CONTROL)<0) { if(msg.wParam=='N') { newchat(); continue; } if(msg.wParam=='R') { SendMessageW(app.window,WM_COMMAND,IdReconnect,0); continue; } }
        if(msg.message==WM_KEYDOWN&&msg.wParam==VK_TAB&&IsDialogMessageW(app.window,&msg)) continue;
        TranslateMessage(&msg); DispatchMessageW(&msg);
    }
    cleanup(); return 0;
}
