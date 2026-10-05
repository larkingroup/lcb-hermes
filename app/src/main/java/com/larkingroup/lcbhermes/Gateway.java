package com.larkingroup.lcbhermes;

import static com.larkingroup.lcbhermes.Protocol.obj;

import android.os.Handler;
import android.os.Looper;
import java.io.*;
import java.util.*;
import java.util.concurrent.*;
import okhttp3.*;
import org.json.*;

final class Gateway {
  interface Result {
    void done(JSONObject value, String error);
  }

  interface Listener {
    void connected();

    void disconnected(String reason);

    void event(JSONObject event);

    void request(JSONObject request);
  }

  final Handler main = new Handler(Looper.getMainLooper());
  private final ScheduledExecutorService timers = Executors.newSingleThreadScheduledExecutor();
  private final ConcurrentHashMap<String, Result> pending = new ConcurrentHashMap<>();
  private final List<Cookie> cookies = new ArrayList<>();
  private final HttpUrl base;
  private final OkHttpClient client;
  private final Listener listener;
  private final Runnable cookieChanged;
  private WebSocket socket;
  private boolean disposed, online;
  private long nextId;

  Gateway(HttpUrl base, JSONArray savedCookies, Listener listener, Runnable cookieChanged) {
    this.base = base;
    this.listener = listener;
    this.cookieChanged = cookieChanged;
    if (savedCookies != null)
      for (int i = 0; i < savedCookies.length(); i++) {
        Cookie c = Cookie.parse(base, savedCookies.optString(i));
        if (c != null && c.expiresAt() > System.currentTimeMillis() && c.matches(base))
          cookies.add(c);
      }
    client =
        new OkHttpClient.Builder()
            .dns(Endpoint.dns(base))
            .followRedirects(false)
            .followSslRedirects(false)
            .connectTimeout(15, TimeUnit.SECONDS)
            .readTimeout(60, TimeUnit.SECONDS)
            .writeTimeout(60, TimeUnit.SECONDS)
            .pingInterval(20, TimeUnit.SECONDS)
            .cookieJar(
                new CookieJar() {
                  public synchronized void saveFromResponse(HttpUrl url, List<Cookie> incoming) {
                    synchronized (cookies) {
                      for (Cookie c : incoming) {
                        cookies.removeIf(
                            x ->
                                x.name().equals(c.name())
                                    && x.domain().equals(c.domain())
                                    && x.path().equals(c.path()));
                        if (c.expiresAt() > System.currentTimeMillis()) cookies.add(c);
                      }
                    }
                    main.post(cookieChanged);
                  }

                  public List<Cookie> loadForRequest(HttpUrl url) {
                    List<Cookie> out = new ArrayList<>();
                    synchronized (cookies) {
                      for (Cookie c : cookies)
                        if (c.matches(url) && c.expiresAt() > System.currentTimeMillis())
                          out.add(c);
                    }
                    return out;
                  }
                })
            .build();
    timers.scheduleWithFixedDelay(
        () ->
            main.post(
                () -> {
                  if (online)
                    rpc(
                        "ping",
                        obj(),
                        (v, e) -> {
                          if (e != null && socket != null) socket.cancel();
                        });
                }),
        20,
        20,
        TimeUnit.SECONDS);
  }

  JSONArray savedCookies() {
    JSONArray a = new JSONArray();
    synchronized (cookies) {
      for (Cookie c : cookies) a.put(c.toString());
    }
    return a;
  }

  String origin() {
    return base.toString().replaceAll("/$", "");
  }

  boolean online() {
    return online;
  }

  void login(String user, String password, Result result) {
    rest(
        "/auth/password-login",
        obj("provider", "basic", "username", user, "password", password),
        (v, e) -> {
          if (e == null) connect(result);
          else result.done(null, e);
        });
  }

  void connect(Result result) {
    rest(
        "/api/auth/ws-ticket",
        obj(),
        (v, e) -> {
          if (disposed) return;
          if (e != null) {
            result.done(null, e);
            return;
          }
          String ticket = v.optString("ticket");
          if (ticket.isEmpty()) {
            result.done(null, "The dashboard did not issue a connection ticket.");
            return;
          }
          HttpUrl url =
              base.newBuilder().encodedPath("/api/ws").addQueryParameter("ticket", ticket).build();
          Request request = new Request.Builder().url(url).header("Origin", origin()).build();
          socket =
              client.newWebSocket(
                  request,
                  new WebSocketListener() {
                    public void onOpen(WebSocket ws, Response response) {
                      main.post(
                          () -> {
                            if (disposed) {
                              ws.close(1000, "closed");
                              return;
                            }
                            online = true;
                            rpc(
                                "client.capabilities",
                                obj("server_requests", true),
                                (r, x) -> {
                                  result.done(r, x);
                                  if (x == null) listener.connected();
                                });
                          });
                    }

                    public void onMessage(WebSocket ws, String frame) {
                      try {
                        List<JSONObject> batch = Protocol.frames(frame);
                        main.post(
                            () -> {
                              if (disposed) return;
                              for (JSONObject m : batch) dispatch(m);
                            });
                      } catch (Exception x) {
                        ws.close(1002, "invalid response");
                        main.post(() -> lost("The server sent an invalid response."));
                      }
                    }

                    public void onFailure(WebSocket ws, Throwable t, Response response) {
                      main.post(
                          () -> {
                            if (!online)
                              result.done(null, "Connection failed. Check the server and network.");
                            lost("Connection lost. Your task stays on the server.");
                          });
                    }

                    public void onClosed(WebSocket ws, int code, String reason) {
                      main.post(() -> lost("Disconnected. Your task stays on the server."));
                    }
                  });
        });
  }

