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
#include <shellapi.h>
#include <uxtheme.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "core.h"
#include "transport_win.h"
#include "store_win.h"
#include "classic_win.h"
#include "markdown.h"

enum { IdTree=100,IdPrompt,IdSend,IdNew,IdServer,IdModel,IdSearch,IdAttach,IdRequest,IdExport,IdReconnect,IdRefresh,IdRename,IdDelete,IdForget,IdAbout,IdDraft,IdRemoveAttachment,IdWorkspace,IdProjects,IdMonitor,IdExpand,IdCollapse,IdExit,IdWider,IdNarrower,IdRender=300,IdSave,IdHeartbeat,IdTimeout,IdStats,IdList };
enum { RpcCaps=1,RpcList,RpcResume,RpcCreate,RpcSubmit,RpcStop,RpcModels,RpcAttach,RpcRename,RpcDelete,RpcPing,RpcCloseDelete,RpcResumeSend,RpcProjects,RpcProjectCreate,RpcProjectAdd,RpcDefaultModel,RpcProjectTree };
enum { RestStats=1,RestUpload };
typedef struct Pending { int id, kind; Chat *chat; ULONGLONG started; struct Pending *next; } Pending;
typedef struct Preview { HBITMAP bitmap; LONG anchor; int width,height; } Preview;
typedef struct Upload { Chat *chat; wchar_t path[32768]; } Upload;
typedef struct TreeRow { Chat *chat; HTREEITEM item,parent; } TreeRow;
typedef struct TreeGroup { char path[1024]; HTREEITEM item; } TreeGroup;
typedef struct RenderRow { unsigned long long hash; LONG anchor; int visible; } RenderRow;
static struct {
    HINSTANCE instance; HWND window,tree,search,prompt,transcript,send,fresh,server,model,title,status,attach,request,serverwin,tooltip;
    HWND explorerhead,monitorhead,monitor,activityhead,activitytitle,activityview,workspace,workspacelabel,projectbutton,summary,projectwin,progress,aboutwin,hotbutton;
    HDWP positions;
    HFONT normal,fixed,heading,brandfont; HBRUSH face,paper; HIMAGELIST icons; HMODULE rich;
    HBITMAP nouslogo; int logo_width,logo_height;
    int dpi,width,height,connected,connecting,restoring,nextid,treebusy,models_pending,models_open,uploads,corrupt;
    int sidebar_width,split_x,dragging,show_monitor,monitor_visible,monitor_x,stats_pending,projects_supported;
    int list_pending,list_again,render_pending,project_pending;
    COLORREF ink,muted,bg,fg,accent; wchar_t path[1024]; char url[1024],user[256],cookie[8192],notice[512],version[80],newmodel[160],newprovider[160],epoch[160];
    char default_model[160],newcwd[1024]; ULONGLONG stats_at,turn_at;
    Net *net; cJSON *settings,*profile,*stats,*models,*projects,*project_tree; Pending *pending; Chat **chats,*current; size_t count,capacity;
    TreeRow rows[1000]; size_t nrows; TreeGroup groups[1000]; size_t ngroups;
    Preview previews[128]; int npreviews; char *status_text;
    Chat *view_chat,*activity_chat; RenderRow *view_rows; size_t view_count,view_capacity; LONG view_tail;
    unsigned long long view_attachments; int view_valid,paragraph_start,speaker;
    char *activity_text;
} app;
static void render(void), sidebar(void), save(void), controls(void), show_server(void), resume(Chat *c), submit(void), start_send(Chat *c), request_dialog(void), layout(void), project_choices(void), show_projects(void);
static const cJSON *chosen_project(HWND w);
static void choose_workspace(const char *path);
static void show_about(HWND owner);
static void draw_button(DRAWITEMSTRUCT *d);
static void activity_render(const char *text);
static LRESULT CALLBACK monitor_proc(HWND w,UINT msg,WPARAM wp,LPARAM lp);
static void update_fonts(void);
static int px(int n) { return MulDiv(n,app.dpi,96); }
static void copytext(char *dst,size_t cap,const char *src) { snprintf(dst,cap,"%s",src); }
static void settext(HWND w,const char *s) { wchar_t *v=wide(s); if(v && w) { int n=GetWindowTextLengthW(w); wchar_t *old=calloc((size_t)n+1,sizeof(*old)); if(old) { GetWindowTextW(w,old,n+1); if(wcscmp(old,v)) SetWindowTextW(w,v); free(old); } } free(v); }
static char *gettext(HWND w) { int n=GetWindowTextLengthW(w); wchar_t *v=calloc((size_t)n+1,sizeof(*v)); char *s; if(!v) return textdup(""); GetWindowTextW(w,v,n+1); s=utf8(v); free(v); return s?s:textdup(""); }
static void put(cJSON *o,const char *key,const char *s) { cJSON_DeleteItemFromObjectCaseSensitive(o,key); cJSON_AddStringToObject(o,key,s); }
static cJSON *params(void) { return cJSON_CreateObject(); }
static cJSON *sessionparams(Chat *c) { cJSON *p=params(); cJSON_AddStringToObject(p,"session_id",*c->id?c->id:c->stored); return p; }
static void schedule_save(void) { if(!app.restoring) SetTimer(app.window,IdSave,750,NULL); }
static void notice(const char *s) { copytext(app.notice,sizeof(app.notice),s); controls(); }
static void freshkey(char out[65]) { GUID id; if(SUCCEEDED(CoCreateGuid(&id))) snprintf(out,65,"%08lx%04x%04x%02x%02x%02x%02x%02x%02x%02x%02x",(unsigned long)id.Data1,id.Data2,id.Data3,id.Data4[0],id.Data4[1],id.Data4[2],id.Data4[3],id.Data4[4],id.Data4[5],id.Data4[6],id.Data4[7]); else snprintf(out,65,"%lu-%llu",(unsigned long)GetCurrentProcessId(),(unsigned long long)GetTickCount64()); }
static int addchat(Chat *c) { if(!c) return 0; if(app.count==app.capacity) { size_t cap=app.capacity?app.capacity*2:16; Chat **next=realloc(app.chats,cap*sizeof(*next)); if(!next) { chat_free(c); return 0; } app.chats=next; app.capacity=cap; } app.chats[app.count++]=c; return 1; }
static Chat *findchat(const char *id) { size_t i; if(!*id) return NULL; for(i=0;i<app.count;i++) if(!strcmp(id,app.chats[i]->id)||!strcmp(id,app.chats[i]->stored)||!strcmp(id,app.chats[i]->key)) return app.chats[i]; return NULL; }
static void newchat(void) { char key[65]; Chat *c; freshkey(key); c=chat_new(key); if(!c) return; copytext(c->cwd,sizeof(c->cwd),app.newcwd); if(!addchat(c)) return; app.current=c; app.notice[0]=0; sidebar(); render(); schedule_save(); SetFocus(app.prompt); }
static void clear_chats(void) { size_t i; app.view_valid=0; app.view_chat=app.activity_chat=NULL; app.treebusy=1; TreeView_DeleteAllItems(app.tree); app.nrows=app.ngroups=0; app.treebusy=0; for(i=0;i<app.count;i++) chat_free(app.chats[i]); free(app.chats); app.chats=NULL; app.count=app.capacity=0; app.current=NULL; }
static void profile_load(const char *url,const char *user) {
    cJSON *profiles=cJSON_GetObjectItemCaseSensitive(app.settings,"profiles"),*p,*a,*v; const char *active;
    clear_chats(); app.profile=NULL;
    app.newmodel[0]=app.newprovider[0]=app.newcwd[0]=app.default_model[0]=0; cJSON_Delete(app.projects); app.projects=NULL; cJSON_Delete(app.project_tree); app.project_tree=NULL; app.projects_supported=0;
    if(!cJSON_IsArray(profiles)) { profiles=cJSON_CreateArray(); cJSON_AddItemToObject(app.settings,"profiles",profiles); }
    cJSON_ArrayForEach(p,profiles) if(!strcmp(js(p,"url"),url)&&!strcmp(js(p,"user"),user)) { app.profile=p; break; }
    if(!app.profile) { app.profile=cJSON_CreateObject(); put(app.profile,"url",url); put(app.profile,"user",user); cJSON_AddItemToArray(profiles,app.profile); }
    copytext(app.newcwd,sizeof(app.newcwd),js(app.profile,"workspace"));
    a=cJSON_GetObjectItemCaseSensitive(app.profile,"chats"); cJSON_ArrayForEach(v,a) if(app.count<1000) addchat(chat_restore(v));
    active=js(app.profile,"active"); app.current=findchat(active); if(!app.current&&app.count) app.current=app.chats[0]; if(!app.current) newchat();
    put(app.settings,"active_url",url); put(app.settings,"active_user",user);
    project_choices();
}
static void save(void) {
    cJSON *a; size_t i; KillTimer(app.window,IdSave); if(!app.settings||app.corrupt) return;
    cJSON_DeleteItemFromObjectCaseSensitive(app.settings,"dark");
    cJSON_DeleteItemFromObjectCaseSensitive(app.settings,"sidebar_width"); cJSON_AddNumberToObject(app.settings,"sidebar_width",app.sidebar_width);
    cJSON_DeleteItemFromObjectCaseSensitive(app.settings,"monitor"); cJSON_AddBoolToObject(app.settings,"monitor",app.show_monitor);
    if(app.profile) { a=cJSON_CreateArray(); for(i=0;i<app.count;i++) cJSON_AddItemToArray(a,chat_json(app.chats[i])); cJSON_DeleteItemFromObjectCaseSensitive(app.profile,"chats"); cJSON_AddItemToObject(app.profile,"chats",a); put(app.profile,"active",app.current?app.current->key:""); }
    if(!store_write(app.path,app.settings)) notice("Could not save settings. Your server chats are safe.");
}
static int rpc(const char *method,cJSON *p,int kind,Chat *chat) {
    cJSON *frame; Pending *wait;
    if(!app.connected) { cJSON_Delete(p); notice("Connect to your server first."); return 0; }
    wait=calloc(1,sizeof(*wait)); if(!wait) { cJSON_Delete(p); return 0; } wait->id=++app.nextid; wait->kind=kind; wait->chat=chat; wait->started=GetTickCount64(); wait->next=app.pending; app.pending=wait;
    frame=cJSON_CreateObject(); cJSON_AddStringToObject(frame,"jsonrpc","2.0"); cJSON_AddNumberToObject(frame,"id",wait->id); cJSON_AddStringToObject(frame,"method",method); cJSON_AddItemToObject(frame,"params",p); net_send(app.net,frame); cJSON_Delete(frame); return wait->id;
}
static void listchats(void) { cJSON *p; KillTimer(app.window,IdList); if(app.list_pending) { app.list_again=1; return; } p=params(); cJSON_AddNumberToObject(p,"limit",500); app.list_pending=rpc("session.list",p,RpcList,NULL)!=0; }
static void project_tree(void) { cJSON *p=params(); cJSON_AddNumberToObject(p,"preview_limit",500); cJSON_AddNumberToObject(p,"session_limit",500); rpc("projects.tree",p,RpcProjectTree,NULL); }
static void tree_metadata(void) {
    const cJSON *p,*s;
    cJSON_ArrayForEach(p,cJSON_GetObjectItemCaseSensitive(app.project_tree,"projects")) cJSON_ArrayForEach(s,cJSON_GetObjectItemCaseSensitive(p,"previewSessions")) {
        Chat *c=findchat(js(s,"id")); if(!c||c->loaded) continue;
        copytext(c->cwd,sizeof(c->cwd),js(s,"cwd")); copytext(c->project_id,sizeof(c->project_id),jb(p,"isNoProject")?"":js(p,"id")); copytext(c->project_name,sizeof(c->project_name),jb(p,"isNoProject")?"":js(p,"label"));
    }
}
static void resume(Chat *c) { cJSON *p; if(!c||!*c->stored||c->pending||!app.connected) return; p=params(); cJSON_AddStringToObject(p,"session_id",c->stored); cJSON_AddStringToObject(p,"source","desktop"); cJSON_AddBoolToObject(p,"close_on_disconnect",0); c->pending=1; rpc("session.resume",p,RpcResume,c); controls(); }
static void poll_stats(void) { if(app.connected&&!app.stats_pending) { app.stats_pending=1; net_rest(app.net,"/api/system/stats",RestStats,NULL); } }
static double number(const cJSON *o,const char *key) { const cJSON *v=cJSON_GetObjectItemCaseSensitive(o,key); return cJSON_IsNumber(v)?v->valuedouble:0; }
static void status_stats(void) {
    char buffer[2048]; const cJSON *s=app.stats,*memory=cJSON_GetObjectItemCaseSensitive(s,"memory"), *process=cJSON_GetObjectItemCaseSensitive(s,"process"),*disk=cJSON_GetObjectItemCaseSensitive(s,"disk");
    HWND view=app.serverwin?GetDlgItem(app.serverwin,507):NULL;
    if(!s || !app.connected) { snprintf(buffer,sizeof(buffer),"%s\r\n\r\n%s",app.connecting?"Connecting...":app.connected?"Connected":"Offline",app.connected?"Waiting for server statistics.":"Connect to Hermes to view live host statistics."); }
    else snprintf(buffer,sizeof(buffer),"HERMES  %s\r\n%s\r\n%s / %s\r\n\r\nHOST RESOURCES\r\nCPU    %.1f%% / %.0f CPUs\r\nRAM    %.1f / %.1f GiB\r\nFree   %.1f GiB\r\nDisk   %.1f / %.1f GiB\r\n\r\nHERMES PROCESS\r\nMemory   %.1f MiB\r\nThreads  %.0f\r\n\r\nSERVER DEFAULT MODEL\r\n%s\r\n\r\nUpdated %llu s ago\r\nFigures describe the server host.",js(s,"hermes_version"),js(s,"hostname"),js(s,"os"),js(s,"arch"),number(s,"cpu_percent"),number(s,"cpu_count"),number(memory,"used")/1073741824.0,number(memory,"total")/1073741824.0,number(memory,"available")/1073741824.0,number(disk,"used")/1073741824.0,number(disk,"total")/1073741824.0,number(process,"rss")/1048576.0,number(process,"num_threads"),*app.default_model?app.default_model:"Supplied by Hermes",(unsigned long long)((GetTickCount64()-app.stats_at)/1000));
    settext(view,buffer); settext(app.monitor,buffer);
}
static void activity_render(const char *text) {
    POINT scroll={0}; CHARRANGE selection={0}; SCROLLINFO info={sizeof(info),SIF_ALL,0,0,0,0,0}; int follow=app.activity_chat!=app.current;
    CHARFORMAT2W font={0}; PARAFORMAT2 paragraph={0}; wchar_t *value;
    if(app.activity_chat==app.current&&app.activity_text&&!strcmp(app.activity_text,text)) return;
    if(!app.activityview) return; value=wide(text); if(!value) return;
    SendMessageW(app.activityview,EM_GETSCROLLPOS,0,(LPARAM)&scroll); SendMessageW(app.activityview,EM_EXGETSEL,0,(LPARAM)&selection);
    if(!GetScrollInfo(app.activityview,SB_VERT,&info)||info.nPos+(int)info.nPage>=info.nMax-4) follow=1;
    if(app.activity_chat==app.current&&selection.cpMin!=selection.cpMax) follow=0;
    SendMessageW(app.activityview,WM_SETREDRAW,FALSE,0); SetWindowTextW(app.activityview,value); free(value);
    SendMessageW(app.activityview,EM_SETBKGNDCOLOR,0,app.fg); SendMessageW(app.activityview,EM_SETSEL,0,-1);
    font.cbSize=sizeof(font); font.dwMask=CFM_FACE|CFM_SIZE|CFM_COLOR; font.yHeight=180; font.crTextColor=app.ink; wcscpy(font.szFaceName,L"Tahoma"); SendMessageW(app.activityview,EM_SETCHARFORMAT,SCF_ALL,(LPARAM)&font);
    paragraph.cbSize=sizeof(paragraph); paragraph.dwMask=PFM_STARTINDENT|PFM_OFFSET|PFM_SPACEAFTER|PFM_LINESPACING; paragraph.dxOffset=180; paragraph.dySpaceAfter=100; paragraph.bLineSpacingRule=0; SendMessageW(app.activityview,EM_SETPARAFORMAT,0,(LPARAM)&paragraph);
    if(follow) { SendMessageW(app.activityview,EM_SETSEL,-1,-1); SendMessageW(app.activityview,EM_SCROLLCARET,0,0); }
    else { SendMessageW(app.activityview,EM_EXSETSEL,0,(LPARAM)&selection); SendMessageW(app.activityview,EM_SETSCROLLPOS,0,(LPARAM)&scroll); }
    SendMessageW(app.activityview,WM_SETREDRAW,TRUE,0); InvalidateRect(app.activityview,NULL,FALSE);
    free(app.activity_text); app.activity_text=textdup(text); app.activity_chat=app.current;
}
static void controls(void) {
    Chat *c=app.current; char line[1200],label[256],log[22000]; size_t used=0,i,working=0; const cJSON *entry; int ready=app.connected&&c&&!c->pending&&!app.uploads;
    EnableWindow(app.send,ready); EnableWindow(app.attach,ready&&c&&!c->running); settext(app.send,c&&c->running?"stop":"send");
    snprintf(line,sizeof(line),"%s  |  %s",app.connecting?"Connecting":app.connected?"Connected":"Offline",*app.notice?app.notice:c&&*c->activity?c->activity:"Ready");
    settext(app.status,line); settext(app.request,c&&cJSON_GetArraySize(c->requests)?"answer request":"export chat");
    if(c&&*c->model) copytext(label,sizeof(label),c->model); else if(*app.newmodel) copytext(label,sizeof(label),app.newmodel); else snprintf(label,sizeof(label),"Server default%s%s",*app.default_model?": ":"",app.default_model); settext(app.model,label);
    settext(app.title,c?c->title:"new chat");
    snprintf(line,sizeof(line),"Workspace: %s",c&&*c->cwd?c->cwd:"Server default folder"); settext(app.workspacelabel,line);
    for(i=0;i<app.count;i++) if(app.chats[i]->running) working++;
    snprintf(line,sizeof(line),"%zu chats  |  %zu working",app.count,working); settext(app.summary,line);
    if(c && c->running) snprintf(line,sizeof(line),"%s...",*c->activity?c->activity:"Working"); else snprintf(line,sizeof(line),"%s",c&&*c->activity?c->activity:"Ready");
    settext(app.activityhead,line); ShowWindow(app.progress,app.monitor_visible&&c&&c->running?SW_SHOW:SW_HIDE); SendMessageW(app.progress,PBM_SETMARQUEE,c&&c->running,80);
    log[0]=0; if(c) cJSON_ArrayForEach(entry,c->activity_log) if(cJSON_IsString(entry) && used+strlen(entry->valuestring)+7<sizeof(log)) { int n=snprintf(log+used,sizeof(log)-used,"\xe2\x80\xa2 %s\r\n",entry->valuestring); if(n>0) used+=(size_t)n; }
    if(!used) copytext(log,sizeof(log),"Tools, thinking and progress from this chat will appear here."); activity_render(log);
    EnableWindow(app.projectbutton,app.connected&&app.projects_supported); EnableWindow(app.workspace,app.connected&&app.projects_supported);
    if(app.serverwin) { snprintf(line,sizeof(line),"%s%s%s",app.connecting?"Connecting":app.connected?"Connected":"Offline",*app.notice?" / ":"",app.notice); settext(GetDlgItem(app.serverwin,508),line); EnableWindow(GetDlgItem(app.serverwin,505),!app.connecting); }
}
static void sidebar(void) {
    size_t i,j; char *filter=gettext(app.search); wchar_t *query=wide(filter); HTREEITEM selected=NULL,first=TreeView_GetFirstVisible(app.tree); free(filter);
    app.treebusy=1; SendMessageW(app.tree,WM_SETREDRAW,FALSE,0);
    /* Keep row handles, scroll position and collapsed folders across token/status updates. */
    for(i=0;i<app.nrows;) {
        int exists=0; for(j=0;j<app.count;j++) if(app.rows[i].chat==app.chats[j]) { exists=1; break; }
        if(!exists) { TreeView_DeleteItem(app.tree,app.rows[i].item); memmove(app.rows+i,app.rows+i+1,(app.nrows-i-1)*sizeof(*app.rows)); app.nrows--; } else i++;
    }
    for(i=0;i<app.count;i++) {
        Chat *c=app.chats[i]; TVINSERTSTRUCTW item={0}; wchar_t label[360],oldlabel[360], *title=wide(c->title); HTREEITEM h=NULL,parent=NULL; TreeRow *row=NULL; int newgroup=0;
        if(!title) continue;
        for(j=0;j<app.nrows;j++) if(app.rows[j].chat==c) { row=&app.rows[j]; break; }
        if(query&&*query&&!StrStrIW(title,query)) { if(row) { TreeView_DeleteItem(app.tree,row->item); memmove(row,row+1,(app.nrows-j-1)*sizeof(*row)); app.nrows--; } free(title); continue; }
        for(j=0;j<app.ngroups;j++) if(!strcmp(app.groups[j].path,c->cwd)) { parent=app.groups[j].item; break; }
        if(!parent && app.ngroups<1000) {
            char group[1400]; wchar_t *name;
            snprintf(group,sizeof(group),"%s%s%s",*c->project_name?c->project_name:*c->cwd?"Folder":"Unfiled chats",*c->cwd?"  /  ":"",c->cwd); name=wide(group);
            item.hParent=TVI_ROOT; item.hInsertAfter=TVI_LAST; item.item.mask=TVIF_TEXT|TVIF_IMAGE|TVIF_SELECTEDIMAGE; item.item.pszText=name; item.item.iImage=item.item.iSelectedImage=ClassicFolder;
            parent=TreeView_InsertItem(app.tree,&item); free(name); copytext(app.groups[app.ngroups].path,1024,c->cwd); app.groups[app.ngroups++].item=parent; newgroup=1;
        }
        if(parent && *c->project_name && !newgroup) {
            char group[1400]; wchar_t *label; TVITEMW current={0}; wchar_t old[1400]; snprintf(group,sizeof(group),"%s  /  %s",c->project_name,c->cwd); label=wide(group);
            current.hItem=parent; current.mask=TVIF_TEXT; current.pszText=old; current.cchTextMax=1400; TreeView_GetItem(app.tree,&current); if(label&&wcscmp(old,label)) { current.pszText=label; TreeView_SetItem(app.tree,&current); } free(label);
        }
        if(row && row->parent!=parent) { TreeView_DeleteItem(app.tree,row->item); memmove(row,row+1,(app.nrows-(size_t)(row-app.rows)-1)*sizeof(*row)); app.nrows--; row=NULL; }
        swprintf(label,360,L"%ls%ls%ls",c->unread?L"* ":L"",title,c->running?L"  ...":L""); free(title);
        item.hParent=parent; item.hInsertAfter=TVI_LAST; item.item.mask=TVIF_TEXT|TVIF_PARAM|TVIF_IMAGE|TVIF_SELECTEDIMAGE; item.item.pszText=label; item.item.lParam=(LPARAM)c; item.item.iImage=item.item.iSelectedImage=c->running?ClassicRun:ClassicDocument;
        if(row) {
            TVITEMW old={0}; old.hItem=row->item; old.mask=TVIF_TEXT|TVIF_IMAGE; old.pszText=oldlabel; old.cchTextMax=360; TreeView_GetItem(app.tree,&old); h=row->item;
            if(wcscmp(oldlabel,label)||old.iImage!=item.item.iImage) { item.item.hItem=h; TreeView_SetItem(app.tree,&item.item); }
        } else if(app.nrows<1000) { h=TreeView_InsertItem(app.tree,&item); app.rows[app.nrows++]=(TreeRow){c,h,parent}; }
        if(newgroup) TreeView_Expand(app.tree,parent,TVE_EXPAND);
        if(c==app.current) selected=h;
    }
    for(i=0;i<app.ngroups;) {
        if(!TreeView_GetChild(app.tree,app.groups[i].item)) { TreeView_DeleteItem(app.tree,app.groups[i].item); memmove(app.groups+i,app.groups+i+1,(app.ngroups-i-1)*sizeof(*app.groups)); app.ngroups--; } else i++;
    }
    if(selected && selected!=TreeView_GetSelection(app.tree) && (TreeView_GetItemState(app.tree,TreeView_GetParent(app.tree,selected),TVIS_EXPANDED)&TVIS_EXPANDED)) TreeView_SelectItem(app.tree,selected);
    if(first) { TVITEMW check={0}; check.hItem=first; check.mask=TVIF_PARAM; if(TreeView_GetItem(app.tree,&check)) TreeView_SelectSetFirstVisible(app.tree,first); }
    SendMessageW(app.tree,WM_SETREDRAW,TRUE,0); InvalidateRect(app.tree,NULL,FALSE); app.treebusy=0; free(query);
}
static void setformat(unsigned style) {
    CHARFORMAT2W f={0}; f.cbSize=sizeof(f); f.dwMask=CFM_COLOR|CFM_FACE|CFM_BOLD|CFM_ITALIC|CFM_SIZE|CFM_BACKCOLOR; f.crTextColor=(style&MdQuote)?app.muted:app.ink; f.crBackColor=app.fg; f.yHeight=app.speaker?180:(style&MdHeading)?240:200;
    if(style&(MdBold|MdHeading)) f.dwEffects|=CFE_BOLD; if(style&MdItalic) f.dwEffects|=CFE_ITALIC;
    wcscpy(f.szFaceName,style&MdCode?L"Consolas":L"Tahoma"); SendMessageW(app.transcript,EM_SETCHARFORMAT,SCF_SELECTION,(LPARAM)&f);
}
static void emit_markdown(void *ctx,const wchar_t *text,unsigned style) {
    const wchar_t *p=text; (void)ctx;
    while(*p) {
        const wchar_t *end=wcschr(p,L'\n'); size_t n=end?(size_t)(end-p)+1:wcslen(p); wchar_t *part=calloc(n+1,sizeof(*part));
        if(!part) return; memcpy(part,p,n*sizeof(*part));
        if(app.paragraph_start) { PARAFORMAT2 f={0}; f.cbSize=sizeof(f); f.dwMask=PFM_STARTINDENT|PFM_OFFSET|PFM_SPACEBEFORE|PFM_SPACEAFTER|PFM_LINESPACING; f.dySpaceBefore=app.speaker?120:0; f.dySpaceAfter=app.speaker?45:35; f.bLineSpacingRule=0; SendMessageW(app.transcript,EM_SETPARAFORMAT,0,(LPARAM)&f); }
        setformat(style); SendMessageW(app.transcript,EM_REPLACESEL,FALSE,(LPARAM)part); app.paragraph_start=end!=NULL; free(part); p+=n;
    }
}
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
    if(msg==WM_PAINT||msg==WM_PRINTCLIENT) { HDC dc=msg==WM_PAINT?GetDC(w):(HDC)wp; RECT client; int saved=SaveDC(dc); HPEN pen=CreatePen(PS_SOLID,1,RGB(211,214,195)); HGDIOBJ old=SelectObject(dc,pen); size_t row; GetClientRect(w,&client); IntersectClipRect(dc,0,0,client.right,client.bottom);
        for(row=0;row<app.view_count;row++) if(app.view_rows[row].visible) { POINTL pos={0}; SendMessageW(w,EM_POSFROMCHAR,(WPARAM)&pos,app.view_rows[row].anchor);
            if(pos.y>=0&&pos.y<client.bottom&&pos.x+px(64)<client.right-px(12)) { MoveToEx(dc,pos.x+px(64),pos.y+px(16),NULL); LineTo(dc,client.right-px(12),pos.y+px(16)); }
        }
        SelectObject(dc,old); DeleteObject(pen);
        for(i=0;i<app.npreviews;i++) { Preview *p=&app.previews[i]; POINTL pos={0}; SendMessageW(w,EM_POSFROMCHAR,(WPARAM)&pos,p->anchor);
            if(pos.y>=-px(150)&&pos.y<client.bottom) { HDC source=CreateCompatibleDC(dc); HGDIOBJ old=SelectObject(source,p->bitmap); SetStretchBltMode(dc,HALFTONE); StretchBlt(dc,pos.x,pos.y,px(p->width),px(p->height),source,0,0,p->width,p->height,SRCCOPY); SelectObject(source,old); DeleteDC(source); }
        } RestoreDC(dc,saved); if(msg==WM_PAINT) ReleaseDC(w,dc);
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
static unsigned long long json_hash(const cJSON *value) {
    char *text=cJSON_PrintUnformatted(value); const unsigned char *p=(const unsigned char *)text; unsigned long long hash=14695981039346656037ULL;
    if(p) while(*p) { hash^=*p++; hash*=1099511628211ULL; } free(text); return hash;
}
static LONG transcript_end(void) { CHARRANGE range={0}; SendMessageW(app.transcript,EM_SETSEL,-1,-1); SendMessageW(app.transcript,EM_EXGETSEL,0,(LPARAM)&range); return range.cpMax; }
static void render(void) {
    const cJSON *row; Chat *c=app.current; POINT scroll={0}; CHARRANGE selected={0}; BOOL atbottom=TRUE;
    SCROLLINFO info={sizeof(info),SIF_ALL,0,0,0,0,0}; wchar_t *draft; size_t n=c?(size_t)cJSON_GetArraySize(c->messages):0,i=0,first=0;
    unsigned long long attachments=json_hash(c?c->attachments:NULL),*hashes=n?calloc(n,sizeof(*hashes)):NULL;
    int full=!app.view_valid||app.view_chat!=c||!app.view_count||!n; LONG anchor=0;
    KillTimer(app.window,IdRender); app.render_pending=0; if(n&&!hashes) return;
    cJSON_ArrayForEach(row,(c?c->messages:NULL)) hashes[i++]=json_hash(row);
    if(!full) { while(first<n&&first<app.view_count&&app.view_rows[first].hash==hashes[first]) first++;
        if(first==n&&first==app.view_count&&attachments==app.view_attachments) goto unchanged;
        anchor=first<app.view_count?app.view_rows[first].anchor:app.view_tail;
    }
    if(n>app.view_capacity) { RenderRow *rows=realloc(app.view_rows,n*sizeof(*rows)); if(!rows) { free(hashes); return; } app.view_rows=rows; app.view_capacity=n; }
    SendMessageW(app.transcript,EM_GETSCROLLPOS,0,(LPARAM)&scroll); SendMessageW(app.transcript,EM_EXGETSEL,0,(LPARAM)&selected);
    if(app.view_chat==c&&GetScrollInfo(app.transcript,SB_VERT,&info)) atbottom=info.nPos+(int)info.nPage>=info.nMax-4;
    if(app.view_chat==c&&selected.cpMin!=selected.cpMax) atbottom=FALSE;
    SendMessageW(app.transcript,WM_SETREDRAW,FALSE,0);
    if(full) { previews_clear(); SetWindowTextW(app.transcript,L""); first=0; }
    else { int j; CHARRANGE tail={anchor,-1}; SendMessageW(app.transcript,EM_EXSETSEL,0,(LPARAM)&tail); SendMessageW(app.transcript,EM_REPLACESEL,FALSE,(LPARAM)L"");
        for(j=0;j<app.npreviews;) if(app.previews[j].anchor>=anchor) { DeleteObject(app.previews[j].bitmap); memmove(app.previews+j,app.previews+j+1,(size_t)(app.npreviews-j-1)*sizeof(*app.previews)); app.npreviews--; } else j++;
    }
    SendMessageW(app.transcript,EM_SETBKGNDCOLOR,0,app.fg); transcript_end(); app.paragraph_start=1; app.speaker=0;
    i=0; cJSON_ArrayForEach(row,(c?c->messages:NULL)) {
        char *text; wchar_t *w; const char *role; const cJSON *media; size_t length;
        if(i<first) { i++; continue; }
        app.view_rows[i]=(RenderRow){hashes[i],transcript_end(),0}; i++;
        role=js(row,"role"); if(strcmp(role,"user")&&strcmp(role,"assistant")&&strcmp(role,"system")) continue;
        text=row_text(row); if(!text) continue; length=strlen(text);
        while(length&&(text[length-1]=='\r'||text[length-1]=='\n'||text[length-1]==' '||text[length-1]=='\t')) text[--length]=0;
        media=cJSON_GetObjectItemCaseSensitive(row,"attachments");
        if(!length&&!cJSON_GetArraySize(media)) { free(text); continue; }
        app.view_rows[i-1].visible=1;
        app.speaker=1; append(!strcmp(role,"user")?L"You\r\n":!strcmp(role,"assistant")?L"Hermes\r\n":L"System\r\n",MdBold|MdQuote); app.speaker=0;
        show_attachments(media); w=wide(text); if(w) markdown_render(w,emit_markdown,NULL); if(!app.paragraph_start) append(L"\r\n",0); free(text); free(w);
    }
    if(!GetWindowTextLengthW(app.transcript)) { append(L"lcb-hermes\r\n",MdHeading); append(app.connected?L"A fresh conversation.\r\n":L"Connect to your Hermes server to begin.\r\n",MdQuote); }
    app.view_tail=transcript_end();
    if(c&&cJSON_GetArraySize(c->attachments)) { append(L"Attached for your next message\r\n",MdQuote); show_attachments(c->attachments); }
    if(atbottom) { SendMessageW(app.transcript,EM_SETSEL,-1,-1); SendMessageW(app.transcript,EM_SCROLLCARET,0,0); }
    else { SendMessageW(app.transcript,EM_EXSETSEL,0,(LPARAM)&selected); SendMessageW(app.transcript,EM_SETSCROLLPOS,0,(LPARAM)&scroll); }
    SendMessageW(app.transcript,WM_SETREDRAW,TRUE,0); InvalidateRect(app.transcript,NULL,FALSE);
    app.view_chat=c; app.view_count=n; app.view_attachments=attachments; app.view_valid=1;
unchanged:
    free(hashes); app.restoring=1; draft=wide(c?c->draft:"");
    if(draft) { char *old=gettext(app.prompt); if(strcmp(old,c?c->draft:"")) SetWindowTextW(app.prompt,draft); free(old); free(draft); } app.restoring=0; controls();
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
    else if(!*c->id) { p=params(); cJSON_AddStringToObject(p,"source","desktop"); cJSON_AddBoolToObject(p,"close_on_disconnect",0); if(*c->cwd) cJSON_AddStringToObject(p,"cwd",c->cwd); if(*app.newmodel) { cJSON_AddStringToObject(p,"model",app.newmodel); cJSON_AddStringToObject(p,"provider",app.newprovider); } rpc("session.create",p,RpcCreate,c); }
    else start_send(c); controls();
}
static void incoming_event(cJSON *e) {
    Chat *c=findchat(js(e,"session_id")); const char *type=js(e,"type"); const cJSON *p=cJSON_GetObjectItemCaseSensitive(e,"payload");
    if(!strcmp(type,"gateway.ready")) { const char *epoch=js(p,"replay_epoch"); if(strcmp(app.epoch,epoch)) { size_t i; copytext(app.epoch,sizeof(app.epoch),epoch); for(i=0;i<app.count;i++) app.chats[i]->sequence=-1; } return; }
    if(!strcmp(type,"sessions.changed")) { SetTimer(app.window,IdList,250,NULL); return; }
    if(!c) return;
    if(chat_event(c,e)) {
        int content=!strcmp(type,"message.delta")||!strcmp(type,"message.complete")||!strcmp(type,"message.interim");
        if(c==app.current && content && !app.render_pending) { app.render_pending=1; SetTimer(app.window,IdRender,40,NULL); } else if(c!=app.current) c->unread=1;
        if(!strcmp(type,"session.info")||!strcmp(type,"session.title")||!strcmp(type,"message.start")) { sidebar(); schedule_save(); }
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
    if(wait->kind==RpcList) { app.list_pending=0; if(app.list_again) { app.list_again=0; SetTimer(app.window,IdList,250,NULL); } }
    if(wait->kind==RpcProjectCreate||wait->kind==RpcProjectAdd) { app.project_pending=0; if(app.projectwin) { EnableWindow(GetDlgItem(app.projectwin,605),TRUE); EnableWindow(GetDlgItem(app.projectwin,606),chosen_project(app.projectwin)!=NULL); } }
    if(*error) { if(c) c->pending=0; if(wait->kind==RpcSubmit) { if(c) { c->running=0; resume(c); } notice("Send result was uncertain. The message was not retried; reconnect to check the chat."); }
        else if(wait->kind==RpcProjects) { app.projects_supported=0; project_choices(); }
        else if(wait->kind!=RpcDefaultModel&&wait->kind!=RpcProjectTree) notice(error); if(wait->kind==RpcModels) app.models_pending=0;
        if(app.projectwin && (wait->kind==RpcProjectCreate||wait->kind==RpcProjectAdd)) settext(GetDlgItem(app.projectwin,608),error);
        if(c&&wait->kind==RpcAttach) { cJSON *a; cJSON_ArrayForEach(a,c->attachments) if(jb(a,"queued")) { cJSON *p=sessionparams(c); cJSON_AddStringToObject(p,"path",js(a,"path")); rpc("image.detach",p,0,NULL); cJSON_DeleteItemFromObjectCaseSensitive(a,"queued"); } }
        controls(); return; }
    switch(wait->kind) {
    case RpcList:
        array=cJSON_GetObjectItemCaseSensitive(r,"sessions"); cJSON_ArrayForEach(v,array) { const char *id=js(v,"session_id"); Chat *found; if(!*id) id=js(v,"id"); if(!*id) continue; found=findchat(id);
            if(!found && app.count<1000) { char key[65]; freshkey(key); found=chat_new(key); if(found) { copytext(found->stored,sizeof(found->stored),id); if(!addchat(found)) found=NULL; } }
            if(found && *js(v,"title")) copytext(found->title,sizeof(found->title),js(v,"title"));
        } tree_metadata(); sidebar(); break;
    case RpcResume: if(c) { chat_load(c,r); if(c==app.current) { c->unread=0; render(); } sidebar(); schedule_save(); } break;
    case RpcCreate: case RpcResumeSend: if(c) { char requested[1024]; copytext(requested,sizeof(requested),c->cwd); chat_load(c,r);
        if(wait->kind==RpcCreate && *requested && strcmp(requested,c->cwd)) { notice("Hermes opened a different folder. Review the workspace before sending."); sidebar(); render(); } else start_send(c); } break;
    case RpcSubmit: if(c) { c->pending=0; free(c->draft); c->draft=textdup(""); if(c==app.current) { app.restoring=1; SetWindowTextW(app.prompt,L""); app.restoring=0; } schedule_save(); } break;
    case RpcAttach:
        if(c) { const cJSON *a; cJSON_ArrayForEach(a,c->attachments) if(!jb(a,"queued") && !strncmp(js(a,"mime"),"image/",6)) { cJSON_AddBoolToObject((cJSON *)a,"queued",1); break; } start_send(c); } break;
    case RpcModels: cJSON_Delete(app.models); app.models=cJSON_Duplicate(r,1); app.models_pending=0; PostMessageW(app.window,WM_COMMAND,IdModel,0); break;
    case RpcProjects: cJSON_Delete(app.projects); app.projects=cJSON_Duplicate(r,1); app.projects_supported=1; project_choices(); break;
    case RpcProjectTree: cJSON_Delete(app.project_tree); app.project_tree=cJSON_Duplicate(r,1); tree_metadata(); project_choices(); sidebar(); schedule_save(); break;
    case RpcProjectCreate: case RpcProjectAdd:
        if(wait->kind==RpcProjectCreate) { const cJSON *project=cJSON_GetObjectItemCaseSensitive(r,"project"); choose_workspace(js(project,"primary_path")); }
        rpc("projects.list",params(),RpcProjects,NULL); project_tree(); notice("Workspace saved on the Hermes server."); if(app.projectwin) settext(GetDlgItem(app.projectwin,608),"Workspace saved on the server."); schedule_save(); break;
    case RpcDefaultModel: copytext(app.default_model,sizeof(app.default_model),*js(r,"model")?js(r,"model"):js(r,"value")); status_stats(); break;
    case RpcRename: if(c) { copytext(c->title,sizeof(c->title),js(r,"title")); listchats(); schedule_save(); } break;
    case RpcCloseDelete: if(c) { cJSON *p=params(); c->id[0]=0; cJSON_AddStringToObject(p,"session_id",c->stored); rpc("session.delete",p,RpcDelete,c); } break;
    case RpcDelete: if(c) { size_t i; Pending **link=&app.pending; while(*link) { Pending *p=*link; if(p->chat==c) { *link=p->next; free(p); } else link=&p->next; } for(i=0;i<app.count;i++) if(app.chats[i]==c) { memmove(app.chats+i,app.chats+i+1,(app.count-i-1)*sizeof(*app.chats)); app.count--; break; } if(app.current==c) app.current=app.count?app.chats[0]:NULL; if(app.view_chat==c) app.view_valid=0; if(app.activity_chat==c) app.activity_chat=NULL; chat_free(c); if(!app.current) newchat(); else { if(*app.current->stored) resume(app.current); render(); sidebar(); } save(); } break;
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
        rpc("projects.list",params(),RpcProjects,NULL); project_tree(); { cJSON *p=params(); cJSON_AddStringToObject(p,"key","provider"); rpc("config.get",p,RpcDefaultModel,NULL); }
        save(); sidebar(); render(); poll_stats();
    } else if(m->kind==NetOffline) {
        Pending *p; size_t i; app.connected=app.connecting=0; app.models_pending=app.list_pending=app.list_again=app.project_pending=0;
        while((p=app.pending)!=NULL) { app.pending=p->next; if(p->chat) p->chat->pending=0; free(p); }
        for(i=0;i<app.count;i++) app.chats[i]->pending=0;
        notice(m->error); status_stats();
    } else if(m->kind==NetFrame) { if(app.connected) frame(m->json); }
    else if(m->kind==NetRest) {
        if(m->tag==RestStats) { app.stats_pending=0; if(app.connected&&!strcmp(m->url,app.url)) { cJSON_Delete(app.stats); app.stats=m->json?cJSON_Duplicate(m->json,1):NULL; app.stats_at=GetTickCount64(); status_stats(); } }
        else if(m->tag==RestUpload) { Upload *u=m->context; app.uploads--; if(u) {
            if(m->json && *js(m->json,"path")) { cJSON *a=cJSON_CreateObject(); char *local=utf8(u->path),*name=utf8(wcsrchr(u->path,L'\\')?wcsrchr(u->path,L'\\')+1:u->path); const wchar_t *ext=wcsrchr(u->path,L'.');
                cJSON_AddStringToObject(a,"path",js(m->json,"path")); cJSON_AddStringToObject(a,"local",local); cJSON_AddStringToObject(a,"name",name); cJSON_AddStringToObject(a,"mime",ext&&(!_wcsicmp(ext,L".png")||!_wcsicmp(ext,L".jpg")||!_wcsicmp(ext,L".jpeg")||!_wcsicmp(ext,L".webp")||!_wcsicmp(ext,L".gif"))?"image/local":"application/octet-stream");
                cJSON_AddItemToArray(u->chat->attachments,a); free(local); free(name); if(u->chat==app.current) render(); schedule_save();
            } else notice(*m->error?m->error:"Attachment upload failed."); free(u);
        } controls(); }
    }
    cJSON_Delete(m->json); SecureZeroMemory(m->cookie,sizeof(m->cookie)); free(m);
}
static LRESULT CALLBACK button_proc(HWND w,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR ref) {
    (void)ref;
    if(msg==WM_MOUSEMOVE && app.hotbutton!=w) { HWND old=app.hotbutton; TRACKMOUSEEVENT track={sizeof(track),TME_LEAVE,w,0}; app.hotbutton=w; TrackMouseEvent(&track); if(old) InvalidateRect(old,NULL,FALSE); InvalidateRect(w,NULL,FALSE); }
    if(msg==WM_MOUSELEAVE && app.hotbutton==w) { app.hotbutton=NULL; InvalidateRect(w,NULL,FALSE); }
    if(msg==WM_NCDESTROY) { if(app.hotbutton==w) app.hotbutton=NULL; RemoveWindowSubclass(w,button_proc,id); }
    return DefSubclassProc(w,msg,wp,lp);
}
static void draw_button(DRAWITEMSTRUCT *d) {
    wchar_t text[256]; RECT r=d->rcItem,t=r; int icon=-1,pressed=(d->itemState&ODS_SELECTED)!=0,disabled=(d->itemState&ODS_DISABLED)!=0;
    COLORREF background=disabled?app.bg:pressed?app.accent:app.hotbutton==d->hwndItem?app.fg:d->CtlID==IdSend?app.accent:app.bg;
    HGDIOBJ old=SelectObject(d->hDC,app.normal); SetDCBrushColor(d->hDC,background); FillRect(d->hDC,&r,(HBRUSH)GetStockObject(DC_BRUSH)); classic_edge(d->hDC,r,!pressed); GetWindowTextW(d->hwndItem,text,256);
    switch(d->CtlID) {
    case IdSend: icon=app.current&&app.current->running?ClassicStop:ClassicRun; break;
    case IdAttach: icon=ClassicAttach; break;
    case IdNew: case IdRequest: icon=ClassicDocument; break;
    case IdProjects: icon=ClassicFolder; break;
    case IdServer: icon=ClassicOutput; break;
    case IdModel: icon=ClassicModel; break;
    }
    if(icon>=0&&app.icons) { int x=*text?r.left+px(5):r.left+(r.right-r.left-px(16))/2;
        ImageList_DrawEx(app.icons,icon,d->hDC,x+pressed,r.top+(r.bottom-r.top-px(16))/2+pressed,0,0,CLR_NONE,background,disabled?ILD_BLEND50:ILD_TRANSPARENT);
        if(*text) { t.left+=px(23); t.right-=px(4); }
    }
    if(pressed) OffsetRect(&t,1,1); SetBkMode(d->hDC,TRANSPARENT); SetTextColor(d->hDC,disabled?app.muted:app.ink);
    DrawTextW(d->hDC,text,-1,&t,DT_SINGLELINE|DT_VCENTER|DT_CENTER|DT_END_ELLIPSIS|((d->itemState&ODS_NOACCEL)?DT_HIDEPREFIX:0));
    if((d->itemState&ODS_FOCUS)&&!(d->itemState&ODS_NOFOCUSRECT)) { InflateRect(&t,-px(4),-px(4)); DrawFocusRect(d->hDC,&t); } SelectObject(d->hDC,old);
}
static HWND child(HWND owner,const wchar_t *class,const wchar_t *text,DWORD style,int id) {
    HWND w=CreateWindowExW((!wcscmp(class,L"EDIT")||!wcscmp(class,WC_TREEVIEWW)||!wcscmp(class,MSFTEDIT_CLASS)||!wcscmp(class,L"LCBHermesMonitor"))?WS_EX_CLIENTEDGE:0,class,text,WS_CHILD|WS_VISIBLE|style,0,0,0,0,owner,(HMENU)(INT_PTR)id,app.instance,NULL);
    SetWindowTheme(w,L"",L""); SendMessageW(w,WM_SETFONT,(WPARAM)app.normal,TRUE);
    if(!wcscmp(class,L"BUTTON")&&(style&BS_TYPEMASK)==BS_OWNERDRAW) SetWindowSubclass(w,button_proc,1,0); return w;
}
static void move(HWND w,int x,int y,int width,int height) {
    if(app.positions&&GetParent(w)==app.window) app.positions=DeferWindowPos(app.positions,w,NULL,px(x),px(y),px(width),px(height),SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOCOPYBITS);
    else SetWindowPos(w,NULL,px(x),px(y),px(width),px(height),SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOCOPYBITS);
}
static void visible(HWND w,int show) { if(((GetWindowLongPtrW(w,GWL_STYLE)&WS_VISIBLE)!=0)!=show) ShowWindow(w,show?SW_SHOW:SW_HIDE); }
static void inset_text(HWND w,int margin) { RECT r; GetClientRect(w,&r); InflateRect(&r,-px(margin),-px(margin)); if(r.right>r.left&&r.bottom>r.top) SendMessageW(w,EM_SETRECT,0,(LPARAM)&r); }
static void layout(void) {
    int w=MulDiv(app.width,96,app.dpi),h=MulDiv(app.height,96,app.dpi),left=app.sidebar_width,end=w-10,statsheight; TOOLINFOW tip={0};
    if(left<180) left=180; if(left>w-370) left=w-370;
    app.monitor_visible=app.show_monitor&&w-left>=660&&h>=560; if(app.monitor_visible) end=w-274;
    if(left>end-360) left=end-360; if(left<160) left=160; app.split_x=left; app.monitor_x=end+12;
    app.positions=BeginDeferWindowPos(24);
    move(app.fresh,10,10,104,28); move(app.projectbutton,122,10,122,28); move(app.server,w-44,10,34,28); move(app.model,w-328,10,274,28);
    move(app.explorerhead,10,53,left-20,23); move(app.search,10,83,left-20,25); move(app.tree,10,116,left-20,h-245);
    move(app.workspace,10,h-94,left-20,200); move(app.summary,10,h-59,left-20,24);
    move(app.title,left+12,53,end-left-24,24); move(app.workspacelabel,left+12,82,end-left-24,22); move(app.transcript,left+12,110,end-left-24,h-294);
    move(app.prompt,left+12,h-151,end-left-120,88); move(app.send,end-96,h-151,84,28); move(app.attach,end-96,h-115,84,26);
    move(app.request,left+12,h-54,142,25); move(app.status,10,h-25,w-20,20);
    statsheight=(h-190)/2; if(statsheight>342) statsheight=342; if(statsheight<240) statsheight=240;
    move(app.monitorhead,end+12,53,250,23); move(app.monitor,end+12,83,250,statsheight);
    move(app.activitytitle,end+12,statsheight+98,250,23); move(app.activityhead,end+16,statsheight+128,242,20);
    move(app.progress,end+16,statsheight+152,242,8); move(app.activityview,end+12,statsheight+168,250,h-statsheight-203);
    if(app.positions) EndDeferWindowPos(app.positions); app.positions=NULL;
    visible(app.monitorhead,app.monitor_visible); visible(app.monitor,app.monitor_visible); visible(app.activitytitle,app.monitor_visible); visible(app.activityhead,app.monitor_visible); visible(app.activityview,app.monitor_visible); visible(app.progress,app.monitor_visible&&app.current&&app.current->running);
    inset_text(app.transcript,12); inset_text(app.activityview,8);
    tip.cbSize=sizeof(tip); tip.hwnd=app.window; tip.uId=1; tip.rect=(RECT){px(left-6),px(50),px(left+6),px(h-30)}; SendMessageW(app.tooltip,TTM_NEWTOOLRECTW,0,(LPARAM)&tip);
    RedrawWindow(app.window,NULL,NULL,RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN);
}
static void theme(void) {
    BOOL dark=FALSE; app.view_valid=0; app.bg=RGB(222,218,199); app.fg=RGB(255,253,238); app.ink=RGB(43,48,42); app.muted=RGB(94,99,80); app.accent=RGB(192,203,179);
    if(app.face) DeleteObject(app.face); if(app.paper) DeleteObject(app.paper); app.face=CreateSolidBrush(app.bg); app.paper=CreateSolidBrush(app.fg);
    TreeView_SetBkColor(app.tree,app.fg); TreeView_SetTextColor(app.tree,app.ink); DwmSetWindowAttribute(app.window,20,&dark,sizeof(dark));
    if(app.serverwin) DwmSetWindowAttribute(app.serverwin,20,&dark,sizeof(dark)); render(); InvalidateRect(app.window,NULL,TRUE); if(app.serverwin) InvalidateRect(app.serverwin,NULL,TRUE);
}
static LRESULT color_control(UINT msg,WPARAM wp,LPARAM lp) {
    HDC dc=(HDC)wp; HWND w=(HWND)lp; COLORREF background=(msg==WM_CTLCOLOREDIT||w==app.transcript||w==app.monitor||w==app.activityview||msg==WM_CTLCOLORLISTBOX)?app.fg:app.bg;
    if(w==app.explorerhead||w==app.title||w==app.monitorhead||w==app.activitytitle) background=app.accent;
    SetTextColor(dc,app.ink); SetBkColor(dc,background); SetDCBrushColor(dc,background); return (LRESULT)GetStockObject(DC_BRUSH);
}
static HBITMAP resource_bitmap(int id,int *width,int *height) {
    HRSRC resource=FindResourceW(app.instance,MAKEINTRESOURCEW(id),RT_RCDATA); HGLOBAL data;
    IWICImagingFactory *factory=NULL; IWICStream *stream=NULL; IWICBitmapDecoder *decoder=NULL; IWICBitmapFrameDecode *frame=NULL; IWICFormatConverter *converter=NULL;
    HBITMAP bitmap=NULL; BITMAPINFO info={0}; UINT w=0,h=0; void *pixels; HDC dc; DWORD size;
    if(!resource || !(size=SizeofResource(app.instance,resource)) || !(data=LoadResource(app.instance,resource))) return NULL;
    if(FAILED(CoCreateInstance(&CLSID_WICImagingFactory,NULL,CLSCTX_INPROC_SERVER,&IID_IWICImagingFactory,(void **)&factory))) return NULL;
    if(FAILED(IWICImagingFactory_CreateStream(factory,&stream))||FAILED(IWICStream_InitializeFromMemory(stream,LockResource(data),size))||FAILED(IWICImagingFactory_CreateDecoderFromStream(factory,(IStream *)stream,NULL,WICDecodeMetadataCacheOnLoad,&decoder))||FAILED(IWICBitmapDecoder_GetFrame(decoder,0,&frame))||FAILED(IWICBitmapFrameDecode_GetSize(frame,&w,&h))||!w||!h||w>4096||h>4096) goto done;
    if(FAILED(IWICImagingFactory_CreateFormatConverter(factory,&converter))||FAILED(IWICFormatConverter_Initialize(converter,(IWICBitmapSource *)frame,&GUID_WICPixelFormat32bppBGR,WICBitmapDitherTypeNone,NULL,0,WICBitmapPaletteTypeCustom))) goto done;
    info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER); info.bmiHeader.biWidth=(LONG)w; info.bmiHeader.biHeight=-(LONG)h; info.bmiHeader.biPlanes=1; info.bmiHeader.biBitCount=32; info.bmiHeader.biCompression=BI_RGB;
    dc=GetDC(NULL); bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,NULL,0); ReleaseDC(NULL,dc);
    if(bitmap&&FAILED(IWICFormatConverter_CopyPixels(converter,NULL,w*4,w*h*4,pixels))) { DeleteObject(bitmap); bitmap=NULL; }
    if(bitmap) { *width=(int)w; *height=(int)h; }
done:
    if(converter) IWICFormatConverter_Release(converter); if(frame) IWICBitmapFrameDecode_Release(frame); if(decoder) IWICBitmapDecoder_Release(decoder); if(stream) IWICStream_Release(stream); IWICImagingFactory_Release(factory); return bitmap;
}
static void about_text(HDC dc,int x,int y,int width,int height,const wchar_t *text,HFONT font,COLORREF ink) {
    RECT r={px(x),px(y),px(x+width),px(y+height)}; HGDIOBJ old=SelectObject(dc,font); SetBkMode(dc,TRANSPARENT); SetTextColor(dc,ink); DrawTextW(dc,text,-1,&r,DT_LEFT|DT_WORDBREAK|DT_NOPREFIX); SelectObject(dc,old);
}
static void panel_text(HDC dc,int x,int y,int width,const char *text,HFONT font,COLORREF ink) {
    wchar_t *value=wide(text); RECT r={px(x),px(y),px(x+width),px(y+18)}; HGDIOBJ old=SelectObject(dc,font);
    SetBkMode(dc,TRANSPARENT); SetTextColor(dc,ink); if(value) DrawTextW(dc,value,-1,&r,DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX); free(value); SelectObject(dc,old);
}
static void resource_meter(HDC dc,int y,int width,int icon,const char *label,double used,double total,const char *detail,int compact) {
    char percent[40]; RECT r={px(30),px(y+21),px(width-10),px(y+29)},inside; double ratio=total>0?used/total:0;
    if(ratio<0) ratio=0; if(ratio>1) ratio=1;
    if(app.icons) ImageList_Draw(app.icons,icon,dc,px(9),px(y),ILD_TRANSPARENT);
    panel_text(dc,30,y,width-102,label,app.heading,app.ink);
    if(total>0) snprintf(percent,sizeof(percent),"%.1f%%",used/total*100.0); else copytext(percent,sizeof(percent),"--"); panel_text(dc,width-65,y,55,percent,app.normal,app.muted);
    SetDCBrushColor(dc,app.bg); FillRect(dc,&r,(HBRUSH)GetStockObject(DC_BRUSH)); classic_edge(dc,r,0); inside=r; InflateRect(&inside,-2,-2); inside.right=inside.left+(LONG)((inside.right-inside.left)*ratio);
    SetDCBrushColor(dc,ratio>.9?RGB(174,148,101):RGB(137,160,113)); if(inside.right>inside.left) FillRect(dc,&inside,(HBRUSH)GetStockObject(DC_BRUSH));
    if(!compact) panel_text(dc,30,y+32,width-40,detail,app.normal,app.muted);
}
static void draw_monitor(HWND w,HDC dc) {
    RECT r; int width,height,compact,step,start,model_y; char text[512],detail[128];
    const cJSON *s=app.stats,*memory=cJSON_GetObjectItemCaseSensitive(s,"memory"),*disk=cJSON_GetObjectItemCaseSensitive(s,"disk"),*process=cJSON_GetObjectItemCaseSensitive(s,"process");
    GetClientRect(w,&r); FillRect(dc,&r,app.paper); width=MulDiv(r.right,96,app.dpi); height=MulDiv(r.bottom,96,app.dpi); compact=height<285; step=compact?43:55; start=compact?57:70;
    if(app.icons) ImageList_Draw(app.icons,ClassicOutput,dc,px(9),px(10),ILD_TRANSPARENT);
    snprintf(text,sizeof(text),"Hermes%s%s",app.connected&&s?" ":"",app.connected&&s?js(s,"hermes_version"):""); panel_text(dc,32,10,width-42,text,app.heading,app.ink);
    if(!app.connected||!s) { panel_text(dc,10,37,width-20,app.connecting?"Connecting...":app.connected?"Waiting for host statistics":"Offline",app.normal,app.muted);
        about_text(dc,10,75,width-20,70,L"Connect to Hermes to see server resources and the configured model.",app.normal,app.muted); return;
    }
    panel_text(dc,10,32,width-20,js(s,"hostname"),app.normal,app.muted);
    if(!compact) { snprintf(text,sizeof(text),"%s / %s",js(s,"os"),js(s,"arch")); panel_text(dc,10,49,width-20,text,app.normal,app.muted); }
    snprintf(detail,sizeof(detail),"%.0f logical CPUs",number(s,"cpu_count")); resource_meter(dc,start,width,ClassicEngine,"CPU",number(s,"cpu_percent"),cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(s,"cpu_percent"))?100:0,detail,compact);
    snprintf(detail,sizeof(detail),"%.1f / %.1f GiB",number(memory,"used")/1073741824.0,number(memory,"total")/1073741824.0); resource_meter(dc,start+step,width,ClassicModel,"Memory",number(memory,"used"),number(memory,"total"),detail,compact);
    snprintf(detail,sizeof(detail),"%.1f / %.1f GiB",number(disk,"used")/1073741824.0,number(disk,"total")/1073741824.0); resource_meter(dc,start+step*2,width,ClassicFolder,"Disk",number(disk,"used"),number(disk,"total"),detail,compact);
    model_y=height-(compact?42:59);
    if(height>=330) { snprintf(text,sizeof(text),"Process: %.1f MiB / %.0f threads",number(process,"rss")/1048576.0,number(process,"num_threads")); panel_text(dc,10,242,width-20,text,app.normal,app.muted); }
    panel_text(dc,10,model_y,width-20,"Server default model",app.heading,app.ink); panel_text(dc,10,model_y+19,width-20,*app.default_model?app.default_model:"Supplied by Hermes",app.normal,app.ink);
    if(!compact) { snprintf(text,sizeof(text),"Host readings / updated %llu s ago",(unsigned long long)((GetTickCount64()-app.stats_at)/1000)); panel_text(dc,10,height-19,width-20,text,app.normal,app.muted); }
}
static LRESULT CALLBACK monitor_proc(HWND w,UINT msg,WPARAM wp,LPARAM lp) {
    switch(msg) {
    case WM_PAINT: { PAINTSTRUCT paint; RECT r; HDC dc=BeginPaint(w,&paint),buffer=CreateCompatibleDC(dc); HBITMAP bitmap; HGDIOBJ old;
        GetClientRect(w,&r); bitmap=CreateCompatibleBitmap(dc,r.right>0?r.right:1,r.bottom>0?r.bottom:1);
        if(buffer&&bitmap) { old=SelectObject(buffer,bitmap); draw_monitor(w,buffer); BitBlt(dc,0,0,r.right,r.bottom,buffer,0,0,SRCCOPY); SelectObject(buffer,old); } else draw_monitor(w,dc);
        if(bitmap) DeleteObject(bitmap); if(buffer) DeleteDC(buffer); EndPaint(w,&paint); return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_PRINTCLIENT: draw_monitor(w,(HDC)wp); return 0;
    case WM_SETTEXT: { LRESULT result=DefWindowProcW(w,msg,wp,lp); InvalidateRect(w,NULL,FALSE); return result; }
    }
    return DefWindowProcW(w,msg,wp,lp);
}
static void draw_about(HDC dc) {
    RECT r={px(14),px(14),px(498),px(82)};
    SetDCBrushColor(dc,app.accent); FillRect(dc,&r,(HBRUSH)GetStockObject(DC_BRUSH)); classic_edge(dc,r,1);
    DrawIconEx(dc,px(28),px(32),LoadIconW(app.instance,MAKEINTRESOURCEW(101)),px(24),px(24),0,NULL,DI_NORMAL);
    about_text(dc,66,23,290,28,L"lcb-hermes",app.brandfont,app.ink); about_text(dc,436,28,48,20,L"0.4.0",app.normal,app.muted);
    about_text(dc,66,55,408,20,L"A classic desktop client for Hermes",app.normal,app.ink);
    r=(RECT){px(18),px(104),px(494),px(244)}; FillRect(dc,&r,(HBRUSH)GetStockObject(WHITE_BRUSH)); classic_edge(dc,r,0);
    if(app.nouslogo) { HDC source=CreateCompatibleDC(dc); HGDIOBJ old=SelectObject(source,app.nouslogo); SetStretchBltMode(dc,HALFTONE); SetBrushOrgEx(dc,0,0,NULL); StretchBlt(dc,px(32),px(122),px(104),px(104),source,0,0,app.logo_width,app.logo_height,SRCCOPY); SelectObject(source,old); DeleteDC(source); }
    about_text(dc,152,119,324,26,L"Hermes Agent",app.brandfont,app.ink);
    about_text(dc,152,153,324,20,L"Nous Research & the Hermes team",app.heading,app.ink);
    about_text(dc,152,180,324,20,L"Creators of the Hermes agent framework.",app.normal,app.muted);
    about_text(dc,18,266,470,22,L"Larkin Computing Bureau",app.heading,app.ink);
    about_text(dc,18,291,470,20,L"Classic controls and icons adapted from lcb-ai.",app.normal,app.muted);
    about_text(dc,18,313,470,20,L"JSON parsing: cJSON by Dave Gamble and contributors (MIT).",app.normal,app.muted);
}
static LRESULT CALLBACK about_proc(HWND w,UINT msg,WPARAM wp,LPARAM lp) {
    switch(msg) {
    case WM_CREATE: {
        HWND link; if(!app.nouslogo) app.nouslogo=resource_bitmap(102,&app.logo_width,&app.logo_height);
        link=child(w,WC_LINK,L"<a>Hermes project</a>",WS_TABSTOP,801); move(link,152,211,320,22);
        link=child(w,WC_LINK,L"<a>Nous Research</a>",WS_TABSTOP,802); move(link,18,346,240,22);
        move(child(w,L"BUTTON",L"&Close",WS_TABSTOP|BS_OWNERDRAW,IDCANCEL),404,340,90,28); return 0;
    }
    case WM_DRAWITEM: if(((DRAWITEMSTRUCT *)lp)->CtlType==ODT_BUTTON) { draw_button((DRAWITEMSTRUCT *)lp); return TRUE; } break;
    case WM_COMMAND: if(LOWORD(wp)==IDCANCEL) { DestroyWindow(w); return 0; } break;
    case WM_NOTIFY: { NMHDR *header=(NMHDR *)lp; if(header->code==NM_CLICK||header->code==NM_RETURN) {
        const wchar_t *url=header->idFrom==801?L"https://github.com/NousResearch/hermes-agent":header->idFrom==802?L"https://nousresearch.com/":NULL;
        if(url) ShellExecuteW(w,L"open",url,NULL,NULL,SW_SHOWNORMAL); return 0;
    } break; }
    case WM_CTLCOLORSTATIC: { HDC dc=(HDC)wp; COLORREF background=GetDlgCtrlID((HWND)lp)==801?RGB(255,255,255):app.bg; SetBkColor(dc,background); SetTextColor(dc,app.ink); SetDCBrushColor(dc,background); return (LRESULT)GetStockObject(DC_BRUSH); }
    case WM_ERASEBKGND: { RECT r; GetClientRect(w,&r); FillRect((HDC)wp,&r,app.face); return 1; }
    case WM_PAINT: { PAINTSTRUCT paint; HDC dc=BeginPaint(w,&paint); draw_about(dc); EndPaint(w,&paint); return 0; }
    case WM_PRINTCLIENT: draw_about((HDC)wp); return 0;
    case WM_CLOSE: DestroyWindow(w); return 0;
    case WM_DESTROY: app.aboutwin=NULL; return 0;
    }
    return DefWindowProcW(w,msg,wp,lp);
}
static void show_about(HWND owner) {
    RECT r={0,0,px(512),px(386)},parent; DWORD style=WS_CAPTION|WS_SYSMENU|WS_CLIPCHILDREN;
    if(app.aboutwin) { SetForegroundWindow(app.aboutwin); return; }
    AdjustWindowRectEx(&r,style,FALSE,WS_EX_DLGMODALFRAME); GetWindowRect(owner,&parent);
    app.aboutwin=CreateWindowExW(WS_EX_DLGMODALFRAME,L"LCBHermesAbout",L"About lcb-hermes",style,parent.left+(parent.right-parent.left-r.right+r.left)/2,parent.top+(parent.bottom-parent.top-r.bottom+r.top)/2,r.right-r.left,r.bottom-r.top,owner,NULL,app.instance,NULL);
    ShowWindow(app.aboutwin,SW_SHOW); SetFocus(GetDlgItem(app.aboutwin,IDCANCEL));
}
static void connect_to(const char *url,const char *user,const char *password,int allow,const char *cookie) {
    Pending *p; if(app.connecting) return; save(); app.connected=0; app.connecting=1;
    app.list_pending=app.list_again=0; KillTimer(app.window,IdList); cJSON_Delete(app.stats); app.stats=NULL; app.stats_at=0; status_stats();
    while((p=app.pending)!=NULL) { app.pending=p->next; if(p->chat) p->chat->pending=0; free(p); }
    app.notice[0]=0; net_connect(app.net,url,user,password,allow,cookie); controls();
}
static LRESULT CALLBACK server_proc(HWND w,UINT msg,WPARAM wp,LPARAM lp) {
    switch(msg) {
    case WM_CREATE: {
        HWND h; child(w,L"STATIC",L"Connect to your Hermes server",0,0); move(GetWindow(w,GW_CHILD),18,12,480,28);
        child(w,L"STATIC",L"URL",0,501); move(GetDlgItem(w,501),18,49,70,24); h=child(w,L"EDIT",L"",WS_TABSTOP|ES_AUTOHSCROLL,502); move(h,90,45,386,28); settext(h,app.url); SendMessageW(h,EM_SETCUEBANNER,FALSE,(LPARAM)L"https://server:port");
        h=child(w,L"EDIT",L"",WS_TABSTOP|ES_AUTOHSCROLL,503); move(h,18,87,200,28); settext(h,app.user); SendMessageW(h,EM_SETCUEBANNER,FALSE,(LPARAM)L"username");
        h=child(w,L"EDIT",L"",WS_TABSTOP|ES_PASSWORD|ES_AUTOHSCROLL,504); move(h,230,87,246,28); SendMessageW(h,EM_SETCUEBANNER,FALSE,(LPARAM)L"password");
        h=child(w,L"BUTTON",L"private HTTP",WS_TABSTOP|BS_AUTOCHECKBOX,506); move(h,18,127,180,26); SendMessageW(h,BM_SETCHECK,jb(app.profile,"http")?BST_CHECKED:BST_UNCHECKED,0);
        h=child(w,L"BUTTON",L"connect",WS_TABSTOP,505); move(h,376,125,100,29);
        h=child(w,L"EDIT",L"",ES_MULTILINE|ES_READONLY|WS_VSCROLL,507); move(h,18,197,458,235);
        h=child(w,L"STATIC",L"",0,508); move(h,18,164,458,26);
        h=child(w,L"BUTTON",L"refresh stats",WS_TABSTOP,IdRefresh); move(h,18,447,114,29);
        h=child(w,L"BUTTON",L"about",WS_TABSTOP,IdAbout); move(h,140,447,114,29);
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
        case IdRefresh: poll_stats(); break;
        case IdAbout: show_about(w); break;
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
    app.serverwin=CreateWindowExW(WS_EX_DLGMODALFRAME,L"LCBHermesServer",*app.url?L"Server connection":L"Welcome to lcb-hermes - Server setup",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU,0,0,px(510),px(566),app.window,NULL,app.instance,NULL);
    { RECT parent; GetWindowRect(app.window,&parent); SetWindowPos(app.serverwin,NULL,parent.left+px(120),parent.top+px(55),0,0,SWP_NOSIZE|SWP_NOZORDER); }
    ShowWindow(app.serverwin,SW_SHOW); controls(); status_stats(); poll_stats(); SetFocus(GetDlgItem(app.serverwin,502));
}
static const cJSON *chosen_project(HWND w) {
    LRESULT index=SendMessageW(GetDlgItem(w,601),CB_GETCURSEL,0,0),data;
    if(index==CB_ERR) return NULL; data=SendMessageW(GetDlgItem(w,601),CB_GETITEMDATA,(WPARAM)index,0);
    return data==CB_ERR?NULL:cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(app.projects,"projects"),(int)data);
}
static void project_details(HWND w) {
    const cJSON *p=chosen_project(w),*f; HWND list=GetDlgItem(w,609); SendMessageW(list,LB_RESETCONTENT,0,0);
    settext(GetDlgItem(w,602),js(p,"name")); settext(GetDlgItem(w,603),js(p,"primary_path"));
    cJSON_ArrayForEach(f,cJSON_GetObjectItemCaseSensitive(p,"folders")) { wchar_t *text=wide(js(f,"path")); if(text) { SendMessageW(list,LB_ADDSTRING,0,(LPARAM)text); free(text); } }
    EnableWindow(GetDlgItem(w,606),p!=NULL&&!app.project_pending); EnableWindow(GetDlgItem(w,607),p!=NULL);
}
static int folder_choice(const char *label,const char *path) {
    wchar_t *text; LRESULT at,count=SendMessageW(app.workspace,CB_GETCOUNT,0,0),i;
    for(i=1;i<count;i++) { const char *old=(const char *)SendMessageW(app.workspace,CB_GETITEMDATA,(WPARAM)i,0); if(old && (LRESULT)old!=CB_ERR && !strcmp(old,path)) return (int)i; }
    text=wide(label); at=SendMessageW(app.workspace,CB_ADDSTRING,0,(LPARAM)text); free(text); SendMessageW(app.workspace,CB_SETITEMDATA,(WPARAM)at,(LPARAM)path); return (int)at;
}
static void project_choices(void) {
    const cJSON *p,*f; int index=0,selected=0; HWND combo=app.workspace,projects=app.projectwin?GetDlgItem(app.projectwin,601):NULL;
    SendMessageW(combo,CB_RESETCONTENT,0,0); SendMessageW(combo,CB_ADDSTRING,0,(LPARAM)L"Server default folder");
    if(projects) SendMessageW(projects,CB_RESETCONTENT,0,0);
    cJSON_ArrayForEach(p,cJSON_GetObjectItemCaseSensitive(app.projects,"projects")) {
        if(jb(p,"archived")) { index++; continue; }
        if(projects) { wchar_t *label=wide(js(p,"name")); LRESULT at=SendMessageW(projects,CB_ADDSTRING,0,(LPARAM)label); SendMessageW(projects,CB_SETITEMDATA,(WPARAM)at,index); free(label); }
        cJSON_ArrayForEach(f,cJSON_GetObjectItemCaseSensitive(p,"folders")) { char text[1400]; int at;
            snprintf(text,sizeof(text),"%s / %s",js(p,"name"),js(f,"path")); at=folder_choice(text,js(f,"path"));
            if(!strcmp(app.newcwd,js(f,"path"))) selected=at;
        } index++;
    }
    cJSON_ArrayForEach(p,cJSON_GetObjectItemCaseSensitive(app.project_tree,"projects")) if(!jb(p,"isNoProject")&&*js(p,"path")) { char label[1400]; int at; snprintf(label,sizeof(label),"%s / %s",js(p,"label"),js(p,"path")); at=folder_choice(label,js(p,"path")); if(!strcmp(app.newcwd,js(p,"path"))) selected=at; }
    if(*app.newcwd && !selected) selected=folder_choice(app.newcwd,app.newcwd);
    SendMessageW(combo,CB_SETCURSEL,selected,0); if(projects) { SendMessageW(projects,CB_SETCURSEL,0,0); project_details(app.projectwin); }
    controls();
}
static void choose_workspace(const char *path) {
    if(path!=app.newcwd) copytext(app.newcwd,sizeof(app.newcwd),path); if(app.profile) put(app.profile,"workspace",app.newcwd);
    /* A resumed chat keeps its server cwd; a fresh draft can still choose its folder. */
    if(app.current && !*app.current->id && !*app.current->stored) { copytext(app.current->cwd,sizeof(app.current->cwd),path); sidebar(); }
    project_choices(); controls(); schedule_save();
}
static void workspace_choice(void) {
    LRESULT selected=SendMessageW(app.workspace,CB_GETCURSEL,0,0); const char *path;
    if(selected==CB_ERR) return; if(!selected) { choose_workspace(""); return; }
    path=(const char *)SendMessageW(app.workspace,CB_GETITEMDATA,(WPARAM)selected,0); if(path&&(LRESULT)path!=CB_ERR) choose_workspace(path);
}
static LRESULT CALLBACK project_proc(HWND w,UINT msg,WPARAM wp,LPARAM lp) {
    switch(msg) {
    case WM_CREATE: {
        HWND h; h=child(w,L"STATIC",L"Hermes workspaces",0,0); move(h,18,12,520,24);
        h=child(w,L"STATIC",L"Projects and folders are saved on your Hermes server.",0,0); move(h,18,43,540,22);
        h=child(w,L"COMBOBOX",L"",WS_TABSTOP|CBS_DROPDOWNLIST|WS_VSCROLL,601); move(h,18,77,530,200);
        h=child(w,L"STATIC",L"Name",0,0); move(h,18,116,75,22); h=child(w,L"EDIT",L"",WS_TABSTOP|ES_AUTOHSCROLL,602); move(h,98,111,450,27);
        h=child(w,L"STATIC",L"Server folder",0,0); move(h,18,154,90,22); h=child(w,L"EDIT",L"",WS_TABSTOP|ES_AUTOHSCROLL,603); move(h,112,149,436,27); SendMessageW(h,EM_SETCUEBANNER,FALSE,(LPARAM)L"/path/on/hermes/server");
        h=child(w,L"BUTTON",L"Make primary folder",WS_TABSTOP|BS_AUTOCHECKBOX,604); move(h,18,186,210,25);
        h=child(w,L"BUTTON",L"Create workspace",WS_TABSTOP,605); move(h,18,222,170,29); h=child(w,L"BUTTON",L"Add folder to selected",WS_TABSTOP,606); move(h,200,222,194,29);
        h=child(w,L"BUTTON",L"Refresh",WS_TABSTOP,IdRefresh); move(h,406,222,142,29);
        h=child(w,L"LISTBOX",L"",WS_TABSTOP|LBS_NOTIFY|WS_VSCROLL|WS_BORDER,609); move(h,18,266,530,125);
        h=child(w,L"STATIC",L"Select a folder below, then use it for new chats. Existing chats keep their folder.",0,608); move(h,18,401,530,44);
        h=child(w,L"BUTTON",L"Use selected folder",WS_TABSTOP,607); move(h,18,453,200,29); h=child(w,L"BUTTON",L"Done",WS_TABSTOP,IDCANCEL); move(h,406,453,142,29); return 0;
    }
    case WM_COMMAND:
        switch(LOWORD(wp)) {
        case 601: if(HIWORD(wp)==CBN_SELCHANGE) project_details(w); break;
        case IdRefresh: if(app.connected) { rpc("projects.list",params(),RpcProjects,NULL); project_tree(); } break;
        case 605: case 606: {
            const cJSON *project=chosen_project(w); char *name=gettext(GetDlgItem(w,602)),*path=gettext(GetDlgItem(w,603)); cJSON *p=params();
            if(app.project_pending) { cJSON_Delete(p); free(name); free(path); break; }
            if(!app.connected) { settext(GetDlgItem(w,608),"Connect to Hermes first."); }
            else if(!*path || (LOWORD(wp)==605 && !*name)) settext(GetDlgItem(w,608),"Enter a workspace name and an existing folder on the server.");
            else if(LOWORD(wp)==605) { cJSON *folders=cJSON_CreateArray(); cJSON_AddStringToObject(p,"name",name); cJSON_AddItemToArray(folders,cJSON_CreateString(path)); cJSON_AddItemToObject(p,"folders",folders); cJSON_AddStringToObject(p,"primary_path",path); rpc("projects.create",p,RpcProjectCreate,NULL); p=NULL; settext(GetDlgItem(w,608),"Creating workspace..."); }
            else if(project) { cJSON_AddStringToObject(p,"id",js(project,"id")); cJSON_AddStringToObject(p,"path",path); cJSON_AddBoolToObject(p,"is_primary",SendMessageW(GetDlgItem(w,604),BM_GETCHECK,0,0)==BST_CHECKED); rpc("projects.add_folder",p,RpcProjectAdd,NULL); p=NULL; settext(GetDlgItem(w,608),"Adding folder..."); }
            if(!p) { app.project_pending=1; EnableWindow(GetDlgItem(w,605),FALSE); EnableWindow(GetDlgItem(w,606),FALSE); }
            cJSON_Delete(p); free(name); free(path); break;
        }
        case 607: {
            const cJSON *project=chosen_project(w),*folder; LRESULT selected=SendMessageW(GetDlgItem(w,609),LB_GETCURSEL,0,0);
            folder=cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(project,"folders"),(int)selected);
            if(folder) { choose_workspace(js(folder,"path")); settext(GetDlgItem(w,608),"Folder selected for new chats."); } break;
        }
        case IDCANCEL: DestroyWindow(w); break;
        } return 0;
    case WM_CLOSE: DestroyWindow(w); return 0;
    case WM_DESTROY: app.projectwin=NULL; return 0;
    case WM_CTLCOLORSTATIC: case WM_CTLCOLOREDIT: case WM_CTLCOLORLISTBOX: return color_control(msg,wp,lp);
    case WM_ERASEBKGND: { RECT r; GetClientRect(w,&r); FillRect((HDC)wp,&r,app.face); return 1; }
    } return DefWindowProcW(w,msg,wp,lp);
}
static void show_projects(void) {
    RECT parent; if(!app.connected||!app.projects_supported) { notice("This server has not advertised workspace support. Connect or refresh first."); return; }
    if(app.projectwin) { SetForegroundWindow(app.projectwin); return; }
    app.projectwin=CreateWindowExW(WS_EX_DLGMODALFRAME,L"LCBHermesProjects",L"Workspaces and server folders",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU,0,0,px(582),px(538),app.window,NULL,app.instance,NULL);
    GetWindowRect(app.window,&parent); SetWindowPos(app.projectwin,NULL,parent.left+px(100),parent.top+px(60),0,0,SWP_NOSIZE|SWP_NOZORDER); project_choices(); ShowWindow(app.projectwin,SW_SHOW); SetFocus(GetDlgItem(app.projectwin,601));
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
static HMENU workbench_menu(void) {
    HMENU bar=CreateMenu(),file=CreatePopupMenu(),view=CreatePopupMenu(),server=CreatePopupMenu(),help=CreatePopupMenu();
    AppendMenuW(file,MF_STRING,IdNew,L"&New chat\tCtrl+N"); AppendMenuW(file,MF_STRING,IdExport,L"&Export chat..."); AppendMenuW(file,MF_STRING,IdDelete,L"&Delete chat..."); AppendMenuW(file,MF_SEPARATOR,0,NULL); AppendMenuW(file,MF_STRING,IdExit,L"E&xit");
    AppendMenuW(view,MF_STRING|MF_CHECKED,IdMonitor,L"&Server monitor"); AppendMenuW(view,MF_STRING,IdWider,L"Wider chat list"); AppendMenuW(view,MF_STRING,IdNarrower,L"Narrower chat list"); AppendMenuW(view,MF_SEPARATOR,0,NULL); AppendMenuW(view,MF_STRING,IdExpand,L"&Expand folders"); AppendMenuW(view,MF_STRING,IdCollapse,L"&Collapse folders");
    AppendMenuW(server,MF_STRING,IdServer,L"&Connection..."); AppendMenuW(server,MF_STRING,IdReconnect,L"&Reconnect\tCtrl+R"); AppendMenuW(server,MF_STRING,IdRefresh,L"&Refresh chats and workspaces"); AppendMenuW(server,MF_STRING,IdProjects,L"&Workspaces and folders...");
    AppendMenuW(help,MF_STRING,IdAbout,L"&About lcb-hermes...");
    AppendMenuW(bar,MF_POPUP,(UINT_PTR)file,L"&File"); AppendMenuW(bar,MF_POPUP,(UINT_PTR)view,L"&View"); AppendMenuW(bar,MF_POPUP,(UINT_PTR)server,L"&Server"); AppendMenuW(bar,MF_POPUP,(UINT_PTR)help,L"&Help"); return bar;
}
static void paint_header(HDC dc,HWND childwindow) {
    RECT r; GetWindowRect(childwindow,&r); MapWindowPoints(NULL,app.window,(POINT *)&r,2); InflateRect(&r,4,3); SetDCBrushColor(dc,app.accent); FillRect(dc,&r,(HBRUSH)GetStockObject(DC_BRUSH)); classic_edge(dc,r,1);
}
static void draw_workbench(HDC dc) {
    RECT r={px(4),px(4),app.width-px(4),px(44)}; HGDIOBJ old=SelectObject(dc,app.normal); int height=MulDiv(app.height,96,app.dpi);
    { RECT background={0,0,app.width,app.height}; FillRect(dc,&background,app.face); }
    classic_edge(dc,r,1); paint_header(dc,app.explorerhead); paint_header(dc,app.title); if(app.monitor_visible) { paint_header(dc,app.monitorhead); paint_header(dc,app.activitytitle); }
    r=(RECT){px(app.split_x-4),px(50),px(app.split_x+4),px(height-31)}; classic_edge(dc,r,1);
    { int y; for(y=height/2-16;y<height/2+16;y+=4) { RECT dot={px(app.split_x-1),px(y),px(app.split_x+1),px(y+2)}; SetDCBrushColor(dc,app.muted); FillRect(dc,&dot,(HBRUSH)GetStockObject(DC_BRUSH)); } }
    SetBkMode(dc,TRANSPARENT); SetTextColor(dc,app.muted);
    { const wchar_t *folder=L"Folder for new chats",*message=L"Message  /  Ctrl+Enter to send"; TextOutW(dc,px(12),px(height-118),folder,(int)wcslen(folder)); TextOutW(dc,px(app.split_x+14),px(height-176),message,(int)wcslen(message)); }
    r=(RECT){px(4),px(height-28),app.width-px(4),app.height-px(3)}; classic_edge(dc,r,0); SelectObject(dc,old);
}
static BOOL CALLBACK font_child(HWND w,LPARAM value) { SendMessageW(w,WM_SETFONT,(WPARAM)value,TRUE); return TRUE; }
static void update_fonts(void) {
    HFONT normal=app.normal,fixed=app.fixed,heading=app.heading,brand=app.brandfont;
    app.normal=CreateFontW(-px(13),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Tahoma");
    app.fixed=CreateFontW(-px(12),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,FIXED_PITCH,L"Consolas");
    app.heading=CreateFontW(-px(13),0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Tahoma");
    app.brandfont=CreateFontW(-px(21),0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Tahoma");
    EnumChildWindows(app.window,font_child,(LPARAM)app.normal); if(app.serverwin) EnumChildWindows(app.serverwin,font_child,(LPARAM)app.normal); if(app.projectwin) EnumChildWindows(app.projectwin,font_child,(LPARAM)app.normal); if(app.aboutwin) EnumChildWindows(app.aboutwin,font_child,(LPARAM)app.normal);
    SendMessageW(app.explorerhead,WM_SETFONT,(WPARAM)app.heading,TRUE); SendMessageW(app.title,WM_SETFONT,(WPARAM)app.heading,TRUE); SendMessageW(app.monitorhead,WM_SETFONT,(WPARAM)app.heading,TRUE); SendMessageW(app.activitytitle,WM_SETFONT,(WPARAM)app.heading,TRUE);
    if(app.tree) { HIMAGELIST old=app.icons; app.icons=classic_icons(app.dpi); TreeView_SetImageList(app.tree,app.icons,TVSIL_NORMAL); if(old) ImageList_Destroy(old); }
    if(normal) DeleteObject(normal); if(fixed) DeleteObject(fixed); if(heading) DeleteObject(heading); if(brand) DeleteObject(brand); app.view_valid=0; app.activity_chat=NULL;
}
static void paint_workbench(HDC dc) {
    HDC buffer=CreateCompatibleDC(dc); HBITMAP bitmap=CreateCompatibleBitmap(dc,app.width>0?app.width:1,app.height>0?app.height:1); HGDIOBJ old;
    if(buffer&&bitmap) { old=SelectObject(buffer,bitmap); draw_workbench(buffer); BitBlt(dc,0,0,app.width,app.height,buffer,0,0,SRCCOPY); SelectObject(buffer,old); } else draw_workbench(dc);
    if(bitmap) DeleteObject(bitmap); if(buffer) DeleteDC(buffer);
}
static LRESULT CALLBACK window_proc(HWND w,UINT msg,WPARAM wp,LPARAM lp) {
    switch(msg) {
    case WM_CREATE:
        app.window=w; app.dpi=(int)GetDpiForWindow(w); app.rich=LoadLibraryW(L"Msftedit.dll");
        update_fonts();
        app.fresh=child(w,L"BUTTON",L"New chat",WS_TABSTOP|BS_OWNERDRAW,IdNew); app.server=child(w,L"BUTTON",L"",WS_TABSTOP|BS_OWNERDRAW,IdServer); app.title=child(w,L"STATIC",L"new chat",SS_ENDELLIPSIS,0); app.model=child(w,L"BUTTON",L"server default",WS_TABSTOP|BS_OWNERDRAW,IdModel); app.search=child(w,L"EDIT",L"",WS_TABSTOP|ES_AUTOHSCROLL,IdSearch); SendMessageW(app.search,EM_SETCUEBANNER,FALSE,(LPARAM)L"search chats");
        app.tree=child(w,WC_TREEVIEWW,L"",WS_TABSTOP|TVS_SHOWSELALWAYS|TVS_INFOTIP|TVS_HASBUTTONS|TVS_HASLINES|TVS_LINESATROOT,IdTree); SendMessageW(app.tree,TVM_SETEXTENDEDSTYLE,TVS_EX_DOUBLEBUFFER,TVS_EX_DOUBLEBUFFER); app.icons=classic_icons(app.dpi); TreeView_SetImageList(app.tree,app.icons,TVSIL_NORMAL);
        app.tooltip=CreateWindowExW(WS_EX_TOPMOST,TOOLTIPS_CLASSW,NULL,WS_POPUP|TTS_ALWAYSTIP,0,0,0,0,w,NULL,app.instance,NULL); { TOOLINFOW t={0}; t.cbSize=sizeof(t); t.uFlags=TTF_IDISHWND|TTF_SUBCLASS; t.hwnd=w; t.uId=(UINT_PTR)app.server; t.lpszText=L"Server connection and login"; SendMessageW(app.tooltip,TTM_ADDTOOLW,0,(LPARAM)&t); t.uFlags=TTF_SUBCLASS; t.uId=1; t.lpszText=L"Drag to resize the chat list"; SendMessageW(app.tooltip,TTM_ADDTOOLW,0,(LPARAM)&t); }
        app.transcript=child(w,MSFTEDIT_CLASS,L"",WS_TABSTOP|ES_MULTILINE|ES_READONLY|WS_VSCROLL,0); SendMessageW(app.transcript,EM_EXLIMITTEXT,0,WIRE_LIMIT); SetWindowSubclass(app.transcript,transcript_proc,1,0); SendMessageW(app.transcript,EM_SETOPTIONS,ECOOP_OR,ECO_NOHIDESEL);
        app.prompt=child(w,L"EDIT",L"",WS_TABSTOP|ES_MULTILINE|ES_AUTOVSCROLL|WS_VSCROLL,IdPrompt); SendMessageW(app.prompt,EM_SETLIMITTEXT,256*1024,0); SetWindowSubclass(app.prompt,prompt_proc,1,0);
        app.send=child(w,L"BUTTON",L"send",WS_TABSTOP|BS_OWNERDRAW,IdSend); app.attach=child(w,L"BUTTON",L"attach",WS_TABSTOP|BS_OWNERDRAW,IdAttach); app.request=child(w,L"BUTTON",L"export chat",WS_TABSTOP|BS_OWNERDRAW,IdRequest); app.status=child(w,L"STATIC",L"offline",SS_LEFTNOWORDWRAP,0); app.net=net_new(w);
        app.explorerhead=child(w,L"STATIC",L"Conversations",SS_ENDELLIPSIS,0); app.monitorhead=child(w,L"STATIC",L"Server monitor",0,0); app.monitor=child(w,L"LCBHermesMonitor",L"",0,0);
        app.activitytitle=child(w,L"STATIC",L"Hermes activity",SS_ENDELLIPSIS,0); app.activityhead=child(w,L"STATIC",L"Ready",SS_ENDELLIPSIS,0); app.activityview=child(w,MSFTEDIT_CLASS,L"",WS_TABSTOP|ES_MULTILINE|ES_READONLY|WS_VSCROLL,0); SendMessageW(app.activityview,EM_SETTARGETDEVICE,0,0); SendMessageW(app.activityview,EM_EXLIMITTEXT,0,32000); app.progress=child(w,PROGRESS_CLASSW,L"",PBS_MARQUEE,0);
        app.workspace=child(w,L"COMBOBOX",L"",WS_TABSTOP|CBS_DROPDOWNLIST|WS_VSCROLL,IdWorkspace); app.workspacelabel=child(w,L"STATIC",L"Workspace: server default",SS_ENDELLIPSIS,0); app.projectbutton=child(w,L"BUTTON",L"Workspaces...",WS_TABSTOP|BS_OWNERDRAW,IdProjects); app.summary=child(w,L"STATIC",L"",SS_ENDELLIPSIS,0);
        SendMessageW(app.explorerhead,WM_SETFONT,(WPARAM)app.heading,TRUE); SendMessageW(app.title,WM_SETFONT,(WPARAM)app.heading,TRUE); SendMessageW(app.monitorhead,WM_SETFONT,(WPARAM)app.heading,TRUE); SendMessageW(app.activitytitle,WM_SETFONT,(WPARAM)app.heading,TRUE);
        SetMenu(w,workbench_menu()); CheckMenuItem(GetMenu(w),IdMonitor,MF_BYCOMMAND|(app.show_monitor?MF_CHECKED:MF_UNCHECKED));
        theme(); SetTimer(w,IdHeartbeat,20000,NULL); SetTimer(w,IdTimeout,1000,NULL); SetTimer(w,IdStats,5000,NULL); return 0;
    case WM_SIZE: app.width=LOWORD(lp); app.height=HIWORD(lp); if(app.width&&app.height) layout(); return 0;
    case WM_GETMINMAXINFO: ((MINMAXINFO *)lp)->ptMinTrackSize.x=px(620); ((MINMAXINFO *)lp)->ptMinTrackSize.y=px(450); return 0;
    case WM_DPICHANGED: {
        RECT *r=(RECT *)lp; app.dpi=LOWORD(wp); update_fonts(); SetWindowPos(w,NULL,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOCOPYBITS); render(); return 0;
    }
    case NET_MESSAGE: on_net((NetMessage *)lp); return 0;
    case WM_DRAWITEM: if(((DRAWITEMSTRUCT *)lp)->CtlType==ODT_BUTTON) { draw_button((DRAWITEMSTRUCT *)lp); return TRUE; } break;
    case WM_LBUTTONDOWN: if(GET_Y_LPARAM(lp)>=px(50)&&GET_Y_LPARAM(lp)<app.height-px(30)&&abs(GET_X_LPARAM(lp)-px(app.split_x))<=px(6)) { app.dragging=1; SetCapture(w); SetCursor(LoadCursorW(NULL,IDC_SIZEWE)); } return 0;
    case WM_MOUSEMOVE: if(app.dragging) { int desired=MulDiv(GET_X_LPARAM(lp),96,app.dpi),maximum=MulDiv(app.width,96,app.dpi)-370; if(desired<180) desired=180; if(desired>maximum) desired=maximum; app.sidebar_width=desired; layout(); } return 0;
    case WM_LBUTTONUP: if(app.dragging) { app.dragging=0; ReleaseCapture(); schedule_save(); } return 0;
    case WM_CAPTURECHANGED: app.dragging=0; return 0;
    case WM_SETCURSOR: { POINT p; GetCursorPos(&p); ScreenToClient(w,&p); if(LOWORD(lp)==HTCLIENT && (app.dragging||(p.y>=px(50)&&p.y<app.height-px(30)&&abs(p.x-px(app.split_x))<=px(6)))) { SetCursor(LoadCursorW(NULL,IDC_SIZEWE)); return TRUE; } break; }
    case WM_COMMAND:
        switch(LOWORD(wp)) {
        case IdNew: newchat(); break;
        case IdServer: show_server(); break;
        case IdProjects: show_projects(); break;
        case IdWorkspace: if(HIWORD(wp)==CBN_SELCHANGE) workspace_choice(); break;
        case IdWider: case IdNarrower: app.sidebar_width=app.split_x+(LOWORD(wp)==IdWider?48:-48); if(app.sidebar_width<180) app.sidebar_width=180; if(app.sidebar_width>MulDiv(app.width,96,app.dpi)-370) app.sidebar_width=MulDiv(app.width,96,app.dpi)-370; layout(); schedule_save(); break;
        case IdMonitor: app.show_monitor=!app.show_monitor; CheckMenuItem(GetMenu(w),IdMonitor,MF_BYCOMMAND|(app.show_monitor?MF_CHECKED:MF_UNCHECKED)); layout(); schedule_save(); break;
        case IdExpand: case IdCollapse: { size_t i; for(i=0;i<app.ngroups;i++) TreeView_Expand(app.tree,app.groups[i].item,LOWORD(wp)==IdExpand?TVE_EXPAND:TVE_COLLAPSE); break; }
        case IdExport: export_chat(); break;
        case IdAbout: show_about(w); break;
        case IdExit: PostMessageW(w,WM_CLOSE,0,0); break;
        case IdModel: if(app.connected) models(); else notice("Connect before choosing a model."); break;
        case IdSend: submit(); break;
        case IdAttach: attach(); break;
        case IdRequest: if(app.current&&cJSON_GetArraySize(app.current->requests)) request_dialog(); else export_chat(); break;
        case IdPrompt: if(HIWORD(wp)==EN_CHANGE && !app.restoring && app.current) { free(app.current->draft); app.current->draft=gettext(app.prompt); schedule_save(); } break;
        case IdSearch: if(HIWORD(wp)==EN_CHANGE) sidebar(); break;
        case IdReconnect: if(*app.url) connect_to(app.url,app.user,"",jb(app.profile,"http"),app.cookie); else show_server(); break;
        case IdRefresh: if(app.connected) { listchats(); rpc("projects.list",params(),RpcProjects,NULL); project_tree(); poll_stats(); } break;
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
        if(h->hwndFrom==app.tree && h->code==TVN_SELCHANGEDW && !app.treebusy) { Chat *c=(Chat *)((NMTREEVIEWW *)lp)->itemNew.lParam; if(c&&c!=app.current) { app.current=c; c->unread=0; app.notice[0]=0; if(*c->stored&&!c->loaded) resume(c); render(); sidebar(); schedule_save(); } }
        if(h->hwndFrom==app.tree && h->code==TVN_KEYDOWN && ((NMTVKEYDOWN *)lp)->wVKey==VK_DELETE) { TVITEMW item={0}; item.hItem=TreeView_GetSelection(app.tree); item.mask=TVIF_PARAM; if(TreeView_GetItem(app.tree,&item)&&item.lParam) PostMessageW(w,WM_COMMAND,IdDelete,0); }
        return 0;
    }
    case WM_CONTEXTMENU: {
        if((HWND)wp==app.tree) { HMENU menu=CreatePopupMenu(); POINT point={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)}; TVITEMW selected={0};
            if(point.x!=-1) { TVHITTESTINFO hit={0}; hit.pt=point; ScreenToClient(app.tree,&hit.pt); if(TreeView_HitTest(app.tree,&hit)) TreeView_SelectItem(app.tree,hit.hItem); }
            if(point.x==-1) { RECT r; GetWindowRect(app.tree,&r); point.x=r.left+20; point.y=r.top+20; }
            selected.hItem=TreeView_GetSelection(app.tree); selected.mask=TVIF_PARAM; TreeView_GetItem(app.tree,&selected);
            AppendMenuW(menu,MF_STRING,IdRefresh,L"refresh chats"); AppendMenuW(menu,MF_STRING|(!selected.lParam?MF_GRAYED:0),IdDelete,L"delete chat..."); AppendMenuW(menu,MF_STRING,IdRemoveAttachment,L"clear draft attachments"); TrackPopupMenu(menu,TPM_RIGHTBUTTON,point.x,point.y,0,w,NULL); DestroyMenu(menu);
        } return 0;
    }
    case WM_TIMER:
        if(wp==IdRender) render(); else if(wp==IdSave) save(); else if(wp==IdHeartbeat&&app.connected) rpc("ping",params(),RpcPing,NULL);
        else if(wp==IdStats) { if(app.monitor_visible||app.serverwin) poll_stats(); status_stats(); }
        else if(wp==IdList) listchats();
        else if(wp==IdTimeout) { Pending **link=&app.pending; ULONGLONG now=GetTickCount64(); while(*link) { Pending *p=*link; if(now-p->started>90000) { *link=p->next; result(p,NULL,"Server request timed out. The request was not retried."); free(p); } else link=&p->next; } } return 0;
    case WM_CTLCOLORSTATIC: case WM_CTLCOLOREDIT: case WM_CTLCOLORLISTBOX: return color_control(msg,wp,lp);
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: { PAINTSTRUCT paint; HDC dc=BeginPaint(w,&paint); paint_workbench(dc); EndPaint(w,&paint); return 0; }
    case WM_PRINTCLIENT: draw_workbench((HDC)wp); return 0;
    case WM_CLOSE: save(); if(app.aboutwin) DestroyWindow(app.aboutwin); if(app.serverwin) DestroyWindow(app.serverwin); if(app.projectwin) DestroyWindow(app.projectwin); DestroyWindow(w); return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(w,msg,wp,lp);
}
static int create_window(HINSTANCE instance,int show) {
    WNDCLASSEXW wc={0}; INITCOMMONCONTROLSEX cc={sizeof(cc),ICC_TREEVIEW_CLASSES|ICC_STANDARD_CLASSES|ICC_LINK_CLASS}; HWND w;
    app.instance=instance; app.dpi=96; InitCommonControlsEx(&cc); CoInitializeEx(NULL,COINIT_APARTMENTTHREADED);
    wc.cbSize=sizeof(wc); wc.hInstance=instance; wc.hCursor=LoadCursorW(NULL,IDC_ARROW); wc.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(101)); wc.lpszClassName=L"LCBHermes"; wc.lpfnWndProc=window_proc; RegisterClassExW(&wc);
    wc.lpszClassName=L"LCBHermesServer"; wc.lpfnWndProc=server_proc; RegisterClassExW(&wc); wc.lpszClassName=L"LCBHermesQuestion"; wc.lpfnWndProc=question_proc; RegisterClassExW(&wc); wc.lpszClassName=L"LCBHermesProjects"; wc.lpfnWndProc=project_proc; RegisterClassExW(&wc);
    wc.lpszClassName=L"LCBHermesMonitor"; wc.lpfnWndProc=monitor_proc; RegisterClassExW(&wc);
    wc.lpszClassName=L"LCBHermesAbout"; wc.lpfnWndProc=about_proc; RegisterClassExW(&wc);
    w=CreateWindowExW(0,L"LCBHermes",L"lcb-hermes",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,1180,800,NULL,NULL,instance,NULL); if(!w) return 0; ShowWindow(w,show); UpdateWindow(w); return 1;
}
static void cleanup(void) {
    MSG msg; Pending *p; net_free(app.net); while(PeekMessageW(&msg,app.window,NET_MESSAGE,NET_MESSAGE,PM_REMOVE)) { NetMessage *m=(NetMessage *)msg.lParam; if(m->tag==RestUpload) free(m->context); cJSON_Delete(m->json); free(m); }
    while((p=app.pending)!=NULL) { app.pending=p->next; free(p); } clear_chats(); previews_clear(); cJSON_Delete(app.settings); cJSON_Delete(app.stats); cJSON_Delete(app.models); cJSON_Delete(app.projects); cJSON_Delete(app.project_tree);
    free(app.view_rows); free(app.activity_text); if(app.nouslogo) DeleteObject(app.nouslogo); if(app.icons) ImageList_Destroy(app.icons); if(app.normal) DeleteObject(app.normal); if(app.fixed) DeleteObject(app.fixed); if(app.heading) DeleteObject(app.heading); if(app.brandfont) DeleteObject(app.brandfont); if(app.face) DeleteObject(app.face); if(app.paper) DeleteObject(app.paper); if(app.rich) FreeLibrary(app.rich); CoUninitialize();
}
static void startup_connection(void) {
    if(*app.url&&*app.cookie) connect_to(app.url,app.user,"",jb(app.profile,"http"),app.cookie);
    else show_server();
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE previous,PWSTR command,int show) {
    MSG msg; cJSON *profiles,*p; (void)previous;(void)command;
    if(!store_path(app.path,1024)) return 1; app.settings=store_read(app.path); if(!app.settings) { app.corrupt=1; app.settings=cJSON_CreateObject(); }
    app.sidebar_width=(int)number(app.settings,"sidebar_width"); if(app.sidebar_width<160||app.sidebar_width>10000) app.sidebar_width=240;
    app.show_monitor=!cJSON_HasObjectItem(app.settings,"monitor")||jb(app.settings,"monitor"); copytext(app.url,sizeof(app.url),js(app.settings,"active_url")); copytext(app.user,sizeof(app.user),js(app.settings,"active_user"));
    if(!create_window(instance,show)) return 1;
    profile_load(app.url,app.user); sidebar(); render(); if(app.corrupt) notice("Saved settings could not be read. They were preserved; reconnect without saving.");
    profiles=cJSON_GetObjectItemCaseSensitive(app.settings,"profiles"); cJSON_ArrayForEach(p,profiles) if(!strcmp(js(p,"url"),app.url)&&!strcmp(js(p,"user"),app.user)) { copytext(app.cookie,sizeof(app.cookie),js(p,"cookie")); break; }
    startup_connection();
    while(GetMessageW(&msg,NULL,0,0)>0) { if(app.aboutwin&&IsDialogMessageW(app.aboutwin,&msg)) continue;
        if(app.serverwin&&IsDialogMessageW(app.serverwin,&msg)) continue;
        if(app.projectwin&&IsDialogMessageW(app.projectwin,&msg)) continue;
        if(msg.message==WM_KEYDOWN&&GetKeyState(VK_CONTROL)<0) { if(msg.wParam=='N') { newchat(); continue; } if(msg.wParam=='R') { SendMessageW(app.window,WM_COMMAND,IdReconnect,0); continue; } }
        if(msg.message==WM_KEYDOWN&&msg.wParam==VK_TAB&&IsDialogMessageW(app.window,&msg)) continue;
        TranslateMessage(&msg); DispatchMessageW(&msg);
    }
    cleanup(); return 0;
}
