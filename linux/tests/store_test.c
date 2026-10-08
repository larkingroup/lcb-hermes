#include "store.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
int main(void) {
    unsigned char key[32]={1},other[32]={2};
    const char text[]="{\"draft\":\"café / λ / 日本語\",\"cookie\":\"secret\"}";
    GBytes *a=linux_store_seal(key,text,sizeof(text));
    GBytes *b=linux_store_seal(key,text,sizeof(text)); assert(a && b);
    gsize n; const unsigned char *wire=g_bytes_get_data(a,&n);
    assert(!g_bytes_equal(a,b));
    GBytes *clear=linux_store_open(key,wire,n); assert(clear);
    gsize size; const void *p=g_bytes_get_data(clear,&size);
    assert(size==sizeof(text) && !memcmp(p,text,size)); g_bytes_unref(clear);
    assert(!linux_store_open(other,wire,n));
    unsigned char *changed=g_memdup2(wire,n);
    changed[n-1]^=1; assert(!linux_store_open(key,changed,n)); changed[n-1]^=1;
    changed[8]^=1; assert(!linux_store_open(key,changed,n));
    assert(!linux_store_open(key,wire,20));
    assert(!linux_store_seal(key,text,WIRE_LIMIT+1));
    g_free(changed); g_bytes_unref(a); g_bytes_unref(b);
    puts("Encrypted store: random nonce, UTF-8 roundtrip, wrong-key/tamper/truncation rejection passed.");
    return 0;
}
