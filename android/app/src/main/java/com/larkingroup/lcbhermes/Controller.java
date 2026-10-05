package com.larkingroup.lcbhermes;

import static com.larkingroup.lcbhermes.Protocol.obj;

import android.content.*;
import android.os.*;
import java.util.*;
import java.util.concurrent.*;
import org.json.*;

final class Controller implements Gateway.Listener {
  interface Observer {
    void changed(boolean structure);

    void request(JSONObject r);
  }

  final Context context;
  final Handler main = new Handler(Looper.getMainLooper());
  final ExecutorService disk = Executors.newSingleThreadExecutor();
  final SecureStore store;
  final List<JSONObject> rows = new ArrayList<>();
  final LinkedHashMap<String, JSONObject> tools = new LinkedHashMap<>();
  final LinkedHashMap<String, JSONObject> requests = new LinkedHashMap<>();
  JSONObject data = obj("profiles", new JSONArray(), "theme", "light");
  JSONArray sessions = new JSONArray();
  JSONArray attachments = new JSONArray();
  int uploadsPending;
  Observer observer;
  Gateway gateway;
  String profileId = "",
      sessionId = "",
      storedId = "",
      title = "new chat",
      model = "",
      provider = "",
      draft = "",
      status = "offline";
  String newModel = "", newProvider = "", notice = "";
  boolean running, connecting, loaded, demo, submissionPending;
  private JSONObject stream;
  private int reconnects;
  private boolean reconnectScheduled, renderScheduled;
  private final Map<String, Long> sequences = new HashMap<>();
  private String epoch = "";
  private int sessionGeneration;
  private final LinkedHashMap<String, WatchedChat> watched = new LinkedHashMap<>();
  private boolean listPending;

  private static final class WatchedChat {
    String runtime, stored, title, reply = "";
    boolean running;
    final LinkedHashMap<String, JSONObject> requests = new LinkedHashMap<>();

    WatchedChat(String runtime, String stored, String title, boolean running) {
      this.runtime = runtime;
      this.stored = stored;
      this.title = title;
      this.running = running;
    }
  }

  private void leaveChat() {
    save();
    if (!storedId.isEmpty() && (running || !requests.isEmpty())) {
      WatchedChat chat = new WatchedChat(sessionId, storedId, title, running);
      chat.requests.putAll(requests);
      if (stream != null) chat.reply = Protocol.text(stream);
      watched.put(storedId, chat);
    }
  }

  private WatchedChat watchedChat(String id) {
    if (id.isEmpty()) return null;
    for (WatchedChat chat : watched.values())
      if (id.equals(chat.runtime) || id.equals(chat.stored)) return chat;
    return null;
  }

  String chatBadge(String id) {
    if (id.equals(storedId) && running) return "working";
    if (id.equals(storedId) && !requests.isEmpty()) return "answer needed";
    WatchedChat chat = watchedChat(id);
    if (chat != null && !chat.requests.isEmpty()) return "answer needed";
    if (chat != null && chat.running) return "working";
    JSONObject p = profile();
    JSONObject unread = p == null ? null : p.optJSONObject("unread");
    return unread != null && unread.optBoolean(id) ? "reply" : "";
  }

  JSONArray sidebarSessions() {
    JSONArray all = new JSONArray();
    Set<String> seen = new HashSet<>();
    if (sessions != null)
      for (int i = 0; i < sessions.length(); i++) {
        JSONObject session = sessions.optJSONObject(i);
        if (session == null || !seen.add(session.optString("id"))) continue;
        WatchedChat chat = watchedChat(session.optString("id"));
        all.put(
            chat == null
                ? session
                : obj(
                    "id",
                    chat.stored,
                    "title",
                    chat.title,
                    "preview",
                    session.optString("preview")));
      }
    for (WatchedChat chat : watched.values())
      if (seen.add(chat.stored)) all.put(obj("id", chat.stored, "title", chat.title));
    return all;
  }

  void removedChat(String id) {
    watched.remove(id);
    markRead(id, false);
    if (id.equals(storedId)) {
      draft = "";
      attachments = new JSONArray();
      newChat();
    }
    ChatDrafts.remove(profile(), id);
    save();
    changed(false);
  }

  String sidebarBadges() {
    StringBuilder value = new StringBuilder(chatBadge(storedId));
    for (WatchedChat chat : watched.values())
      value.append(chat.stored).append(chat.title).append(chatBadge(chat.stored));
    JSONObject p = profile();
    if (p != null) value.append(p.optJSONObject("unread"));
    return value.toString();
  }

