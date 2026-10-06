package com.larkingroup.lcbhermes;

import java.io.IOException;
import java.net.InetAddress;
import java.security.cert.CertificateException;
import java.util.List;
import java.util.concurrent.TimeUnit;
import javax.net.ssl.SSLPeerUnverifiedException;
import okhttp3.Dns;
import okhttp3.HttpUrl;
import okhttp3.OkHttpClient;
import okhttp3.Request;
import okhttp3.Response;
import org.json.JSONObject;

public final class Endpoint {
  public static HttpUrl parse(String input, boolean privateHttp) {
    String value = input.trim();
    if (!value.contains("://")) value = "https://" + value;
    HttpUrl url = HttpUrl.parse(value);
    if (url == null
        || !url.username().isEmpty()
        || !url.password().isEmpty()
        || url.query() != null
        || url.fragment() != null
        || !url.encodedPath().equals("/"))
      throw new IllegalArgumentException(
          "Use a dashboard URL, such as https://server:port");
    if (!url.isHttps() && !privateHttp)
      throw new IllegalArgumentException("Enable private HTTP for a local server, or use HTTPS.");
    return url;
  }

  public static boolean privateAddress(byte[] a) {
    if (a.length == 4) {
      int x = a[0] & 255, y = a[1] & 255;
      return x == 10
          || (x == 172 && y >= 16 && y <= 31)
          || (x == 192 && y == 168)
          || (x == 100 && y >= 64 && y <= 127);
    }
    // Unique local IPv6. Link-local needs an interface scope and is deliberately excluded.
    return a.length == 16 && (a[0] & 254) == 252;
  }

  public static Dns dns(HttpUrl origin) {
    return host -> {
      List<InetAddress> addresses = Dns.SYSTEM.lookup(host);
      if (!origin.isHttps())
        for (InetAddress a : addresses)
          if (!privateAddress(a.getAddress()))
            throw new java.net.UnknownHostException(
                "HTTP is limited to private addresses. Use HTTPS.");
      return addresses;
    };
  }

  public static void verify(HttpUrl origin) throws IOException {
    dns(origin).lookup(origin.host());
  }

  interface Probe {
    void check(HttpUrl url) throws IOException;
  }

  static HttpUrl resolve(HttpUrl url, boolean privateHttp) throws IOException {
    return resolve(url, privateHttp, Endpoint::probe);
  }

  // Probe without cookies or credentials before choosing a transport.
  static HttpUrl resolve(HttpUrl url, boolean privateHttp, Probe probe) throws IOException {
    IOException first;
    try {
      probe.check(url);
      return url;
    } catch (IOException error) {
      first = error;
      if (certificateFailure(error))
        throw new IOException("The HTTPS certificate could not be verified.", error);
    }
    HttpUrl alternate =
        url.newBuilder()
            .scheme(url.isHttps() ? "http" : "https")
            .port(
                url.port() == HttpUrl.defaultPort(url.scheme())
                    ? HttpUrl.defaultPort(url.isHttps() ? "http" : "https")
                    : url.port())
            .build();
    if (!alternate.isHttps()) {
      try {
        verify(alternate);
      } catch (IOException error) {
        throw new IOException(connectionError(first, url), first);
      }
    }
    try {
      probe.check(alternate);
    } catch (IOException error) {
      if (certificateFailure(error))
        throw new IOException("The HTTPS certificate could not be verified.", error);
      throw new IOException(connectionError(first, url), first);
    }
    if (!alternate.isHttps() && !privateHttp)
      throw new IOException("This server uses HTTP. Tick ‘allow private HTTP’ and connect again.");
    return alternate;
  }

  static boolean certificateFailure(Throwable error) {
    for (Throwable e = error; e != null; e = e.getCause())
      if (e instanceof CertificateException || e instanceof SSLPeerUnverifiedException) return true;
    return false;
  }

  static String connectionError(Throwable error, HttpUrl url) {
    if (certificateFailure(error)) return "The HTTPS certificate could not be verified.";
    return "Could not connect to "
        + url.host()
        + ":"
        + url.port()
        + ". Check the address, port, and network.";
  }

  private static void probe(HttpUrl url) throws IOException {
    verify(url);
    OkHttpClient client =
        new OkHttpClient.Builder()
            .dns(dns(url))
            .followRedirects(false)
            .followSslRedirects(false)
            .connectTimeout(5, TimeUnit.SECONDS)
            .readTimeout(5, TimeUnit.SECONDS)
            .callTimeout(8, TimeUnit.SECONDS)
            .build();
    try (Response response =
        client
            .newCall(new Request.Builder().url(url.resolve("/api/status")).get().build())
            .execute()) {
      if (!response.isSuccessful() || response.body() == null)
        throw new IOException("Dashboard not available");
      if (response.body().contentLength() > 1024 * 1024)
        throw new IOException("Unexpected dashboard response");
      JSONObject status = new JSONObject(response.body().string());
      if (status.optString("version").isEmpty() || !status.has("gateway_running"))
        throw new IOException("Not a Hermes dashboard");
    } catch (org.json.JSONException error) {
      throw new IOException("Not a Hermes dashboard", error);
    } finally {
      client.connectionPool().evictAll();
      client.dispatcher().executorService().shutdown();
    }
  }

  private Endpoint() {}
}
