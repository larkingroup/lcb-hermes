#include "store.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void) {
    LinuxStore *s=linux_store_new(); char *error=NULL;
    cJSON *value=cJSON_Parse("{\"draft\":\"Temporary encrypted storage check\",\"cookie\":\"fixture\"}");
    int ok=linux_store_write(s,value,&error);
    if(!ok) { fprintf(stderr,"Secret Service unavailable: %s\n",error?error:""); g_free(error); cJSON_Delete(value); linux_store_free(s); return 77; }
    cJSON *restored=linux_store_read(s,&error);
    assert(restored && !error && cJSON_Compare(value,restored,1));
    cJSON_Delete(restored); cJSON_Delete(value); linux_store_free(s);
    puts("Secret Service key, encrypted file, and authenticated readback passed."); return 0;
}