  private void markRead(String id, boolean unread) {
    JSONObject p = profile();
    if (p == null || id.isEmpty()) return;
    try {
      JSONObject marks = p.optJSONObject("unread");
      if (marks == null) {
        marks = obj();
        p.put("unread", marks);
      }
      if (unread) marks.put(id, true);
      else marks.remove(id);
    } catch (Exception ignored) {
    }
  }

  boolean anyRunning() {
    if (running) return true;
    for (WatchedChat chat : watched.values()) if (chat.running) return true;
    return false;
  }

  String workingSession() {
    if (running) return storedId;
    for (WatchedChat chat : watched.values()) if (chat.running) return chat.stored;
    return "";
  }

  private void restoreDraft(String id) {
    JSONObject value = ChatDrafts.read(profile(), id);
    draft = value.optString("text");
    attachments = value.optJSONArray("attachments");
    if (attachments == null) attachments = new JSONArray();
  }

  Controller(Context c) {
    context = c;
    store = new SecureStore(c);
    disk.execute(
        () -> {
          JSONObject loadedData;
          String error = "";
          try {
            loadedData = store.read();
          } catch (Exception e) {
            loadedData = data;
            error = "Local data could not be unlocked. Sign in to continue.";
          }
          JSONObject ready = loadedData;
          String message = error;
          main.post(
              () -> {
                data = ready;
                notice = message;
                loaded = true;
                profileId = data.optString("active");
                restore();
                changed(true);
                if (profile() != null
                    && profile().optJSONArray("cookies") != null
                    && profile().optJSONArray("cookies").length() > 0) connectSaved();
              });
        });
  }

  JSONArray profiles() {
    JSONArray a = data.optJSONArray("profiles");
    if (a == null) {
      a = new JSONArray();
      try {
        data.put("profiles", a);
      } catch (Exception ignored) {
      }
    }
    return a;
  }

  JSONObject profile() {
    for (int i = 0; i < profiles().length(); i++) {
      JSONObject p = profiles().optJSONObject(i);
      if (p != null && p.optString("id").equals(profileId)) return p;
    }
    return null;
  }

  void attach(Observer o) {
    observer = o;
    changed(true);
  }

  void detach(Observer o) {
    if (observer == o) observer = null;
  }

  void changed(boolean structure) {
    if (observer != null) observer.changed(structure);
  }

  void renderSoon() {
    if (renderScheduled) return;
    renderScheduled = true;
    main.postDelayed(
        () -> {
          renderScheduled = false;
          changed(false);
        },
        80);
  }

  void restore() {
    rows.clear();
    tools.clear();
    requests.clear();
    stream = null;
    running = false;
    sessionId = "";
    attachments = new JSONArray();
    JSONObject p = profile();
    watched.clear();
    if (p == null) return;
    JSONArray background = p.optJSONArray("watched_chats");
    if (background != null)
      for (int i = 0; i < background.length(); i++) {
        JSONObject chat = background.optJSONObject(i);
        if (chat != null && !chat.optString("stored").isEmpty())
          watched.put(
              chat.optString("stored"),
              new WatchedChat(
                  chat.optString("runtime"),
                  chat.optString("stored"),
                  chat.optString("title", "untitled"),
                  true));
      }
    storedId = p.optString("session");
    restoreDraft(storedId);
    title = p.optString("title", "new chat");
    JSONArray a = p.optJSONArray("cache");
    if (a != null)
      for (int i = 0; i < a.length(); i++)
        if (a.optJSONObject(i) != null) rows.add(a.optJSONObject(i));
  }

