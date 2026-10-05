package com.larkingroup.lcbhermes;

import static com.larkingroup.lcbhermes.Protocol.obj;
import static org.junit.Assert.*;

import android.content.*;
import android.os.*;
import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.platform.app.InstrumentationRegistry;
import androidx.test.rule.ActivityTestRule;
import androidx.test.uiautomator.*;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.*;
import java.util.function.BooleanSupplier;
import org.json.*;
import org.junit.*;
import org.junit.runner.RunWith;

@RunWith(AndroidJUnit4.class)
public class ClientTest {
  @Rule public ActivityTestRule<MainActivity> activity = new ActivityTestRule<>(MainActivity.class);
  private final UiDevice device =
      UiDevice.getInstance(InstrumentationRegistry.getInstrumentation());

  private Controller c() {
    return activity.getActivity().c;
  }

  private void ui(Runnable r) {
    InstrumentationRegistry.getInstrumentation().runOnMainSync(r);
  }

  private void until(BooleanSupplier condition, long seconds) throws Exception {
    long end = System.nanoTime() + TimeUnit.SECONDS.toNanos(seconds);
    while (System.nanoTime() < end) {
      boolean[] yes = {false};
      ui(() -> yes[0] = condition.getAsBoolean());
      if (yes[0]) return;
      Thread.sleep(200);
    }
    throw new AssertionError(
        "Timed out waiting for client state: " + c().status + "; " + c().notice);
  }

  private void capture(String name) throws Exception {
    device.waitForIdle();
    File dir = new File(activity.getActivity().getFilesDir(), "captures");
    dir.mkdirs();
    assertTrue(device.takeScreenshot(new File(dir, name + ".png")));
  }

  @Test
  public void nativeClientConnectsBrowsesAndResumes() throws Exception {
    File fixture = new File(activity.getActivity().getFilesDir(), "fixture.json");
    Assume.assumeTrue("private test access required", fixture.exists());
    JSONObject access =
        new JSONObject(
            new String(java.nio.file.Files.readAllBytes(fixture.toPath()), StandardCharsets.UTF_8));
    assertTrue(fixture.delete());
    until(() -> c().loaded, 15);
    Context testContext =
        new ContextWrapper(activity.getActivity()) {
          @Override
          public File getFilesDir() {
            File f = new File(super.getFilesDir(), "store-test");
            f.mkdirs();
            return f;
          }
        };
    SecureStore store = new SecureStore(testContext);
    String secret = "test-cookie-not-plaintext";
    store.write(obj("cookie", secret, "draft", "test draft"));
    assertEquals(secret, new SecureStore(testContext).read().getString("cookie"));
    byte[] encrypted =
        java.nio.file.Files.readAllBytes(
            new File(testContext.getFilesDir(), "private.bin").toPath());
    assertFalse(new String(encrypted, StandardCharsets.ISO_8859_1).contains(secret));
    CountDownLatch login = new CountDownLatch(1);
    String[] failure = {null};
    ui(
        () ->
            c().login(
                    "TrueNAS",
                    access.optString("url"),
                    access.optString("username"),
                    access.optString("password"),
                    true,
                    (v, e) -> {
                      failure[0] = e;
                      login.countDown();
                    }));
    assertTrue("login timeout", login.await(90, TimeUnit.SECONDS));
    assertNull(failure[0]);
    until(() -> c().gateway != null && c().gateway.online(), 15);
    ui(() -> c().newChat());
    String prompt =
        "Use your local browser to open https://en.wikipedia.org, type Hermes into the search"
            + " field, click the Search button, and tell me the resulting page title. End your"
            + " answer with LCB_HERMES_OK. Do not use a web search API.";
    ui(() -> c().submit(prompt));
    until(
        () -> c().tools.values().stream().anyMatch(t -> t.optString("name").contains("browser")),
        180);
    // One deliberate disconnect during the task; the server owns the turn.
    ui(
        () -> {
          c().gateway.close();
          c().gateway = null;
          c().connectSaved();
        });
    until(() -> c().gateway != null && c().gateway.online() && !c().connecting, 90);
    until(
        () ->
            !c().running
                && !c().submissionPending
                && c().rows.stream()
                    .anyMatch(
                        r ->
                            r.optString("role").equals("assistant")
                                && Protocol.text(r).contains("LCB_HERMES_OK")),
        240);
    long userRows =
        c().rows.stream()
            .filter(r -> r.optString("role").equals("user") && Protocol.text(r).equals(prompt))
            .count();
    assertEquals("prompt must not be replayed", 1, userRows);
    String stored = c().storedId;
    assertFalse(stored.isEmpty());
    CountDownLatch list = new CountDownLatch(1);
    ui(
        () ->
            c().list(
                    (v, e) -> {
                      failure[0] = e;
                      list.countDown();
                    }));
    assertTrue(list.await(30, TimeUnit.SECONDS));
    assertNull(failure[0]);
    assertTrue(c().sessions.length() > 0);
    capture("phone-live-light");
    ui(() -> c().theme("dark"));
    capture("phone-live-dark");
    // Approval must remain pending until a person chooses a permitted response.
    JSONObject approval =
        obj(
            "id",
            "test-approval",
            "method",
            "approval",
            "params",
            obj(
                "session_id",
                c().sessionId,
                "command",
                "test command (not executed)",
                "description",
                "Test approval dialog",
                "choices",
                new JSONArray().put("once").put("deny")));
    ui(() -> c().request(approval));
    device.wait(Until.hasObject(By.text("allow this action?")), 5000);
    assertNotNull(device.findObject(By.text(java.util.regex.Pattern.compile("(?i)allow once"))));
    assertEquals(1, c().requests.size());
    capture("phone-approval");
    device.findObject(By.text(java.util.regex.Pattern.compile("(?i)deny"))).click();
    until(() -> c().requests.isEmpty(), 10);
    ui(() -> c().setDraft("offline draft test"));
    until(() -> c().draft.equals("offline draft test"), 5);
    ui(
        () -> {
          c().gateway.close();
          c().gateway = null;
          c().status = "offline";
          c().changed(false);
        });
    assertEquals("offline draft test", c().draft);
    ui(() -> c().connectSaved());
    until(() -> c().gateway != null && c().gateway.online() && c().storedId.equals(stored), 90);
    CountDownLatch files = new CountDownLatch(1);
    ui(
        () ->
            c().gateway
                .rest(
                    "/api/files",
                    null,
                    (v, e) -> {
                      failure[0] = e;
                      files.countDown();
                    }));
    assertTrue(files.await(30, TimeUnit.SECONDS));
    assertNull(failure[0]);
    ui(
        () -> {
          c().setDraft("");
          c().theme("light");
        });
    capture("phone-final");
  }