  private void dispatch(JSONObject m) {
    if (m.has("method")) {
      if (m.optString("method").equals("event")) {
        JSONObject e = m.optJSONObject("params");
        if (e != null) listener.event(e);
      } else if (m.has("id")) listener.request(m);
    } else {
      Result cb = pending.remove(m.optString("id"));
      if (cb == null) return;
      JSONObject error = m.optJSONObject("error");
      Object raw = m.opt("result");
      cb.done(
          raw instanceof JSONObject ? (JSONObject) raw : obj("value", raw),
          error == null ? null : error.optString("message", "Request failed"));
    }
  }

  void rpc(String method, JSONObject params, Result cb) {
    if (!online || socket == null) {
      cb.done(null, "Connect to Hermes first.");
      return;
    }
    String id = "android-" + (++nextId);
    pending.put(id, cb);
    if (!socket.send(
        obj("jsonrpc", "2.0", "id", id, "method", method, "params", params).toString())) {
      pending.remove(id);
      cb.done(null, "Connection closed. Reconnect to check the task.");
      return;
    }
    timers.schedule(
        () ->
            main.post(
                () -> {
                  Result timed = pending.remove(id);
                  if (timed != null)
                    timed.done(
                        null, "The server did not respond in time. Reconnect to check the task.");
                }),
        60,
        TimeUnit.SECONDS);
  }

  void answer(JSONObject request, JSONObject answer) {
    if (online && socket != null)
      socket.send(obj("jsonrpc", "2.0", "id", request.opt("id"), "result", answer).toString());
  }

  void unsupported(JSONObject request) {
    if (online && socket != null)
      socket.send(
          obj(
                  "jsonrpc",
                  "2.0",
                  "id",
                  request.opt("id"),
                  "error",
                  obj("code", -32601, "message", "This client cannot handle that request"))
              .toString());
  }

  void rest(String path, JSONObject body, Result result) {
    HttpUrl url = base.resolve(path);
    if (url == null
        || !url.host().equals(base.host())
        || url.port() != base.port()
        || !url.scheme().equals(base.scheme())) {
      result.done(null, "Invalid server path");
      return;
    }
    Request.Builder b = new Request.Builder().url(url).header("Origin", origin());
    if (body != null)
      b.post(RequestBody.create(body.toString(), MediaType.get("application/json")));
    call(b.build(), result);
  }

  void upload(String path, String name, byte[] bytes, Result result) {
    RequestBody body =
        new MultipartBody.Builder()
            .setType(MultipartBody.FORM)
            .addFormDataPart("path", path)
            .addFormDataPart("overwrite", "false")
            .addFormDataPart(
                "file", name, RequestBody.create(bytes, MediaType.get("application/octet-stream")))
            .build();
    call(
        new Request.Builder()
            .url(base.resolve("/api/files/upload-stream"))
            .header("Origin", origin())
            .post(body)
            .build(),
        result);
  }

  private void call(Request request, Result result) {
    client
        .newCall(request)
        .enqueue(
            new Callback() {
              public void onFailure(Call call, IOException e) {
                main.post(
                    () -> {
                      if (!disposed)
                        result.done(null, Endpoint.connectionError(e, base));
                    });
              }

              public void onResponse(Call call, Response response) {
                try (response) {
                  if (response.body() != null && response.body().contentLength() > 32 * 1024 * 1024)
                    throw new IOException("Response too large");
                  String text = response.body() == null ? "{}" : response.body().string();
                  JSONObject json = new JSONObject(text);
                  String error =
                      response.isSuccessful()
                          ? null
                          : response.code() == 401
                              ? "Sign in again to this dashboard."
                              : json.optString("detail", "Server returned " + response.code());
                  main.post(
                      () -> {
                        if (!disposed) result.done(json, error);
                      });
                } catch (Exception e) {
                  main.post(
                      () -> {
                        if (!disposed) result.done(null, "Unexpected server response.");
                      });
                }
              }
            });
  }

  private void lost(String why) {
    if (disposed) return;
    online = false;
    for (Result cb : pending.values()) cb.done(null, why);
    pending.clear();
    listener.disconnected(why);
  }

  void close() {
    disposed = true;
    online = false;
    if (socket != null) socket.close(1000, "leaving");
    timers.shutdownNow();
    pending.clear();
    client.dispatcher().cancelAll();
    client.connectionPool().evictAll();
  }
}