  void save() {
    JSONObject p = profile();
    if (p != null) {
      ChatDrafts.write(p, storedId, draft, attachments);
      try {
        p.put("session", storedId)
            .put("draft", draft)
            .put("title", title)
            .put("draft_attachments", attachments);
        JSONArray cache = new JSONArray();
        for (int i = Math.max(0, rows.size() - 120); i < rows.size(); i++) {
          JSONObject row = rows.get(i);
          String t = Protocol.text(row);
          if (t.length() > 100000) t = t.substring(0, 100000);
          JSONObject cached = obj("role", row.optString("role"), "text", t);
          if (row.has("attachments")) cached.put("attachments", row.optJSONArray("attachments"));
          if (row.has("display_text")) cached.put("display_text", row.optString("display_text"));
          cache.put(cached);
        }
        p.put("cache", cache);
        JSONArray background = new JSONArray();
        for (WatchedChat chat : watched.values())
          if (chat.running || !chat.requests.isEmpty())
            background.put(
                obj("runtime", chat.runtime, "stored", chat.stored, "title", chat.title));
        p.put("watched_chats", background);
        if (gateway != null) p.put("cookies", gateway.savedCookies());
      } catch (Exception ignored) {
      }
    }
    String snapshot = data.toString();
    disk.execute(
        () -> {
          try {
            store.write(new JSONObject(snapshot));
          } catch (Exception e) {
            main.post(
                () -> {
                  notice = "Could not save local data. Keep this chat open.";
                  changed(false);
                });
          }
        });
  }

  void setDraft(String value) {
    draft = value;
    main.removeCallbacks(saveDraft);
    main.postDelayed(saveDraft, 500);
  }

  void addAttachment(JSONObject value) {
    if (attachments.length() >= 6) {
      notice = "Send these attachments before adding more.";
      changed(false);
      return;
    }
    attachments.put(value);
    notice = "";
    save();
    changed(true);
  }

  void removeAttachment(String id) {
    if (submissionPending || running) return;
    JSONArray keep = new JSONArray();
    for (int i = 0; i < attachments.length(); i++) {
      JSONObject a = attachments.optJSONObject(i);
      if (a != null && !a.optString("id").equals(id)) keep.put(a);
    }
    attachments = keep;
    save();
    changed(true);
  }

  private final Runnable saveDraft = this::save;

  void login(
      String name,
      String url,
      String username,
      String password,
      boolean http,
      Gateway.Result callback) {
    if (anyRunning() || submissionPending || uploadsPending > 0) {
      callback.done(null, "Let the current task finish before changing servers.");
      return;
    }
    okhttp3.HttpUrl base;
    try {
      base = Endpoint.parse(url, http);
    } catch (Exception e) {
      callback.done(null, e.getMessage());
      return;
    }
    save();
    close();
    demo = false;
    status = "signing in";
    connecting = true;
    changed(true);
    int generation = sessionGeneration;
    disk.execute(
        () -> {
          try {
            okhttp3.HttpUrl resolved = Endpoint.resolve(base, http);
            main.post(
                () -> {
                  if (generation == sessionGeneration && connecting)
                    loginAt(name, resolved, username, password, http, callback);
                });
          } catch (Exception error) {
            main.post(
                () -> {
                  if (generation != sessionGeneration || !connecting) return;
                  connecting = false;
                  status = "offline";
                  callback.done(null, error.getMessage());
                  changed(true);
                });
          }
        });
  }

  private void loginAt(
      String name,
      okhttp3.HttpUrl base,
      String username,
      String password,
      boolean http,
      Gateway.Result callback) {
    String id = UUID.randomUUID().toString();
    JSONObject p =
        obj(
            "id",
            id,
            "name",
            name.trim().isEmpty() ? base.host() : name.trim(),
            "url",
            base.toString(),
            "username",
            username,
            "http",
            http,
            "cookies",
            new JSONArray());
    int oldIndex = -1;
    for (int i = 0; i < profiles().length(); i++) {
      JSONObject old = profiles().optJSONObject(i);
      if (old != null
          && old.optString("url").equals(base.toString())
          && old.optString("username").equals(username)) {
        oldIndex = i;
        try {
          p.put("id", old.optString("id"))
              .put("session", old.optString("session"))
              .put("draft", old.optString("draft"))
              .put("title", old.optString("title", "new chat"))
              .put("cache", old.optJSONArray("cache"))
              .put("draft_attachments", old.optJSONArray("draft_attachments"))
              .put("message_attachments", old.optJSONObject("message_attachments"))
              .put("chat_drafts", old.optJSONObject("chat_drafts"))
              .put("unread", old.optJSONObject("unread"))
              .put("watched_chats", old.optJSONArray("watched_chats"));
        } catch (Exception ignored) {
        }
        break;
      }
    }
    final int replaceIndex = oldIndex;
    Gateway candidate =
        new Gateway(
            base,
            new JSONArray(),
            this,
            () -> {
              if (!connecting) save();
            });
    gateway = candidate;
    candidate.login(
        username,
        password,
        (v, e) -> {
          if (candidate != gateway) return;
          if (e != null) {
            connecting = false;
            status = "offline";
            candidate.close();
            gateway = null;
            callback.done(null, e);
            changed(true);
            return;
          }
          try {
            if (replaceIndex < 0) profiles().put(p);
            else profiles().put(replaceIndex, p);
          } catch (Exception ignored) {
          }
          profileId = p.optString("id");
          try {
            data.put("active", profileId);
          } catch (Exception ignored) {
          }
          restore();
          status = "connected";
          connecting = false;
          save();
          callback.done(v, null);
          changed(true);
        });
  }

