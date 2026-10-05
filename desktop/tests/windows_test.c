#define UNICODE
#define _UNICODE
#include "transport_win.h"
#include "store_win.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void) {
    char error[256]; wchar_t dir[MAX_PATH],path[MAX_PATH]; cJSON *value,*loaded; HANDLE file,lock; DWORD size,read; char *bytes;
    assert(endpoint_valid("http://192.168.0.50:7777",1,error,sizeof(error)));
    assert(endpoint_valid("http://100.100.100.100:7777",1,error,sizeof(error)));
    assert(endpoint_valid("http://[::1]:7777",1,error,sizeof(error)));
    assert(!endpoint_valid("http://192.168.0.50:7777",0,error,sizeof(error)));
    assert(!endpoint_valid("http://8.8.8.8:7777",1,error,sizeof(error)));
    assert(!endpoint_valid("http://100.128.0.1:7777",1,error,sizeof(error)));
    assert(!endpoint_valid("https://user:pass@example.com",1,error,sizeof(error)));
    assert(!endpoint_valid("https://example.com/chat",1,error,sizeof(error)));
    assert(!endpoint_valid("https://example.com?secret=x",1,error,sizeof(error)));
    assert(endpoint_valid("https://example.com:7777/",0,error,sizeof(error)));
    GetTempPathW(MAX_PATH,dir); GetTempFileNameW(dir,L"lcb",0,path);
    value=cJSON_CreateObject(); cJSON_AddStringToObject(value,"cookie","synthetic-private-cookie"); cJSON_AddStringToObject(value,"draft","draft 🌿"); assert(store_write(path,value)); loaded=store_read(path); assert(cJSON_Compare(value,loaded,1)); cJSON_Delete(loaded);
    file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL); assert(file!=INVALID_HANDLE_VALUE); size=GetFileSize(file,NULL); bytes=calloc(size+1,1); assert(ReadFile(file,bytes,size,&read,NULL)); assert(!strstr(bytes,"synthetic-private-cookie")); free(bytes); CloseHandle(file);
    lock=CreateFileW(path,GENERIC_READ,0,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL); assert(lock!=INVALID_HANDLE_VALUE); cJSON_AddNumberToObject(value,"new",1); assert(!store_write(path,value)); CloseHandle(lock); loaded=store_read(path); assert(!cJSON_GetObjectItemCaseSensitive(loaded,"new")); cJSON_Delete(loaded); cJSON_Delete(value);
    DeleteFileW(path); wcscat(path,L".tmp"); DeleteFileW(path); puts("private endpoints, DPAPI and atomic save failure: passed"); return 0;
}
