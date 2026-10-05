#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#include "store_win.h"
#include <wincrypt.h>
#include <shlobj.h>
#include <stdlib.h>
#include <stdio.h>
#include <wchar.h>
#include <string.h>
int store_path(wchar_t *out,size_t cap) {
    wchar_t *base=NULL; int ok;
    if(FAILED(SHGetKnownFolderPath(&FOLDERID_LocalAppData,0,NULL,&base))) return 0;
    ok=swprintf(out,cap,L"%ls\\LCB\\hermes",base)>0; CoTaskMemFree(base); if(!ok) return 0;
    SHCreateDirectoryExW(NULL,out,NULL); return wcslen(out)+13<cap && wcscat(out,L"\\private.bin")!=NULL;
}
cJSON *store_read(const wchar_t *path) {
    HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
    LARGE_INTEGER size; DATA_BLOB encrypted={0}, clear={0}; DWORD read; cJSON *json=NULL;
    if(file==INVALID_HANDLE_VALUE) { DWORD error=GetLastError(); return error==ERROR_FILE_NOT_FOUND||error==ERROR_PATH_NOT_FOUND?cJSON_CreateObject():NULL; }
    if(GetFileSizeEx(file,&size)&&size.QuadPart>0&&size.QuadPart<32*1024*1024) {
        encrypted.cbData=(DWORD)size.QuadPart; encrypted.pbData=malloc(encrypted.cbData);
        if(encrypted.pbData&&ReadFile(file,encrypted.pbData,encrypted.cbData,&read,NULL)&&read==encrypted.cbData && CryptUnprotectData(&encrypted,NULL,NULL,NULL,NULL,CRYPTPROTECT_UI_FORBIDDEN,&clear)) {
            json=cJSON_ParseWithLength((char *)clear.pbData,clear.cbData); SecureZeroMemory(clear.pbData,clear.cbData); LocalFree(clear.pbData);
        }
        free(encrypted.pbData);
    }
    CloseHandle(file); return json;
}
int store_write(const wchar_t *path,const cJSON *value) {
    char *text=cJSON_PrintUnformatted(value); DATA_BLOB clear={0}, encrypted={0}; HANDLE file; DWORD written; wchar_t temp[1024]; int ok=0;
    if(!text) return 0; clear.cbData=(DWORD)strlen(text)+1; clear.pbData=(BYTE *)text;
    if(clear.cbData>=32*1024*1024 || !CryptProtectData(&clear,L"lcb-hermes",NULL,NULL,NULL,CRYPTPROTECT_UI_FORBIDDEN,&encrypted)) goto done;
    if(swprintf(temp,1024,L"%ls.tmp",path)<0) goto done;
    file=CreateFileW(temp,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
    if(file==INVALID_HANDLE_VALUE) goto done;
    ok=WriteFile(file,encrypted.pbData,encrypted.cbData,&written,NULL)&&written==encrypted.cbData&&FlushFileBuffers(file); CloseHandle(file);
    if(ok) ok=MoveFileExW(temp,path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
done:
    if(encrypted.pbData) LocalFree(encrypted.pbData); SecureZeroMemory(text,strlen(text)); free(text); return ok;
}