  void select(String id) {
    if (anyRunning() || submissionPending || uploadsPending > 0) {
      notice = "Let the current task finish before changing servers.";
      changed(false);
      return;
    }
    save();
    close();
    profileId = id;
    try {
      data.put("active", id);
    } catch (Exception ignored) {
    }
    restore();
    save();
    changed(true);
    connectSaved();
  }

  void forget() {
    save();
    close();
    JSONArray a = new JSONArray();
    for (int i = 0; i < profiles().length(); i++) {
      JSONObject p = profiles().optJSONObject(i);
      if (p != null && !p.optString("id").equals(profileId)) a.put(p);
    }
    try {
      data.put("profiles", a).put("active", "");
    } catch (Exception ignored) {
    }
    profileId = "";
    storedId = "";
    draft = "";
    attachments = new JSONArray();
    rows.clear();
    tools.clear();
    requests.clear();
    title = "new chat";
    save();
    changed(true);
  }

  void close() {
    sessionGeneration++;
    if (gateway != null) gateway.close();
    gateway = null;
    connecting = false;
    running = false;
    submissionPending = false;
    uploadsPending = 0;
    watched.clear();
    sessions = new JSONArray();
    listPending = false;
    status = "offline";
    context.stopService(new Intent(context, TurnService.class));
    reconnectScheduled = false;
  }

  void reconnect() {
    save();
    sessionGeneration++;
    if (gateway != null) gateway.close();
    gateway = null;
    connecting = false;
    submissionPending = false;
    connectSaved();
  }

  void connectSaved() {
    if (connecting || (gateway != null && gateway.online()) || profile() == null) return;
    JSONObject p = profile();
    try {
      if (gateway != null) gateway.close();
      connecting = true;
      status = "connecting";
      changed(false);
      gateway =
          new Gateway(
              Endpoint.parse(p.optString("url"), p.optBoolean("http")),
              p.optJSONArray("cookies"),
              this,
              this::save);
      gateway.connect(
          (v, e) -> {
            connecting = false;
            if (e != null) {
              status = "offline";
              notice = e;
              changed(false);
            }
          });
    } catch (Exception e) {
      connecting = false;
      notice = e.getMessage();
      changed(false);
    }
  }

  @Override
  public void connected() {
    connecting = false;
    status = "connected";
    reconnects = 0;
    notice = "";
    if (!storedId.isEmpty()) resume(storedId);
    else changed(false);
    list((v, e) -> {});
    Gateway source = gateway;
    for (WatchedChat chat : new ArrayList<>(watched.values())) {
      if (!chat.running || chat.stored.equals(storedId)) continue;
      source.rpc(
          "session.resume",
          obj("session_id", chat.stored, "source", "android", "close_on_disconnect", false),
          (v, e) -> {
            if (source != gateway || !watched.containsValue(chat)) return;
            if (e == null) {
              chat.runtime = v.optString("session_id", chat.runtime);
              JSONObject info = v.optJSONObject("info");
              if (info != null) chat.title = info.optString("title", chat.title);
              boolean wasRunning = chat.running;
              chat.running = v.optBoolean("running");
              JSONObject inflight = v.optJSONObject("inflight");
              if (inflight != null) chat.running |= inflight.optBoolean("streaming");
              JSONArray open = v.optJSONArray("open_requests");
              chat.requests.clear();
              if (open != null)
                for (int i = 0; i < open.length(); i++) {
                  JSONObject request = open.optJSONObject(i);
                  if (request != null) chat.requests.put(request.optString("id"), request);
                }
              if (wasRunning && !chat.running) {
                String reply = "";
                JSONArray messages = v.optJSONArray("messages");
                if (messages != null)
                  for (int i = messages.length() - 1; i >= 0; i--) {
                    JSONObject message = messages.optJSONObject(i);
                    if (message != null && message.optString("role").equals("assistant")) {
                      reply = Protocol.text(message);
                      break;
                    }
                  }
                markRead(chat.stored, true);
                TurnService.reply(context, reply, "", profileId, chat.stored);
                save();
              }
            }
            taskService();
            changed(false);
          });
    }
  }

