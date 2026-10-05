#define wWinMain hermes_entry
#include "../src/winmain.c"
#undef wWinMain
#include <assert.h>
static BOOL CALLBACK print_child(HWND child,LPARAM value) {
    HDC dc=(HDC)value; RECT r; int saved=SaveDC(dc); GetWindowRect(child,&r); MapWindowPoints(NULL,app.window,(POINT *)&r,2); SetViewportOrgEx(dc,r.left,r.top,NULL);
    SendMessageW(child,WM_PRINT,(WPARAM)dc,PRF_CLIENT|PRF_ERASEBKGND|PRF_NONCLIENT); RestoreDC(dc,saved); return TRUE;
}
static void screenshot(const wchar_t *path) {
    RECT r; HDC dc,mem; HBITMAP bitmap; HGDIOBJ old; IWICImagingFactory *factory=NULL; IWICBitmap *source=NULL; IWICStream *stream=NULL; IWICBitmapEncoder *encoder=NULL; IWICBitmapFrameEncode *frame=NULL; WICPixelFormatGUID format=GUID_WICPixelFormat24bppBGR;
    GetClientRect(app.window,&r); dc=GetDC(app.window); mem=CreateCompatibleDC(dc); bitmap=CreateCompatibleBitmap(dc,r.right,r.bottom); old=SelectObject(mem,bitmap); FillRect(mem,&r,app.face); SendMessageW(app.window,WM_PRINT,(WPARAM)mem,PRF_CLIENT|PRF_ERASEBKGND); EnumChildWindows(app.window,print_child,(LPARAM)mem);
    assert(SUCCEEDED(CoCreateInstance(&CLSID_WICImagingFactory,NULL,CLSCTX_INPROC_SERVER,&IID_IWICImagingFactory,(void **)&factory)));
    assert(SUCCEEDED(IWICImagingFactory_CreateBitmapFromHBITMAP(factory,bitmap,NULL,WICBitmapIgnoreAlpha,&source)));
    assert(SUCCEEDED(IWICImagingFactory_CreateStream(factory,&stream))); assert(SUCCEEDED(IWICStream_InitializeFromFilename(stream,path,GENERIC_WRITE)));
    assert(SUCCEEDED(IWICImagingFactory_CreateEncoder(factory,&GUID_ContainerFormatPng,NULL,&encoder))); assert(SUCCEEDED(IWICBitmapEncoder_Initialize(encoder,(IStream *)stream,WICBitmapEncoderNoCache)));
    assert(SUCCEEDED(IWICBitmapEncoder_CreateNewFrame(encoder,&frame,NULL))); assert(SUCCEEDED(IWICBitmapFrameEncode_Initialize(frame,NULL))); assert(SUCCEEDED(IWICBitmapFrameEncode_SetSize(frame,(UINT)r.right,(UINT)r.bottom))); assert(SUCCEEDED(IWICBitmapFrameEncode_SetPixelFormat(frame,&format))); assert(SUCCEEDED(IWICBitmapFrameEncode_WriteSource(frame,(IWICBitmapSource *)source,NULL))); assert(SUCCEEDED(IWICBitmapFrameEncode_Commit(frame))); assert(SUCCEEDED(IWICBitmapEncoder_Commit(encoder)));
    IWICBitmapFrameEncode_Release(frame); IWICBitmapEncoder_Release(encoder); IWICStream_Release(stream); IWICBitmap_Release(source); IWICImagingFactory_Release(factory); SelectObject(mem,old); DeleteObject(bitmap); DeleteDC(mem); ReleaseDC(app.window,dc);
}
int main(int argc,char **argv) {
    Chat *first,*second; cJSON *event,*row; wchar_t dir[MAX_PATH]; char *draft;
    app.settings=cJSON_CreateObject(); GetTempPathW(MAX_PATH,dir); GetTempFileNameW(dir,L"lch",0,app.path); assert(create_window(GetModuleHandleW(NULL),SW_HIDE)); profile_load("",""); first=app.current;
    strcpy(first->id,"runtime-first"); strcpy(first->stored,"first"); strcpy(first->title,"Research a receiver"); first->loaded=1; first->running=1;
    newchat(); second=app.current; strcpy(second->title,"New conversation"); SetWindowTextW(app.prompt,L"A separate draft"); SendMessageW(app.window,WM_COMMAND,MAKEWPARAM(IdPrompt,EN_CHANGE),(LPARAM)app.prompt); assert(!strcmp(second->draft,"A separate draft"));
    event=cJSON_Parse("{\"session_id\":\"runtime-first\",\"seq\":1,\"type\":\"message.delta\",\"payload\":{\"text\":\"background reply\"}}"); incoming_event(event); cJSON_Delete(event); assert(app.current==second); assert(cJSON_GetArraySize(first->messages)==1); assert(cJSON_GetArraySize(second->messages)==0); assert(first->unread);
    show_server(); assert(app.serverwin); DestroyWindow(app.serverwin); assert(app.current==second); draft=gettext(app.prompt); assert(!strcmp(draft,"A separate draft")); free(draft);
    /* Returning from server/about never changes the selected chat or its draft. */
    app.current=first; first->running=0; strcpy(first->title,"A small Hermes workspace"); cJSON_Delete(first->messages); first->messages=cJSON_CreateArray();
    row=cJSON_CreateObject(); cJSON_AddStringToObject(row,"role","user"); cJSON_AddStringToObject(row,"text","Can you research this and leave a short report?"); cJSON_AddItemToArray(first->messages,row);
    row=cJSON_CreateObject(); cJSON_AddStringToObject(row,"role","assistant"); cJSON_AddStringToObject(row,"text","## Ready when you are\n\nI can browse, use tools, and keep the results in this chat.\n\n- Chats stay on your server.\n- Each conversation keeps its own draft.\n- You choose what to approve.\n\n`Ctrl+Enter` sends a message."); cJSON_AddItemToArray(first->messages,row);
    app.connected=0; sidebar(); render(); assert(IsWindowEnabled(app.send)==FALSE); SetWindowPos(app.window,NULL,0,0,1000,740,SWP_NOZORDER|SWP_NOACTIVATE); assert(app.current==first); app.dark=1; theme(); app.dark=0; theme();
    { RECT client,button; GetClientRect(app.window,&client); GetWindowRect(app.request,&button); MapWindowPoints(NULL,app.window,(POINT *)&button,2); assert(button.bottom<=client.bottom); }
    if(argc>1) { wchar_t *path=wide(argv[1]); screenshot(path); free(path); }
    assert(store_write(app.path,app.settings)); DeleteFileW(app.path); DestroyWindow(app.window); cleanup(); puts("native layout, separate drafts, background routing and server return: passed"); return 0;
}
