#define _POSIX_C_SOURCE 200809L
#include "store.h"
#include <libsecret/secret.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/crypto.h>
#include <glib/gstdio.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

struct LinuxStore { char *path; unsigned char key[32]; int ready; };
static const SecretSchema schema={"org.lcb.hermes.linux",SECRET_SCHEMA_NONE,{{"purpose",SECRET_SCHEMA_ATTRIBUTE_STRING},{NULL,0}},0,NULL,NULL,NULL,NULL,NULL,NULL,NULL};
#define HEADER 36u
static const unsigned char magic[8]={'L','C','B','H','L','N','X',1};
GBytes *linux_store_seal(const unsigned char key[32],const void *data,size_t size) {
    if(size>WIRE_LIMIT) return NULL;
    unsigned char *out=g_malloc(size+HEADER+16); memcpy(out,magic,8);
    if(RAND_bytes(out+8,12)!=1) { g_free(out); return NULL; }
    EVP_CIPHER_CTX *ctx=EVP_CIPHER_CTX_new(); int len=0,last=0;
    int ok=ctx && EVP_EncryptInit_ex(ctx,EVP_aes_256_gcm(),NULL,key,out+8)==1 &&
        EVP_EncryptUpdate(ctx,NULL,&len,out,20)==1 &&
        EVP_EncryptUpdate(ctx,out+HEADER,&len,data,(int)size)==1 &&
        EVP_EncryptFinal_ex(ctx,out+HEADER+len,&last)==1 &&
        EVP_CIPHER_CTX_ctrl(ctx,EVP_CTRL_GCM_GET_TAG,16,out+20)==1;
    EVP_CIPHER_CTX_free(ctx);
    if(!ok) { g_free(out); return NULL; }
    return g_bytes_new_take(out,HEADER+(size_t)len+(size_t)last);
}
GBytes *linux_store_open(const unsigned char key[32],const void *data,size_t size) {
    const unsigned char *in=data;
    if(size<HEADER || size>WIRE_LIMIT+HEADER || memcmp(in,magic,8)) return NULL;
    unsigned char *out=g_malloc(size-HEADER+16); EVP_CIPHER_CTX *ctx=EVP_CIPHER_CTX_new(); int len=0,last=0;
    int ok=ctx && EVP_DecryptInit_ex(ctx,EVP_aes_256_gcm(),NULL,key,in+8)==1 &&
        EVP_DecryptUpdate(ctx,NULL,&len,in,20)==1 &&
        EVP_DecryptUpdate(ctx,out,&len,in+HEADER,(int)(size-HEADER))==1 &&
        EVP_CIPHER_CTX_ctrl(ctx,EVP_CTRL_GCM_SET_TAG,16,(void *)(in+20))==1 &&
        EVP_DecryptFinal_ex(ctx,out+len,&last)==1;
    EVP_CIPHER_CTX_free(ctx);
    if(!ok) { OPENSSL_cleanse(out,size-HEADER+16); g_free(out); return NULL; }
    return g_bytes_new_take(out,(size_t)len+(size_t)last);
}
LinuxStore *linux_store_new(void) {
    LinuxStore *s=g_new0(LinuxStore,1);
    s->path=g_build_filename(g_get_user_data_dir(),"lcb-hermes","private-linux.bin",NULL); return s;
}
static int key(LinuxStore *s,int create,char **error) {
    if(s->ready) return 1;
    GError *e=NULL;
    char *hex=secret_password_lookup_sync(&schema,NULL,&e,"purpose","storage-key",NULL);
    if(e) { *error=g_strdup(e->message); g_error_free(e); return 0; }
    if(!hex && create) {
        unsigned char raw[32]; char generated[65];
        if(RAND_bytes(raw,32)!=1) { *error=g_strdup("Could not create an encryption key."); return 0; }
        for(int i=0;i<32;i++) g_snprintf(generated+2*i,3,"%02x",raw[i]);
        OPENSSL_cleanse(raw,sizeof(raw));
        if(secret_password_store_sync(&schema,SECRET_COLLECTION_DEFAULT,"LCB Hermes Linux encrypted settings",generated,NULL,&e,"purpose","storage-key",NULL)) hex=g_strdup(generated);
        OPENSSL_cleanse(generated,sizeof(generated));
        if(e) { *error=g_strdup(e->message); g_error_free(e); return 0; }
    }
    if(!hex || strlen(hex)!=64) { if(hex) secret_password_free(hex); *error=g_strdup("Unlock a Secret Service wallet to save encrypted drafts and login cookies."); return 0; }
    for(int i=0;i<32;i++) {
        int a=g_ascii_xdigit_value(hex[2*i]),b=g_ascii_xdigit_value(hex[2*i+1]);
        if(a<0 || b<0) { secret_password_free(hex); *error=g_strdup("Invalid wallet encryption key."); return 0; }
        s->key[i]=(unsigned char)(a*16+b);
    }
    secret_password_free(hex); s->ready=1; return 1;
}
cJSON *linux_store_read(LinuxStore *s,char **error) {
    struct stat st;
    if(lstat(s->path,&st)<0) {
        if(errno==ENOENT) return cJSON_CreateObject();
        *error=g_strdup(g_strerror(errno)); return NULL;
    }
    if(!S_ISREG(st.st_mode) || st.st_uid!=getuid() || (st.st_mode&0077) || st.st_size<HEADER || st.st_size>WIRE_LIMIT+HEADER) {
        *error=g_strdup("Encrypted settings have invalid ownership, permissions or size."); return NULL;
    }
    if(!key(s,0,error)) return NULL;
    gchar *data=NULL; gsize size=0; GError *e=NULL;
    if(!g_file_get_contents(s->path,&data,&size,&e)) { *error=g_strdup(e->message); g_error_free(e); return NULL; }
    GBytes *clear=linux_store_open(s->key,data,size); g_free(data);
    if(!clear) { *error=g_strdup("Encrypted settings could not be authenticated. Existing file preserved."); return NULL; }
    const void *p=g_bytes_get_data(clear,&size); cJSON *value=cJSON_ParseWithLength(p,size);
    OPENSSL_cleanse((void *)p,size); g_bytes_unref(clear);
    if(!cJSON_IsObject(value)) { cJSON_Delete(value); *error=g_strdup("Invalid encrypted settings. Existing file preserved."); return NULL; }
    return value;
}
int linux_store_write(LinuxStore *s,const cJSON *value,char **error) {
    if(!key(s,1,error)) return 0;
    char *text=cJSON_PrintUnformatted(value); if(!text) return 0;
    GBytes *sealed=linux_store_seal(s->key,text,strlen(text)); OPENSSL_cleanse(text,strlen(text)); free(text);
    if(!sealed) { *error=g_strdup("Could not encrypt settings, or draft cache exceeds 8 MiB."); return 0; }
    char *dir=g_path_get_dirname(s->path); struct stat st;
    int ok=g_mkdir_with_parents(dir,0700)==0 && lstat(dir,&st)==0 && S_ISDIR(st.st_mode) && st.st_uid==getuid() && (st.st_mode&0077)==0;
    char *temp=g_strconcat(s->path,".XXXXXX",NULL); int fd=ok?g_mkstemp_full(temp,O_WRONLY|O_CLOEXEC,0600):-1;
    if(fd<0) ok=0;
    else {
        gsize size=0; const unsigned char *data=g_bytes_get_data(sealed,&size); size_t used=0;
        while(used<size) { ssize_t n=write(fd,data+used,size-used); if(n<0 && errno==EINTR) continue; if(n<=0) { ok=0; break; } used+=(size_t)n; }
        if(fsync(fd)<0) ok=0;
        if(close(fd)<0) ok=0;
        if(ok && g_rename(temp,s->path)<0) ok=0;
        if(ok) { int d=open(dir,O_RDONLY|O_DIRECTORY|O_CLOEXEC); if(d>=0) { (void)fsync(d); close(d); } }
        if(!ok) (void)g_unlink(temp);
    }
    if(!ok) *error=g_strdup("Could not atomically save encrypted settings.");
    g_free(temp); g_free(dir); g_bytes_unref(sealed); return ok;
}
void linux_store_free(LinuxStore *s) { if(s) { OPENSSL_cleanse(s->key,32); g_free(s->path); g_free(s); } }
