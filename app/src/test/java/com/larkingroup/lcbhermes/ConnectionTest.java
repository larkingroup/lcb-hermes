package com.larkingroup.lcbhermes;

import static org.junit.Assert.*;

import java.net.InetAddress;
import org.junit.Test;

public class ConnectionTest {
  @Test
  public void acceptsDashboardRootAndExplicitPrivateHttp() {
    assertEquals("nas.local", Endpoint.parse("https://nas.local:9119", false).host());
    assertEquals(9119, Endpoint.parse("http://192.168.56.10:9119", true).port());
    assertEquals("https://nas.local:9119/", Endpoint.parse("nas.local:9119", false).toString());
  }

  @Test
  public void recoversWrongHttpsOnAnApprovedPrivateServer() throws Exception {
    java.util.List<String> attempts = new java.util.ArrayList<>();
    okhttp3.HttpUrl resolved =
        Endpoint.resolve(
            Endpoint.parse("https://192.168.56.10:9119", true),
            true,
            url -> {
              attempts.add(url.scheme());
              if (url.isHttps()) throw new javax.net.ssl.SSLException("wrong version number");
            });
    assertEquals(java.util.Arrays.asList("https", "http"), attempts);
    assertEquals("http://192.168.56.10:9119/", resolved.toString());
  }

  @Test
  public void explainsPrivateHttpOptInAndDoesNotRetryPublicHttp() throws Exception {
    try {
      Endpoint.resolve(
          Endpoint.parse("https://192.168.56.10:9119", false),
          false,
          url -> {
            if (url.isHttps()) throw new java.io.IOException("TLS failed");
          });
      fail("unapproved cleartext transport selected");
    } catch (java.io.IOException error) {
      assertTrue(error.getMessage().contains("Tick"));
    }
    java.util.List<String> attempts = new java.util.ArrayList<>();
    try {
      Endpoint.resolve(
          Endpoint.parse("https://8.8.8.8:9119", true),
          true,
          url -> {
            attempts.add(url.scheme());
            throw new java.io.IOException("unreachable");
          });
      fail("public HTTP retry allowed");
    } catch (java.io.IOException expected) {
      assertEquals(java.util.Collections.singletonList("https"), attempts);
    }
  }

  @Test
  public void neverDowngradesACertificateFailure() throws Exception {
    java.util.List<String> attempts = new java.util.ArrayList<>();
    try {
      Endpoint.resolve(
          Endpoint.parse("https://192.168.56.10:9119", true),
          true,
          url -> {
            attempts.add(url.scheme());
            throw new javax.net.ssl.SSLHandshakeException("bad certificate") {
              {
                initCause(new java.security.cert.CertificateException("untrusted"));
              }
            };
          });
      fail("certificate error bypassed");
    } catch (java.io.IOException error) {
      assertTrue(error.getMessage().contains("certificate"));
      assertEquals(java.util.Collections.singletonList("https"), attempts);
    }
  }

  @Test
  public void rejectsCredentialsPathsAndUnapprovedHttp() {
    for (String s :
        new String[] {
          "http://192.168.56.10:9119",
          "https://me:secret@nas/",
          "https://nas/api/ws",
          "https://nas/?token=a",
          "https://nas/#a",
          "not a url"
        }) {
      try {
        Endpoint.parse(s, false);
        fail(s);
      } catch (IllegalArgumentException expected) {
      }
    }
  }

  @Test
  public void checksEveryPrivateAddressBoundary() throws Exception {
    for (String ip :
        new String[] {
          "10.0.0.1",
          "172.16.0.1",
          "172.31.255.254",
          "192.168.56.10",
          "100.64.0.1",
          "100.127.255.254",
          "fd7a:115c:a1e0::1"
        }) assertTrue(ip, Endpoint.privateAddress(InetAddress.getByName(ip).getAddress()));
    for (String ip :
        new String[] {
          "8.8.8.8",
          "172.15.0.1",
          "172.32.0.1",
          "100.63.255.254",
          "100.128.0.1",
          "127.0.0.1",
          "169.254.0.1",
          "0.0.0.0",
          "::1",
          "fe80::1",
          "2001:4860:4860::8888"
        }) assertFalse(ip, Endpoint.privateAddress(InetAddress.getByName(ip).getAddress()));
  }

  @Test
  public void dnsStopsPublicHttpBeforeLogin() throws Exception {
    try {
      Endpoint.verify(Endpoint.parse("http://127.0.0.1", true));
      fail("loopback must not accept cleartext credentials");
    } catch (java.io.IOException expected) {
    }
  }

  @Test
  public void readsCoalescedFramesWithoutDroppingEvents() throws Exception {
    String first =
        "{\"jsonrpc\":\"2.0\",\"method\":\"event\",\"params\":{\"type\":\"message.delta\",\"payload\":{\"text\":\"hello\\n"
            + "world\"}}}";
    String second =
        "{\"jsonrpc\":\"2.0\",\"id\":\"android-1\",\"result\":{\"status\":\"streaming\"}}";
    assertEquals(2, Protocol.frames(first + "\n" + second + "\n").size());
    assertEquals(
        "hello\nworld",
        Protocol.frames(first)
            .get(0)
            .getJSONObject("params")
            .getJSONObject("payload")
            .getString("text"));
  }

  @Test
  public void rejectsMalformedFrame() throws Exception {
    try {
      Protocol.frames("{}\nnot json");
      fail("invalid JSON accepted");
    } catch (org.json.JSONException expected) {
    }
  }

  @Test
  public void extractsStructuredTranscriptText() {
    assertEquals(
        "one\ntwo",
        Protocol.text(
            Protocol.obj(
                "content",
                new org.json.JSONArray()
                    .put(Protocol.obj("type", "text", "text", "one"))
                    .put(Protocol.obj("type", "image", "url", "hidden"))
                    .put(Protocol.obj("type", "text", "text", "two")))));
  }
}