  @Override
  public void disconnected(String reason) {
    connecting = false;
    status = "offline";
    notice = reason;
    changed(false);
    if (observer != null || anyRunning()) scheduleReconnect();
  }

  private void scheduleReconnect() {
    if (reconnectScheduled || reconnects >= 3 || profile() == null) return;
    reconnectScheduled = true;
    int delay = (1 << reconnects++) * 2000;
    main.postDelayed(
        () -> {
          reconnectScheduled = false;
          if (profile() != null && (observer != null || anyRunning())) connectSaved();
        },
        delay);
  }

  void newChat() {
    if (submissionPending || uploadsPending > 0 || status.equals("loading chat")) {
      notice = "Wait for this message or attachment to finish loading.";
      changed(false);
      return;
    }
    if (storedId.isEmpty() && !demo) return;
    leaveChat();
    sessionGeneration++;
    if (gateway != null && gateway.online()) status = "connected";
    rows.clear();
    tools.clear();
    requests.clear();
    stream = null;
    running = false;
    storedId = "";
    sessionId = "";
    title = "new chat";
    model = newModel;
    provider = newProvider;
    notice = "";
    restoreDraft("");
    taskService();
    save();
    changed(true);
  }

  void resume(String id) {
    if (submissionPending || uploadsPending > 0) {
      notice = "Wait for your message or attachment to finish loading.";
      changed(false);
      return;
    }
    if (gateway == null || !gateway.online()) {
      notice = "Reconnect to open this chat. Your draft is saved.";
      changed(false);
      return;
    }
    save();
    Gateway source = gateway;
    int generation = ++sessionGeneration;
    status = "loading chat";
    changed(false);
    gateway.rpc(
        "session.resume",
        obj("session_id", id, "source", "android", "close_on_disconnect", false),
        (v, e) -> {
          if (source != gateway || generation != sessionGeneration) return;
          if (e != null) {
            status = "connected";
            notice = e;
            changed(false);
            return;
          }
          if (!id.equals(storedId)) leaveChat();
          String canonical = storedKey(v);
          if (!canonical.isEmpty() && !id.equals(canonical))
            ChatDrafts.move(profile(), id, canonical);
          loadSession(v);
          status = "connected";
          save();
          changed(true);
        });
  }

  private String storedKey(JSONObject result) {
    JSONObject info = result.optJSONObject("info");
    String id = info == null ? "" : info.optString("stored_session_id");
    if (id.isEmpty()) id = result.optString("stored_session_id", result.optString("session_id"));
    return id;
  }

  private void loadSession(JSONObject v) {
    String previousId = storedId;
    sessionId = v.optString("session_id");
    storedId = storedKey(v);
    rows.clear();
    tools.clear();
    requests.clear();
    stream = null;
    JSONArray a = v.optJSONArray("messages");
    if (a != null)
      for (int i = 0; i < a.length(); i++) {
        JSONObject row = a.optJSONObject(i);
        if (row != null) rows.add(row);
      }
    updateInfo(v.optJSONObject("info"));
    if (!previousId.equals(storedId)) restoreDraft(storedId);
    WatchedChat watchedChat = watched.remove(storedId);
    markRead(storedId, false);
    restoreMessageAttachments();
    running = v.optBoolean("running");
    JSONObject inflight = v.optJSONObject("inflight");
    if (inflight != null) {
      String text = inflight.optString("assistant");
      if (!text.isEmpty()) {
        stream = obj("role", "assistant", "text", text);
        rows.add(stream);
      }
      running |= inflight.optBoolean("streaming");
    }
    JSONArray open = v.optJSONArray("open_requests");
    if (open != null)
      for (int i = 0; i < open.length(); i++)
        if (open.optJSONObject(i) != null) request(open.optJSONObject(i));
    if (watchedChat != null) requests.putAll(watchedChat.requests);
    taskService();
  }

