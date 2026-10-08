/* Real Athena widgets and async transport, connected only to the local fixture. */
#define main hermes_application_entry
#include "../src/main.c"
#undef main
#include <assert.h>
static int (*predicate)(void);
static gint64 deadline;
static gboolean check(gpointer context) {
    (void)context;
    if(predicate()) { app.run=0; return G_SOURCE_REMOVE; }
    assert(g_get_monotonic_time()<deadline);
    return G_SOURCE_CONTINUE;
}
static void until(int (*condition)(void)) {
    predicate=condition; deadline=g_get_monotonic_time()+10000000;
    g_timeout_add(20,check,NULL); app.run=1; loop();
}
static int connected(void) { return app.connected && !app.list_pending && app.count>=2; }
static int loaded(void) { return app.current && app.current->loaded && !app.current->pending; }
static int completed(void) { return app.current && !app.current->running && !app.current->pending && cJSON_GetArraySize(app.current->requests)>0; }
static int models_ready(void) { return app.models!=NULL; }
int main(int argc,char **argv) {
    assert(argc==2); char *url=g_strdup(argv[1]); argc=1;
    ui_init(&argc,argv); app.demo=1; app.settings=cJSON_CreateObject(); fresh();
    server_show(); set(app.url,url); set(app.user,"fixture"); set(app.password,"fixture"); set(app.servername,"Local fixture");
    XtVaSetValues(app.http,XtNstate,True,NULL); connect_selected(); until(connected);
    assert(!app.serverwin);
    assert(strstr(linux_net_cookie(app.net),"hermes_session=fixture"));
    Chat *saved_chat=find("saved"); assert(saved_chat);
    app.current=saved_chat; resume(saved_chat,0); until(loaded); render();
    set(app.prompt,"A retained draft / café / λ / 日本語"); capture_draft();
    fresh(); Chat *draft=app.current; set(app.prompt,"Second separate draft"); capture_draft();
    XawListReturnStruct row={0};
    for(int i=0;i<app.nrows;i++) if(app.rows[i]==saved_chat) row.list_index=i;
    selected(app.list,NULL,&row);
    char *value=get(app.prompt); assert(!strcmp(value,"A retained draft / café / λ / 日本語")); g_free(value);
    assert(!strcmp(draft->draft,"Second separate draft"));
    submit(); until(completed); render();
    value=get(app.prompt); assert(!*value); g_free(value);
    value=get(app.transcript); assert(strstr(value,"Hello from the local fixture.")); assert(strstr(value,"日本語")); g_free(value);
    request_show(); assert(app.questionwin); question_reply(NULL,(XtPointer)"deny",NULL); assert(!app.questionwin);
    assert(cJSON_GetArraySize(app.current->requests)==0);
    /* Background clarification survives a chat switch and returns the correct ID. */
    cJSON *request=cJSON_Parse("{\"jsonrpc\":\"2.0\",\"id\":\"clarify-fixture\",\"method\":\"clarify\",\"params\":{\"session_id\":\"runtime\",\"questions\":[{\"qid\":\"one\",\"question\":\"First?\"},{\"qid\":\"two\",\"question\":\"Second?\"}]}}");
    received(NULL,LinuxFrame,0,request,""); cJSON_Delete(request);
    request_show(); set(app.answer,"alpha"); question_reply(NULL,(XtPointer)"answer",NULL);
    assert(app.questionwin); set(app.answer,"beta"); question_reply(NULL,(XtPointer)"answer",NULL); assert(!app.questionwin);
    model_show(); until(models_ready); value=get(app.modelcatalog); assert(strstr(value,"fixture-model")); g_free(value);
    popup_close(NULL,&app.modelwin,NULL);
    /* New session resolution must merge its existing sidebar alias without losing another draft. */
    fresh(); set(app.prompt,"New session identity check"); capture_draft(); submit(); until(completed);
    int references=0; for(int i=0;i<app.count;i++) if(chat_matches(app.chats[i],"saved")) references++;
    assert(references==1); assert(find("saved")==app.current);
    request_show(); question_reply(NULL,(XtPointer)"deny",NULL);
    XtVaSetValues(app.shell,XtNwidth,720,XtNheight,470,NULL); XSync(XtDisplay(app.shell),False);
    while(XtAppPending(app.xt)) XtAppProcessEvent(app.xt,XtIMAll);
    Dimension width,height; XtVaGetValues(app.prompt,XtNwidth,&width,XtNheight,&height,NULL); assert(width>=100 && height>=28);
    linux_net_free(app.net); linux_store_free(app.store); clear_chats(); cJSON_Delete(app.settings); g_strfreev(app.labels); g_free(url);
    Display *display=XtDisplay(app.shell); XtDestroyWidget(app.shell); XFreeFontSet(display,app.fontset); XtDestroyApplicationContext(app.xt);
    puts("Athena UI: login, Unicode drafts, streaming, approvals, clarification, model catalog, chat aliases and resize passed.");
    return 0;
}