  @Test
  public void filesModelsClarificationAndDrafts() throws Exception {
    until(() -> c().loaded, 15);
    Assume.assumeTrue("signed-in emulator required", c().profile() != null);
    until(() -> c().gateway != null && c().gateway.online(), 60);
    String path = "uploads/lcb-client-test-" + java.util.UUID.randomUUID() + ".txt";
    byte[] body = "lcb-hermes file roundtrip\n".getBytes(StandardCharsets.UTF_8);
    CountDownLatch uploaded = new CountDownLatch(1);
    String[] failure = {null};
    String[] remote = {null};
    ui(
        () ->
            c().gateway
                .upload(
                    path,
                    "client-test.txt",
                    body,
                    (v, e) -> {
                      failure[0] = e;
                      if (v != null) remote[0] = v.optString("path");
                      uploaded.countDown();
                    }));
    assertTrue(uploaded.await(30, TimeUnit.SECONDS));
    assertNull(failure[0]);
    assertNotNull(remote[0]);
    CountDownLatch read = new CountDownLatch(1);
    byte[][] copy = {null};
    ui(
        () ->
            c().gateway
                .rest(
                    "/api/files/read?path=" + android.net.Uri.encode(remote[0]),
                    null,
                    (v, e) -> {
                      failure[0] = e;
                      if (v != null) {
                        String data = v.optString("data_url");
                        copy[0] =
                            android.util.Base64.decode(
                                data.substring(data.indexOf(',') + 1), android.util.Base64.DEFAULT);
                      }
                      read.countDown();
                    }));
    assertTrue(read.await(30, TimeUnit.SECONDS));
    assertNull(failure[0]);
    assertArrayEquals(body, copy[0]);
    // Uploads cannot silently replace an existing file.
    CountDownLatch duplicate = new CountDownLatch(1);
    ui(
        () ->
            c().gateway
                .upload(
                    remote[0],
                    "client-test.txt",
                    body,
                    (v, e) -> {
                      failure[0] = e;
                      duplicate.countDown();
                    }));
    assertTrue(duplicate.await(30, TimeUnit.SECONDS));
    assertNotNull(failure[0]);
    CountDownLatch models = new CountDownLatch(1);
    ui(
        () ->
            c().gateway
                .rpc(
                    "model.options",
                    obj("include_unconfigured", false),
                    (v, e) -> {
                      failure[0] = e;
                      if (v != null) assertTrue(v.optJSONArray("providers").length() > 0);
                      models.countDown();
                    }));
    assertTrue(models.await(30, TimeUnit.SECONDS));
    assertNull(failure[0]);
    JSONObject question =
        obj(
            "id",
            "test-clarify",
            "method",
            "clarify",
            "params",
            obj(
                "session_id",
                c().sessionId,
                "question",
                "Test question",
                "choices",
                new JSONArray().put("first").put("second")));
    ui(() -> c().request(question));
    assertTrue(device.wait(Until.hasObject(By.text("Test question")), 5000));
    device.findObject(By.text("second")).click();
    device.findObject(By.text(java.util.regex.Pattern.compile("(?i)answer"))).click();
    until(() -> c().requests.isEmpty(), 10);
    ui(() -> c().setDraft("rotation draft"));
    device.setOrientationLeft();
    Thread.sleep(1200);
    assertNotNull(device.findObject(By.desc("message Hermes")));
    capture("phone-landscape");
    device.setOrientationNatural();
    device.unfreezeRotation();
    Thread.sleep(1200);
    assertEquals("rotation draft", c().draft);
    Thread.sleep(800);
    JSONObject saved = new SecureStore(activity.getActivity()).read();
    JSONArray profiles = saved.getJSONArray("profiles");
    boolean found = false;
    for (int i = 0; i < profiles.length(); i++)
      if (profiles.getJSONObject(i).optString("id").equals(c().profileId)) {
        assertEquals("rotation draft", profiles.getJSONObject(i).optString("draft"));
        found = true;
      }
    assertTrue(found);
    ui(() -> c().setDraft(""));
    device.findObject(By.text("server")).click();
    Thread.sleep(1200);
    capture("phone-server");
    device.findObject(By.text("chat")).click();
  }

  @Test
  public void demoAndRotationRemainNative() throws Exception {
    until(() -> c().loaded, 15);
    ui(() -> c().demo());
    capture("phone-demo");
    device.setOrientationLeft();
    Thread.sleep(1500);
    assertNotNull(device.findObject(By.text("lcb-hermes")));
    device.setOrientationNatural();
    device.unfreezeRotation();
  }
}