  void submit(String text) {
    if ((text.trim().isEmpty() && attachments.length() == 0) || submissionPending) return;
    if (uploadsPending > 0) {
      notice = "Your attachment is still loading.";
      changed(false);
      return;
    }
    if (status.equals("loading chat")) {
      notice = "Loading this chat. Your draft is saved.";
      changed(false);
      return;
    }
    if (gateway == null || !gateway.online()) {
      notice = "Connect before sending. Your draft is saved.";
      changed(false);
      return;
    }
    if (running) {
      notice = "Hermes is working. Stop it before sending another message.";
      changed(false);
      return;
    }
    submissionPending = true;
    changed(false);
    if (sessionId.isEmpty()) {
      int generation = ++sessionGeneration;
      JSONObject params = obj("source", "android", "close_on_disconnect", false);
      if (!newModel.isEmpty())
        try {
          params.put("model", newModel).put("provider", newProvider);
        } catch (Exception ignored) {
        }
      gateway.rpc(
          "session.create",
          params,
          (v, e) -> {
            if (generation != sessionGeneration) return;
            if (e != null) {
              submissionPending = false;
              notice = e;
              changed(false);
              return;
            }
            String createdId = storedKey(v);
            ChatDrafts.move(profile(), "", createdId);
            loadSession(v);
            save();
            attachAndSend(text, 0, new ArrayList<>());
          });
    } else attachAndSend(text, 0, new ArrayList<>());
  }

  private void attachAndSend(String text, int index, List<String> queued) {
    if (gateway == null || !gateway.online()) {
      submissionPending = false;
      notice = "Reconnect before sending. Your attachments are saved.";
      changed(false);
      return;
    }
    if (index >= attachments.length()) {
      send(text);
      return;
    }
    JSONObject a = attachments.optJSONObject(index);
    if (a == null || !a.optString("mime").startsWith("image/")) {
      attachAndSend(text, index + 1, queued);
      return;
    }
    Gateway source = gateway;
    int generation = sessionGeneration;
    source.rpc(
        "image.attach",
        obj("session_id", sessionId, "path", a.optString("path")),
        (v, e) -> {
          if (source != gateway || generation != sessionGeneration) return;
          if (e != null) {
            for (String path : queued)
              source.rpc(
                  "image.detach", obj("session_id", sessionId, "path", path), (r, error) -> {});
            submissionPending = false;
            notice = "Could not attach the image. " + e;
            changed(false);
            return;
          }
          queued.add(v.optString("path", a.optString("path")));
          attachAndSend(text, index + 1, queued);
        });
  }

  private void restoreMessageAttachments() {
    JSONObject p = profile();
    if (p == null) return;
    JSONObject all = p.optJSONObject("message_attachments");
    if (all == null) return;
    JSONObject chat = all.optJSONObject(storedId);
    if (chat == null) return;
    int ordinal = 0;
    for (JSONObject row : rows)
      if (row.optString("role").equals("user")) {
        JSONObject meta = chat.optJSONObject(String.valueOf(ordinal++));
        if (meta != null)
          try {
            row.put("attachments", meta.optJSONArray("attachments"))
                .put("display_text", meta.optString("display_text"));
          } catch (Exception ignored) {
          }
      }
  }

  private void send(String text) {
    String display = text;
    String wire = text.trim().isEmpty() ? "Please look at the attachment." : text;
    JSONArray sent = attachments;
    for (int i = 0; i < sent.length(); i++) {
      JSONObject a = sent.optJSONObject(i);
      if (a != null && !a.optString("mime").startsWith("image/"))
        wire += "\n\n[Attached file: " + a.optString("path") + "]";
    }
    int ordinal = 0;
    for (JSONObject row : rows) if (row.optString("role").equals("user")) ordinal++;
    JSONObject user =
        obj("role", "user", "text", wire, "display_text", display, "attachments", sent);
    JSONObject p = profile();
    if (p != null && sent.length() > 0)
      try {
        JSONObject all = p.optJSONObject("message_attachments");
        if (all == null) {
          all = obj();
          p.put("message_attachments", all);
        }
        JSONObject chat = all.optJSONObject(storedId);
        if (chat == null) {
          chat = obj();
          all.put(storedId, chat);
        }
        chat.put(String.valueOf(ordinal), obj("display_text", display, "attachments", sent));
      } catch (Exception ignored) {
      }
    attachments = new JSONArray();
    tools.clear();
    stream = null;
    rows.add(user);
    running = true;
    notice = "";
    taskService();
    changed(true);
    save();
    gateway.rpc(
        "prompt.submit",
        obj("session_id", sessionId, "text", wire, "surface", "android"),
        (v, e) -> {
          submissionPending = false;
          if (e != null) {
            // An ambiguous disconnect may already have sent the turn; never requeue images blindly.
            notice = e + " The prompt was not retried.";
            if (gateway != null && gateway.online()) {
              running = false;
              resume(storedId);
            }
          } else {
            draft = "";
            save();
          }
          changed(false);
        });
  }

