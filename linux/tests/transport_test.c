#include "transport.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static LinuxNet *net;
static int online,done,stage,denied,rest;
static Chat *chat;
static void send(int id,const char *method,const char *params) {
    cJSON *f=cJSON_CreateObject(); cJSON_AddStringToObject(f,"jsonrpc","2.0"); cJSON_AddNumberToObject(f,"id",id);
    cJSON_AddStringToObject(f,"method",method); cJSON_AddItemToObject(f,"params",cJSON_Parse(params));
    assert(linux_net_send(net,f)); cJSON_Delete(f);
}
static void receive(void *context,int kind,int tag,const cJSON *j,const char *error) {
    (void)context; (void)tag;
    if(kind==LinuxOffline) { if(stage==0) { denied++; return; } fprintf(stderr,"Unexpected offline: %s\n",error); assert(0); }
    if(kind==LinuxOnline) {
        online++;
        assert(strstr(linux_net_cookie(net),"hermes_session=fixture"));
        send(1,"session.resume","{\"session_id\":\"saved\",\"close_on_disconnect\":false}");
        linux_net_rest(net,"/api/system/stats",99);
    }
    if(kind==LinuxRest) { assert(j && !*error); rest++; }
    if(kind==LinuxFrame) {
        const cJSON *id=cJSON_GetObjectItemCaseSensitive(j,"id"),*p=cJSON_GetObjectItemCaseSensitive(j,"params");
        if(cJSON_IsNumber(id) && id->valueint==1) {
            chat_load(chat,cJSON_GetObjectItemCaseSensitive(j,"result"));
            assert(chat_matches(chat,"runtime") && chat_matches(chat,"saved"));
            send(2,"prompt.submit","{\"session_id\":\"runtime\",\"text\":\"Local transport regression\",\"surface\":\"desktop\"}");
        } else if(!strcmp(js(j,"method"),"event")) {
            chat_event(chat,p);
            if(!strcmp(js(p,"type"),"message.complete")) {
                assert(!strcmp(js(cJSON_GetArrayItem(chat->messages,cJSON_GetArraySize(chat->messages)-1),"text"),"Hello from the local fixture. café / λ / 日本語"));
                done=1;
            }
        }
    }
}
int main(int argc,char **argv) {
    assert(argc==2); net=linux_net_new(receive,NULL); chat=chat_new("fixture");
    const char *private[]={"127.0.0.1","10.0.0.1","172.16.1.1","192.168.1.1","100.64.1.1","::1","fd00::1","fe80::1","::ffff:127.0.0.1"};
    const char *public[]={"8.8.8.8","172.32.1.1","100.128.1.1","2001:4860:4860::8888","::ffff:8.8.8.8"};
    for(size_t i=0;i<sizeof(private)/sizeof(*private);i++) { GInetAddress *a=g_inet_address_new_from_string(private[i]); assert(linux_private_address(a)); g_object_unref(a); }
    for(size_t i=0;i<sizeof(public)/sizeof(*public);i++) { GInetAddress *a=g_inet_address_new_from_string(public[i]); assert(!linux_private_address(a)); g_object_unref(a); }
    linux_net_connect(net,argv[1],"fixture","fixture",0,""); assert(denied==1);
    linux_net_connect(net,"http://user:password@127.0.0.1","fixture","fixture",1,""); assert(denied==2);
    linux_net_connect(net,"http://127.0.0.1/path","fixture","fixture",1,""); assert(denied==3);
    stage=1; linux_net_connect(net,argv[1],"fixture","fixture",1,"");
    gint64 until=g_get_monotonic_time()+15000000;
    while((!done || !rest) && g_get_monotonic_time()<until) g_main_context_iteration(NULL,FALSE);
    assert(done && online==1 && rest==1);
    /* Reconnect with the saved cookie and a superseded in-flight login. */
    char *cookie=g_strdup(linux_net_cookie(net)); done=0;
    linux_net_connect(net,argv[1],"fixture","wrong",1,"");
    linux_net_connect(net,argv[1],"fixture","",1,cookie); g_free(cookie);
    until=g_get_monotonic_time()+15000000;
    while((!done || rest<2) && g_get_monotonic_time()<until) g_main_context_iteration(NULL,FALSE);
    assert(done && online==2 && rest==2);
    linux_net_free(net); chat_free(chat);
    puts("Transport: private HTTP guards, password login, cookie reuse, WebSocket streaming, REST, stale connection cancellation passed.");
    return 0;
}
