#include "transport.h"
#include <libsoup/soup.h>
#include <openssl/crypto.h>
#include <stdlib.h>
#include <string.h>

struct LinuxNet {
    SoupSession *session;
    SoupCookieJar *jar;
    SoupWebsocketConnection *socket;
    GCancellable *cancel;
    char *base, *origin, *host_header, *user, *password;
    LinuxReceive receive;
    void *context;
    unsigned generation, requests;
    int closing;
};
typedef struct {
    LinuxNet *net;
    SoupMessage *message;
    GInputStream *stream;
    GByteArray *bytes;
    unsigned generation;
    int stage, tag;
} Request;
static void http_request(LinuxNet *n, const char *method, const char *path, const char *body, int stage, int tag);
static void wipe(char **s) { if(*s) { OPENSSL_cleanse(*s,strlen(*s)); g_free(*s); *s=NULL; } }
static void emit(LinuxNet *n,int kind,int tag,const cJSON *j,const char *error) {
    if(!n->closing) n->receive(n->context,kind,tag,j,error?error:"");
}
int linux_private_address(GInetAddress *a) {
    const guint8 *p=g_inet_address_to_bytes(a);
    if(g_inet_address_get_is_loopback(a)) return 1;
    if(g_inet_address_get_family(a)==G_SOCKET_FAMILY_IPV4)
        return p[0]==10 || (p[0]==172 && p[1]>=16 && p[1]<=31) ||
            (p[0]==192 && p[1]==168) || (p[0]==169 && p[1]==254) ||
            (p[0]==100 && p[1]>=64 && p[1]<=127);
    if((p[0]&0xfe)==0xfc || (p[0]==0xfe && (p[1]&0xc0)==0x80)) return 1;
    static const guint8 mapped[12]={0,0,0,0,0,0,0,0,0,0,255,255};
    if(!memcmp(p,mapped,12)) {
        GInetAddress *v4=g_inet_address_new_from_bytes(p+12,G_SOCKET_FAMILY_IPV4);
        int ok=linux_private_address(v4); g_object_unref(v4); return ok;
    }
    return 0;
}
static void request_free(Request *r) {
    r->net->requests--;
    g_clear_object(&r->message); g_clear_object(&r->stream);
    if(r->bytes) { OPENSSL_cleanse(r->bytes->data,r->bytes->len); g_byte_array_unref(r->bytes); }
    g_free(r);
}
static int current(Request *r) { return !r->net->closing && r->generation==r->net->generation; }
static void ws_frame(void *context,cJSON *frame) {
    emit(context,LinuxFrame,0,frame,""); cJSON_Delete(frame);
}
static void ws_message(SoupWebsocketConnection *socket,int type,GBytes *message,gpointer context) {
    LinuxNet *n=context; gsize size; const char *data=g_bytes_get_data(message,&size);
    if(socket!=n->socket) return;
    if(type!=SOUP_WEBSOCKET_DATA_TEXT || frames_parse(data,size,ws_frame,n)<0) {
        emit(n,LinuxOffline,0,NULL,"Invalid or oversized Hermes frame.");
        soup_websocket_connection_close(socket,1002,"Invalid frame");
    }
}
static void ws_closed(SoupWebsocketConnection *socket,gpointer context) {
    LinuxNet *n=context;
    if(socket==n->socket) emit(n,LinuxOffline,0,NULL,"Disconnected. Reconnect to resume; server tasks keep running.");
}
static void ws_open(GObject *source,GAsyncResult *result,gpointer context) {
    Request *r=context; LinuxNet *n=r->net; GError *error=NULL;
    SoupWebsocketConnection *socket=soup_session_websocket_connect_finish(SOUP_SESSION(source),result,&error);
    if(current(r)) {
        if(socket) {
            n->socket=socket; socket=NULL;
            soup_websocket_connection_set_max_incoming_payload_size(n->socket,WIRE_LIMIT);
            soup_websocket_connection_set_keepalive_interval(n->socket,20);
            g_signal_connect(n->socket,"message",G_CALLBACK(ws_message),n);
            g_signal_connect(n->socket,"closed",G_CALLBACK(ws_closed),n);
            emit(n,LinuxOnline,0,NULL,"");
        } else emit(n,LinuxOffline,0,NULL,error?error->message:"WebSocket upgrade failed.");
    }
    if(socket) { soup_websocket_connection_close(socket,1000,NULL); g_object_unref(socket); }
    g_clear_error(&error); request_free(r);
}
static SoupMessage *message_new(LinuxNet *n,const char *method,const char *path) {
    char *url=g_strconcat(n->base,path,NULL);
    SoupMessage *m=soup_message_new(method,url); g_free(url);
    if(m) {
        soup_message_add_flags(m,SOUP_MESSAGE_NO_REDIRECT);
        soup_message_headers_replace(soup_message_get_request_headers(m),"Origin",n->origin);
        if(n->host_header) soup_message_headers_replace(soup_message_get_request_headers(m),"Host",n->host_header);
        /* Cookies are scoped to this connection; the jar handles path and expiry. */
    }
    return m;
}
static void response(Request *r,const char *error) {
    LinuxNet *n=r->net; cJSON *j=NULL;
    if(!current(r)) { request_free(r); return; }
    guint status=r->message?soup_message_get_status(r->message):0;
    if(!error && status>=200 && status<300) j=cJSON_ParseWithLength((const char *)r->bytes->data,r->bytes->len);
    const char *failure=error?error:(status==401||status==403)?"Sign in again. Check your server login.":"Invalid Hermes HTTP response.";
    if(!j) emit(n,r->stage?LinuxOffline:LinuxRest,r->tag,NULL,failure);
    else if(r->stage==1) {
        if(!*js(j,"version")) emit(n,LinuxOffline,0,NULL,"This endpoint is not a Hermes server.");
        else if(n->password && *n->password) {
            cJSON *login=cJSON_CreateObject(); cJSON_AddStringToObject(login,"provider","basic");
            cJSON_AddStringToObject(login,"username",n->user); cJSON_AddStringToObject(login,"password",n->password);
            char *body=cJSON_PrintUnformatted(login);
            http_request(n,"POST","/auth/password-login",body,2,0);
            OPENSSL_cleanse(body,strlen(body)); free(body);
            cJSON *p=cJSON_GetObjectItemCaseSensitive(login,"password"); OPENSSL_cleanse(p->valuestring,strlen(p->valuestring));
            cJSON_Delete(login); wipe(&n->password);
        } else http_request(n,"POST","/api/auth/ws-ticket","{}",3,0);
    } else if(r->stage==2) http_request(n,"POST","/api/auth/ws-ticket","{}",3,0);
    else if(r->stage==3) {
        const char *ticket=js(j,"ticket");
        if(!*ticket || strpbrk(ticket,"?#&\r\n ")) emit(n,LinuxOffline,0,NULL,"Invalid connection ticket.");
        else {
            char *path=g_strconcat("/api/ws?ticket=",ticket,NULL);
            Request *ws=g_new0(Request,1); ws->net=n; ws->generation=n->generation;
            ws->message=message_new(n,"GET",path); g_free(path); n->requests++;
            soup_session_websocket_connect_async(n->session,ws->message,n->origin,NULL,G_PRIORITY_DEFAULT,n->cancel,ws_open,ws);
        }
    } else emit(n,LinuxRest,r->tag,j,"");
    cJSON_Delete(j); request_free(r);
}
static void read_chunk(GObject *source,GAsyncResult *result,gpointer context) {
    Request *r=context; GError *error=NULL; GBytes *bytes=g_input_stream_read_bytes_finish(G_INPUT_STREAM(source),result,&error);
    if(!current(r)) { g_clear_pointer(&bytes,g_bytes_unref); g_clear_error(&error); request_free(r); return; }
    if(!bytes) { response(r,error?error->message:"HTTP read failed."); g_clear_error(&error); return; }
    gsize size; const guint8 *data=g_bytes_get_data(bytes,&size);
    if(r->bytes->len+size>WIRE_LIMIT) { g_bytes_unref(bytes); response(r,"Server response is too large."); return; }
    if(size) g_byte_array_append(r->bytes,data,(guint)size);
    g_bytes_unref(bytes);
    if(!size) response(r,NULL);
    else g_input_stream_read_bytes_async(r->stream,16384,G_PRIORITY_DEFAULT,r->net->cancel,read_chunk,r);
}
static void http_open(GObject *source,GAsyncResult *result,gpointer context) {
    Request *r=context; GError *error=NULL;
    r->stream=soup_session_send_finish(SOUP_SESSION(source),result,&error);
    if(!current(r)) request_free(r);
    else if(!r->stream) response(r,error?error->message:"HTTP request failed.");
    else g_input_stream_read_bytes_async(r->stream,16384,G_PRIORITY_DEFAULT,r->net->cancel,read_chunk,r);
    g_clear_error(&error);
}
static void secure_bytes_free(gpointer data) { char *s=data; OPENSSL_cleanse(s,strlen(s)); g_free(s); }
static void http_request(LinuxNet *n,const char *method,const char *path,const char *body,int stage,int tag) {
    Request *r=g_new0(Request,1); r->net=n; r->generation=n->generation; r->stage=stage; r->tag=tag;
    r->message=message_new(n,method,path); r->bytes=g_byte_array_new(); n->requests++;
    if(!r->message) { response(r,"Invalid request URL."); return; }
    if(body) {
        char *copy=g_strdup(body); GBytes *bytes=g_bytes_new_with_free_func(copy,strlen(copy),secure_bytes_free,copy);
        soup_message_set_request_body_from_bytes(r->message,"application/json",bytes); g_bytes_unref(bytes);
    }
    soup_session_send_async(n->session,r->message,G_PRIORITY_DEFAULT,n->cancel,http_open,r);
}
static void disconnect(LinuxNet *n) {
    n->generation++;
    if(n->cancel) g_cancellable_cancel(n->cancel);
    if(n->socket) {
        g_signal_handlers_disconnect_by_data(n->socket,n);
        soup_websocket_connection_close(n->socket,1000,NULL); g_clear_object(&n->socket);
    }
    if(n->session) soup_session_abort(n->session);
    g_clear_object(&n->session); g_clear_object(&n->jar); g_clear_object(&n->cancel);
    g_clear_pointer(&n->base,g_free); g_clear_pointer(&n->origin,g_free); g_clear_pointer(&n->host_header,g_free);
    wipe(&n->password); g_clear_pointer(&n->user,g_free);
}
LinuxNet *linux_net_new(LinuxReceive receive,void *context) {
    LinuxNet *n=g_new0(LinuxNet,1); n->receive=receive; n->context=context; return n;
}
void linux_net_connect(LinuxNet *n,const char *url,const char *user,const char *password,int allow,const char *cookie) {
    disconnect(n); GError *error=NULL; GUri *uri=g_uri_parse(url,G_URI_FLAGS_NONE,&error);
    const char *scheme=uri?g_uri_get_scheme(uri):NULL, *host=uri?g_uri_get_host(uri):NULL;
    const char *path=uri?g_uri_get_path(uri):NULL;
    if(!uri || !scheme || !host || !*host || (strcmp(scheme,"http") && strcmp(scheme,"https")) ||
        g_uri_get_userinfo(uri) || g_uri_get_query(uri) || g_uri_get_fragment(uri) || (path && *path && strcmp(path,"/"))) {
        emit(n,LinuxOffline,0,NULL,"Use a root HTTP(S) server URL without credentials, query or fragment."); goto done;
    }
    n->origin=g_strdup(url); if(g_str_has_suffix(n->origin,"/")) n->origin[strlen(n->origin)-1]=0;
    n->base=g_strdup(n->origin);
    if(!strcmp(scheme,"http")) {
        if(!allow) { emit(n,LinuxOffline,0,NULL,"Enable private HTTP only for a trusted private server."); goto done; }
        GResolver *resolver=g_resolver_get_default(); GList *addresses=g_resolver_lookup_by_name(resolver,host,NULL,&error); g_object_unref(resolver);
        int ok=addresses!=NULL;
        for(GList *a=addresses;a;a=a->next) if(!linux_private_address(a->data)) ok=0;
        if(!ok) { g_resolver_free_addresses(addresses); emit(n,LinuxOffline,0,NULL,"HTTP requires exclusively private or loopback addresses."); goto done; }
        char *ip=g_inet_address_to_string(addresses->data); int port=g_uri_get_port(uri); if(port<0) port=80;
        g_free(n->base); n->base=g_strdup_printf(strchr(ip,':')?"http://[%s]:%d":"http://%s:%d",ip,port);
        n->host_header=g_strdup_printf(strchr(host,':')?"[%s]:%d":"%s:%d",host,port);
        g_free(ip); g_resolver_free_addresses(addresses);
    }
    n->user=g_strdup(user); n->password=g_strdup(password);
    n->cancel=g_cancellable_new(); n->session=soup_session_new_with_options("timeout",30,"user-agent","lcb-hermes/linux-classic",NULL);
    n->jar=soup_cookie_jar_new(); soup_session_add_feature(n->session,SOUP_SESSION_FEATURE(n->jar));
    if(cookie && *cookie && !strpbrk(cookie,"\r\n")) {
        /* Reconstruct only request cookie pairs; do not interpret saved attributes. */
        char **pairs=g_strsplit(cookie,";",-1);
        for(char **p=pairs;*p;p++) {
            char *eq=strchr(*p,'=');
            if(eq) {
                *eq=0; GUri *base=g_uri_parse(n->base,G_URI_FLAGS_NONE,NULL);
                SoupCookie *c=soup_cookie_new(g_strstrip(*p),eq+1,g_uri_get_host(base),"/",-1);
                soup_cookie_set_secure(c,!strcmp(scheme,"https"));
                soup_cookie_jar_add_cookie(n->jar,c); g_uri_unref(base);
            }
        }
        g_strfreev(pairs);
    }
    http_request(n,"GET","/api/status",NULL,1,0);
done:
    g_clear_error(&error); if(uri) g_uri_unref(uri);
}
int linux_net_send(LinuxNet *n,const cJSON *frame) {
    if(!n->socket || soup_websocket_connection_get_state(n->socket)!=SOUP_WEBSOCKET_STATE_OPEN) return 0;
    char *wire=cJSON_PrintUnformatted(frame); if(!wire) return 0;
    soup_websocket_connection_send_text(n->socket,wire); OPENSSL_cleanse(wire,strlen(wire)); free(wire); return 1;
}
void linux_net_rest(LinuxNet *n,const char *path,int tag) { if(n->session) http_request(n,"GET",path,NULL,0,tag); }
const char *linux_net_cookie(LinuxNet *n) {
    static char output[8192]; output[0]=0;
    if(!n->jar || !n->base) return output;
    char *root=g_strconcat(n->base,"/",NULL);
    GUri *uri=g_uri_parse(root,G_URI_FLAGS_NONE,NULL); g_free(root);
    char *cookie=soup_cookie_jar_get_cookies(n->jar,uri,TRUE);
    g_strlcpy(output,cookie?cookie:"",sizeof(output)); g_free(cookie); g_uri_unref(uri); return output;
}
void linux_net_free(LinuxNet *n) {
    if(!n) return;
    n->closing=1; disconnect(n);
    while(n->requests) g_main_context_iteration(NULL,TRUE);
    g_free(n);
}