  void stop() {
    stop(storedId);
  }

  void stop(String id) {
    if (gateway == null) return;
    gateway.rpc(
        "session.interrupt",
        obj("session_id", id),
        (v, e) -> {
          if (id.equals(storedId)) notice = e == null ? "Stop requested." : e;
          changed(false);
        });
  }

  void list(Gateway.Result cb) {
    if (gateway == null) {
      cb.done(null, "Connect first.");
      return;
    }
    Gateway source = gateway;
    gateway.rpc(
        "session.list",
        obj("limit", 100),
        (v, e) -> {
          if (source != gateway) return;
          if (e == null) {
            sessions = v.optJSONArray("sessions");
            changed(false);
          }
          cb.done(v, e);
        });
  }

  void updateInfo(JSONObject info) {
    if (info == null) return;
    model = info.optString("model", model);
    provider = info.optString("provider", provider);
    title = info.optString("title", title);
    storedId = info.optString("stored_session_id", storedId);
  }

  @Override
  public void event(JSONObject event) {
    String type = event.optString("type");
    JSONObject p = event.optJSONObject("payload");
    if (p == null) p = obj();
    if (type.equals("gateway.ready")) {
      String next = p.optString("replay_epoch");
      if (!next.equals(epoch)) {
        epoch = next;
        sequences.clear();
      }
      return;
    }
    String id = event.optString("session_id");
    WatchedChat background = watchedChat(id);
    boolean active = !id.isEmpty() && (id.equals(sessionId) || id.equals(storedId));
    if (!type.equals("sessions.changed") && !active && background == null) return;
    if (event.has("seq")) {
      long seq = event.optLong("seq");
      String key = id + ":" + epoch;
      if (seq <= sequences.getOrDefault(key, -1L)) return;
      sequences.put(key, seq);
    }
    if (!active && background != null) {
      backgroundEvent(background, type, p);
      return;
    }
    switch (type) {
      case "message.start":
        running = true;
        stream = null;
        taskService();
        changed(false);
        break;
      case "message.delta":
        if (stream == null) {
          stream = obj("role", "assistant", "text", "");
          rows.add(stream);
          changed(true);
        }
        try {
          stream.put("text", stream.optString("text") + p.optString("text"));
        } catch (Exception ignored) {
        }
        renderSoon();
        break;
      case "message.complete":
        boolean notifyReply = running && !p.optString("status").equals("interrupted");
        if (stream == null && p.opt("text") instanceof String && !p.optString("text").isEmpty()) {
          stream = obj("role", "assistant", "text", p.optString("text"));
          rows.add(stream);
        } else if (stream != null
            && p.opt("text") instanceof String
            && !p.optString("text").isEmpty())
          try {
            stream.put("text", p.optString("text"));
          } catch (Exception ignored) {
          }
        running = false;
        submissionPending = false;
        status = gateway != null && gateway.online() ? "connected" : "offline";
        stream = null;
        notice = p.optString("error", p.optString("warning", ""));
        if (p.optString("status").equals("interrupted")) notice = "Stopped.";
        taskService();
        if (notifyReply) {
          String reply = p.optString("text");
          if (reply.isEmpty())
            for (int i = rows.size() - 1; i >= 0; i--)
              if (rows.get(i).optString("role").equals("assistant")) {
                reply = Protocol.text(rows.get(i));
                break;
              }
          TurnService.reply(context, reply, notice, profileId, storedId);
        }
        save();
        changed(true);
        break;
      case "message.interim":
        if (stream != null) stream = null;
        changed(true);
        break;
      case "tool.start":
      case "tool.complete":
        JSONObject tool = tools.get(p.optString("tool_id"));
        if (tool == null) tool = obj();
        try {
          Iterator<String> keys = p.keys();
          while (keys.hasNext()) {
            String k = keys.next();
            tool.put(k, p.opt(k));
          }
          tool.put("done", type.equals("tool.complete"));
        } catch (Exception ignored) {
        }
        tools.put(p.optString("tool_id"), tool);
        changed(true);
        break;
      case "session.info":
        updateInfo(p);
        changed(false);
        break;
      case "session.title":
        title = p.optString("title", title);
        save();
        changed(false);
        break;
      case "status.update":
        status = p.optString("text", "working");
        renderSoon();
        break;
      case "session.usage":
        try {
          if (profile() != null) profile().put("usage", p.optJSONObject("usage"));
        } catch (Exception ignored) {
        }
        changed(false);
        break;
      case "sessions.changed":
        if (!listPending && gateway != null && gateway.online()) {
          listPending = true;
          main.postDelayed(
              () ->
                  list(
                      (v, e) -> {
                        listPending = false;
                      }),
              300);
        }
        break;
      case "session.reclaimed":
        notice = "Session moved on the server. Reconnect to resume.";
        changed(false);
        break;
    }
  }

