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
    if (p == null) return;
    storedId = p.optString("session");
    draft = p.optString("draft");
    if (p.optJSONArray("draft_attachments") != null)
      attachments = p.optJSONArray("draft_attachments");
    title = p.optString("title", "new chat");
    JSONArray a = p.optJSONArray("cache");
    if (a != null)
      for (int i = 0; i < a.length(); i++)
        if (a.optJSONObject(i) != null) rows.add(a.optJSONObject(i));
  }

  void save() {
    JSONObject p = profile();
    if (p != null) {
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
    if (running || submissionPending || uploadsPending > 0) {
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
              .put("message_attachments", old.optJSONObject("message_attachments"));
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
    if (running || submissionPending || uploadsPending > 0) {
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
    status = "offline";
    context.stopService(new Intent(context, TurnService.class));
    reconnectScheduled = false;
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
    changed(false);
    if (!storedId.isEmpty()) resume(storedId);
  }

  @Override
  public void disconnected(String reason) {
    connecting = false;
    status = "offline";
    notice = reason;
    changed(false);
    if (observer != null || running) scheduleReconnect();
  }

  private void scheduleReconnect() {
    if (reconnectScheduled || reconnects >= 3 || profile() == null) return;
    reconnectScheduled = true;
    int delay = (1 << reconnects++) * 2000;
    main.postDelayed(
        () -> {
          reconnectScheduled = false;
          if (profile() != null && (observer != null || running)) connectSaved();
        },
        delay);
  }

  void newChat() {
    if (running || submissionPending || uploadsPending > 0) {
      notice = "Finish or stop the current task first.";
      changed(false);
      return;
    }
    sessionGeneration++;
    if (gateway != null && gateway.online()) status = "connected";
    rows.clear();
    tools.clear();
    requests.clear();
    stream = null;
    storedId = "";
    sessionId = "";
    title = "new chat";
    model = newModel;
    provider = newProvider;
    draft = "";
    attachments = new JSONArray();
    save();
    changed(true);
  }

  void resume(String id) {
    if (uploadsPending > 0) {
      notice = "Wait for your attachment to finish loading.";
      changed(false);
      return;
    }
    if (gateway == null || !gateway.online()) return;
    int generation = ++sessionGeneration;
    status = "loading chat";
    changed(false);
    gateway.rpc(
        "session.resume",
        obj("session_id", id, "source", "android", "close_on_disconnect", false),
        (v, e) -> {
          if (generation != sessionGeneration) return;
          if (e != null) {
            status = "connected";
            notice = e;
            changed(false);
            return;
          }
          loadSession(v);
          status = "connected";
          save();
          changed(true);
        });
  }

  private void loadSession(JSONObject v) {
    sessionId = v.optString("session_id");
    storedId = v.optString("stored_session_id", sessionId);
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
    if (gateway == null) return;
    gateway.rpc(
        "session.interrupt",
        obj("session_id", sessionId),
        (v, e) -> {
          notice = e == null ? "Stop requested." : e;
          changed(false);
        });
  }

  void list(Gateway.Result cb) {
    if (gateway == null) {
      cb.done(null, "Connect first.");
      return;
    }
    gateway.rpc(
        "session.list",
        obj("limit", 100),
        (v, e) -> {
          if (e == null) sessions = v.optJSONArray("sessions");
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
    if (!type.equals("sessions.changed")
        && (id.isEmpty()
            || (sessionId.isEmpty() && storedId.isEmpty())
            || (!id.equals(sessionId) && !id.equals(storedId)))) return;
    if (event.has("seq")) {
      long seq = event.optLong("seq");
      String key = id + ":" + epoch;
      if (seq <= sequences.getOrDefault(key, -1L)) return;
      sequences.put(key, seq);
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
          TurnService.reply(context, reply, notice);
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
        if (observer != null) changed(false);
        break;
      case "session.reclaimed":
        notice = "Session moved on the server. Reconnect to resume.";
        changed(false);
        break;
    }
  }

  @Override
  public void request(JSONObject r) {
    JSONObject params = r.optJSONObject("params");
    String id = params == null ? "" : params.optString("session_id");
    if (!id.isEmpty() && !id.equals(sessionId) && !id.equals(storedId)) {
      if (gateway != null) gateway.unsupported(r);
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
      if (running) context.startForegroundService(i);
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
