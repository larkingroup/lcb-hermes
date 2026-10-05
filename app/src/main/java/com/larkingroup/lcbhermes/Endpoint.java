package com.larkingroup.lcbhermes;

import java.io.IOException;
import java.net.InetAddress;
import java.util.List;
import okhttp3.Dns;
import okhttp3.HttpUrl;

public final class Endpoint {
  public static HttpUrl parse(String input, boolean privateHttp) {
    HttpUrl url = HttpUrl.parse(input.trim());
    if (url == null
        || !url.username().isEmpty()
        || !url.password().isEmpty()
        || url.query() != null
        || url.fragment() != null
        || !url.encodedPath().equals("/"))
      throw new IllegalArgumentException(
          "Use a dashboard URL, such as https://hermes.example.com:9119");
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

  private Endpoint() {}
}