  private void backgroundEvent(WatchedChat chat, String type, JSONObject payload) {
    switch (type) {
      case "message.start":
        chat.running = true;
        chat.reply = "";
        break;
      case "message.delta":
        if (chat.reply.length() < 100000) chat.reply += payload.optString("text");
        return;
      case "message.complete":
        boolean notify = chat.running && !payload.optString("status").equals("interrupted");
        chat.running = false;
        chat.requests.clear();
        if (notify) {
          markRead(chat.stored, true);
          TurnService.reply(
              context,
              payload.optString("text", chat.reply),
              payload.optString("error", payload.optString("warning")),
              profileId,
              chat.stored);
        }
        save();
        break;
      case "session.info":
        chat.title = payload.optString("title", chat.title);
        break;
      case "session.title":
        chat.title = payload.optString("title", chat.title);
        break;
      default:
        return;
    }
    taskService();
    changed(false);
  }

  @Override
  public void request(JSONObject r) {
    JSONObject params = r.optJSONObject("params");
    String id = params == null ? "" : params.optString("session_id");
    if (!id.isEmpty() && !id.equals(sessionId) && !id.equals(storedId)) {
      WatchedChat chat = watchedChat(id);
      if (chat != null
          && (r.optString("method").equals("approval")
              || r.optString("method").equals("clarify"))) {
        chat.requests.put(r.optString("id"), r);
        TurnService.reply(
            context, "Hermes needs an answer in " + chat.title + ".", "", profileId, chat.stored);
        changed(false);
      } else if (gateway != null) gateway.unsupported(r);
      return;
    }
    String method = r.optString("method");
    if (method.equals("approval") || method.equals("clarify")) {
      requests.put(r.optString("id"), r);
      if (observer != null) observer.request(r);
      changed(false);
    } else if (gateway != null) gateway.unsupported(r);
  }

  void answer(JSONObject r, JSONObject value) {
    if (gateway == null || !gateway.online()) {
      notice = "Reconnect before answering.";
      changed(false);
      return;
    }
    gateway.answer(r, value);
    requests.remove(r.optString("id"));
    changed(false);
  }

  String export() {
    StringBuilder s = new StringBuilder("# " + title + "\n\n");
    for (JSONObject row : rows) {
      String role = row.optString("role"), t = Attachments.displayText(row);
      if ((role.equals("user") || role.equals("assistant")) && !t.isEmpty())
        s.append("## ").append(role).append("\n\n").append(t).append("\n\n");
    }
    return s.toString();
  }

  void theme(String theme) {
    try {
      data.put("theme", theme);
    } catch (Exception ignored) {
    }
    save();
    changed(true);
  }

  void taskService() {
    try {
      Intent i = new Intent(context, TurnService.class);
      if (anyRunning()) context.startForegroundService(i);
      else context.stopService(i);
    } catch (Exception ignored) {
    }
  }

  void demo() {
    save();
    close();
    profileId = "";
    storedId = "";
    sessionId = "";
    draft = "";
    demo = true;
    attachments = new JSONArray();
    sessions = new JSONArray();
    title = "a little help";
    model = "demo";
    provider = "offline";
    rows.clear();
    tools.clear();
    rows.add(obj("role", "user", "text", "Check the docs and explain it simply."));
    rows.add(
        obj(
            "role",
            "assistant",
            "text",
            "A small client. Your own server.\n\n"
                + "- Chat from your phone\n"
                + "- Watch tools work\n"
                + "- Keep your sessions\n\n"
                + "**Ready when you are.** Connect a server to start a real conversation."));
    tools.put("demo", obj("name", "browser", "summary", "read · docs", "done", true));
    status = "offline demo";
    notice = "Preview only — no server requests.";
    changed(true);
  }
}
