package com.larkingroup.lcbhermes;

import static com.larkingroup.lcbhermes.Protocol.obj;
import static org.junit.Assert.*;

import android.app.NotificationManager;
import android.content.*;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Color;
import android.os.*;
import android.service.notification.StatusBarNotification;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
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
    InstrumentationRegistry.getInstrumentation().waitForIdleSync();
    device.waitForIdle();
    Thread.sleep(300);
    File dir = new File(activity.getActivity().getFilesDir(), "captures");
    dir.mkdirs();
    assertTrue(device.takeScreenshot(new File(dir, name + ".png")));
  }

  private Button button(View view, String name) {
    if (view instanceof Button && ((Button) view).getText().toString().equals(name))
      return (Button) view;
    if (view instanceof ViewGroup)
      for (int i = 0; i < ((ViewGroup) view).getChildCount(); i++) {
        Button found = button(((ViewGroup) view).getChildAt(i), name);
        if (found != null) return found;
      }
    return null;
  }

  private View described(View view, String name) {
    if (name.contentEquals(
        view.getContentDescription() == null ? "" : view.getContentDescription())) return view;
    if (view instanceof ViewGroup)
      for (int i = 0; i < ((ViewGroup) view).getChildCount(); i++) {
        View found = described(((ViewGroup) view).getChildAt(i), name);
        if (found != null) return found;
      }
    return null;
  }

  @Test
  public void backgroundChatSurvivesRestore() throws Exception {
    Assume.assumeTrue("emulator only", Build.MODEL.contains("sdk"));
    until(() -> c().loaded, 15);
    ui(
        () -> {
          c().demo();
          c().data =
              obj(
                  "profiles",
                  new JSONArray().put(obj("id", "local-task-state")),
                  "active",
                  "local-task-state",
                  "theme",
                  "light");
          c().profileId = "local-task-state";
          c().sessionId = "alpha-runtime";
          c().storedId = "alpha-stored";
          c().title = "alpha";
          c().rows.clear();
          c().rows.add(obj("role", "user", "text", "alpha task"));
          c().event(obj("type", "message.start", "session_id", "alpha-runtime", "payload", obj()));
          c().setDraft("alpha draft");
          c().newChat();
          c().setDraft("new chat draft");
          c().save();
        });
    c().disk.submit(() -> {}).get(10, TimeUnit.SECONDS);
    JSONObject saved = c().store.read();
    ui(
        () -> {
          c().close();
          c().data = saved;
          c().restore();
        });
    assertEquals("new chat draft", c().draft);
    assertEquals("", c().storedId);
    assertTrue("saved background task is watched after process restoration", c().anyRunning());
    assertEquals("alpha draft", ChatDrafts.read(c().profile(), "alpha-stored").optString("text"));
    ui(
        () -> {
          c().event(
                  obj(
                      "type",
                      "session.info",
                      "payload",
                      obj("stored_session_id", "unrelated", "title", "wrong")));
          c().request(
                  obj(
                      "id",
                      "alpha-question",
                      "method",
                      "clarify",
                      "params",
                      obj("session_id", "alpha-runtime", "question", "which file?")));
        });
    assertEquals("", c().storedId);
    assertTrue("a background question cannot enter the current chat", c().requests.isEmpty());
    assertEquals("answer needed", c().chatBadge("alpha-stored"));
    ui(
        () ->
            c().event(
                    obj(
                        "type",
                        "message.complete",
                        "session_id",
                        "alpha-runtime",
                        "payload",
                        obj("text", "alpha reply", "status", "completed"))));
    assertFalse(c().anyRunning());
    assertEquals("reply", c().chatBadge("alpha-stored"));
    assertEquals("new chat draft", c().draft);
    assertEquals("", c().storedId);
    assertFalse(c().export().contains("alpha reply"));
    ui(() -> c().demo());
  }

  @Test
  public void sidebarPreview() throws Exception {
    Assume.assumeTrue("emulator only", Build.MODEL.contains("sdk"));
    until(() -> c().loaded, 15);
    ui(
        () -> {
          c().demo();
          c().theme("light");
          c().storedId = "preview-docs";
          c().sessionId = "preview-docs-runtime";
          c().title = "a little help";
          c().sessions =
              new JSONArray()
                  .put(obj("id", "preview-docs", "title", "a little help"))
                  .put(obj("id", "preview-web", "title", "read the web"))
                  .put(obj("id", "preview-build", "title", "build something small"))
                  .put(obj("id", "preview-notes", "title", "notes for later"))
                  .put(obj("id", "preview-photos", "title", "look at these photos"))
                  .put(obj("id", "preview-terminal", "title", "a bit of terminal work"));
          c().changed(true);
        });
    assertTrue(device.wait(Until.hasObject(By.desc("message Hermes")), 5000));
    boolean wide = activity.getActivity().getResources().getConfiguration().screenWidthDp >= 600;
    if (!wide) device.findObject(By.desc("choose chat")).click();
    assertTrue(device.wait(Until.hasObject(By.desc("find a chat")), 3000));
    assertNotNull(device.findObject(By.desc("a little help, open chat")));
    capture(wide ? "sidebar-fold-light" : "sidebar-phone-light");
    device.findObject(By.desc("find a chat")).setText("photos");
    assertTrue(device.wait(Until.hasObject(By.desc("look at these photos, open chat")), 3000));
    assertNull(device.findObject(By.desc("read the web, open chat")));
    device.findObject(By.desc("find a chat")).setText("");
    boolean[] ime = {false};
    ui(
        () ->
            ime[0] =
                activity
                    .getActivity()
                    .getWindow()
                    .getDecorView()
                    .getRootWindowInsets()
                    .isVisible(android.view.WindowInsets.Type.ime()));
    if (ime[0]) device.pressBack();
    if (!wide) {
      backGesture();
      assertTrue(device.wait(Until.gone(By.desc("find a chat")), 3000));
      assertTrue(device.wait(Until.hasObject(By.desc("message Hermes")), 3000));
      capture("sidebar-phone-chat");
      device.findObject(By.desc("choose chat")).click();
    }
    ui(() -> c().theme("dark"));
    assertTrue(device.wait(Until.hasObject(By.desc("find a chat")), 3000));
    capture(wide ? "sidebar-fold-dark" : "sidebar-phone-dark");
    ui(() -> c().theme("light"));
  }

  @Test
  public void sidebarKeepsSeparateChatsAndRoutesReplies() throws Exception {
    Assume.assumeTrue("emulator only", Build.MODEL.contains("sdk"));
    until(() -> c().loaded, 15);
    File fixture = new File(activity.getActivity().getFilesDir(), "fixture.json");
    Assume.assumeTrue(fixture.exists());
    JSONObject access =
        new JSONObject(
            new String(java.nio.file.Files.readAllBytes(fixture.toPath()), StandardCharsets.UTF_8));
    assertTrue(fixture.delete());
    ui(() -> c().demo());
    CountDownLatch login = new CountDownLatch(1);
    String[] error = {null};
    ui(
        () ->
            c().login(
                    "Hermes",
                    access.optString("url"),
                    access.optString("username"),
                    access.optString("password"),
                    true,
                    (v, e) -> {
                      error[0] = e;
                      login.countDown();
                    }));
    assertTrue(login.await(30, TimeUnit.SECONDS));
    assertNull(error[0]);
    until(
        () -> c().gateway != null && c().gateway.online() && !c().status.equals("loading chat"),
        20);
    InstrumentationRegistry.getInstrumentation()
        .getUiAutomation()
        .executeShellCommand(
            "pm grant com.larkingroup.lcbhermes android.permission.POST_NOTIFICATIONS")
        .close();
    NotificationManager manager =
        activity.getActivity().getSystemService(NotificationManager.class);
    manager.cancelAll();
    ui(
        () -> {
          c().newChat();
          c().setDraft("");
          c().attachments = new JSONArray();
          c().save();
        });
    String[] alpha = {""}, beta = {""};
    try {
      ui(
          () ->
              c().submit(
                      "Use the terminal to run sleep 15, then reply exactly SIDEBAR_ALPHA_OK. Do"
                          + " not do anything else."));
      until(() -> c().running && !c().submissionPending && !c().storedId.isEmpty(), 35);
      alpha[0] = c().storedId;
      ui(
          () -> {
            c().setDraft("next for alpha");
            c().addAttachment(
                    obj(
                        "id",
                        "alpha-file",
                        "name",
                        "alpha.txt",
                        "mime",
                        "text/plain",
                        "path",
                        "alpha.txt"));
          });
      device.findObject(By.desc("new chat")).click();
      until(() -> c().storedId.isEmpty(), 5);
      assertEquals("", c().draft);
      assertEquals(0, c().attachments.length());
      assertEquals("next for alpha", ChatDrafts.read(c().profile(), alpha[0]).optString("text"));
      assertTrue("leaving the chat keeps its task alive", c().anyRunning());
      ui(() -> c().submit("Reply exactly SIDEBAR_BETA_OK. Do not use any tools."));
      until(() -> !c().storedId.isEmpty() && !c().submissionPending, 30);
      beta[0] = c().storedId;
      assertNotEquals(alpha[0], beta[0]);
      ui(
          () -> {
            c().setDraft("next for beta");
            c().gateway.close();
            c().connectSaved();
          });
      until(() -> c().gateway.online() && !c().status.equals("loading chat"), 25);
      device.findObject(By.desc("server and connection")).click();
      device.findObject(By.desc("about lcb-hermes")).click();
      backGesture();
      backGesture();
      assertTrue(device.wait(Until.hasObject(By.desc("message Hermes")), 5000));
      assertEquals(beta[0], c().storedId);
      assertEquals("next for beta", c().draft);
      until(() -> !c().anyRunning(), 120);
      assertEquals("reply", c().chatBadge(alpha[0]));
      assertEquals(beta[0], c().storedId);
      assertFalse(
          "the other reply cannot enter this transcript",
          c().export().contains("SIDEBAR_ALPHA_OK"));
      StatusBarNotification reply = null;
      for (StatusBarNotification n : manager.getActiveNotifications())
        if (n.getId() == 2 && n.getTag().endsWith(":" + alpha[0])) reply = n;
      assertNotNull("a reply notification belongs to alpha", reply);
      reply.getNotification().contentIntent.send();
      until(() -> c().storedId.equals(alpha[0]) && !c().status.equals("loading chat"), 20);
      assertEquals("next for alpha", c().draft);
      assertEquals(1, c().attachments.length());
      assertTrue(c().export().contains("SIDEBAR_ALPHA_OK"));
      assertEquals("", c().chatBadge(alpha[0]));
      ui(() -> c().resume(beta[0]));
      until(() -> c().storedId.equals(beta[0]) && !c().status.equals("loading chat"), 20);
      assertEquals("next for beta", c().draft);
      assertEquals(0, c().attachments.length());
      ui(() -> activity.getActivity().recreate());
      assertTrue(device.wait(Until.hasObject(By.desc("message Hermes")), 5000));
      assertEquals(beta[0], c().storedId);
      assertEquals("next for beta", c().draft);
      capture("sidebar-live-separate-chats");
    } finally {
      ui(
          () -> {
            if (!alpha[0].isEmpty()) c().stop(alpha[0]);
            if (!beta[0].isEmpty()) c().stop(beta[0]);
          });
    }
  }

  @Test
  public void serverBackKeepsWorkingChat() throws Exception {
    Assume.assumeTrue("emulator only", Build.MODEL.contains("sdk"));
    until(() -> c().loaded, 15);
    ui(() -> c().demo());
    // A notification or launcher entry must not create a second, stale chat screen.
    ui(
        () ->
            activity
                .getActivity()
                .startActivity(new Intent(activity.getActivity(), MainActivity.class)));
    assertTrue(device.wait(Until.hasObject(By.desc("message Hermes")), 5000));
    ui(
        () -> {
          c().newChat();
          c().sessionId = "current-runtime";
          c().storedId = "current-stored";
          c().rows.add(obj("role", "user", "text", "current task"));
          c().changed(true);
          c().event(obj("type", "message.start", "session_id", c().sessionId, "payload", obj()));
        });
    assertTrue(device.wait(Until.hasObject(By.text("current task")), 5000));
    device.findObject(By.desc("server and connection")).click();
    if (device.findObject(By.desc("about lcb-hermes")) != null)
      device.findObject(By.desc("about lcb-hermes")).click();
    else {
      device.findObject(By.desc("settings")).click();
      device.findObject(By.text("about")).click();
    }
    assertTrue(
        device.wait(
            Until.hasObject(By.text(java.util.regex.Pattern.compile("lcb-hermes 0\\..*"))), 5000));
    device.pressBack();
    device.pressBack();
    try {
      assertTrue(
          "back must return to the current chat, not an older activity",
          device.wait(Until.hasObject(By.text("current task")), 5000));
      assertEquals("current-runtime", c().sessionId);
      assertTrue("navigation must not stop the active task", c().running);
      ui(
          () -> {
            c().event(
                    obj(
                        "type",
                        "session.info",
                        "session_id",
                        "old-runtime",
                        "payload",
                        obj("stored_session_id", "old-stored", "title", "old chat")));
            c().event(
                    obj(
                        "type",
                        "session.info",
                        "payload",
                        obj("stored_session_id", "old-stored", "title", "old chat")));
            c().request(
                    obj(
                        "id",
                        "old-request",
                        "method",
                        "clarify",
                        "params",
                        obj("session_id", "old-runtime", "question", "old question")));
          });
      assertEquals("current-stored", c().storedId);
      assertTrue(c().requests.isEmpty());
      device.findObject(By.desc("server and connection")).click();
      device.findObject(By.desc("about lcb-hermes")).click();
      assertTrue(device.wait(Until.hasObject(By.text("lcb-hermes 0.3.0")), 3000));
      backGesture();
      assertTrue(device.wait(Until.gone(By.text("lcb-hermes 0.3.0")), 3000));
      assertNotNull(device.findObject(By.desc("about lcb-hermes")));
      backGesture();
      assertTrue(
          "edge back must return to the working chat",
          device.wait(Until.hasObject(By.text("current task")), 5000));
      android.app.ActivityManager manager =
          activity.getActivity().getSystemService(android.app.ActivityManager.class);
      assertEquals(
          "only one chat activity", 1, manager.getAppTasks().get(0).getTaskInfo().numActivities);
      capture("server-back-current-chat");
    } finally {
      ui(
          () ->
              c().event(
                      obj(
                          "type",
                          "message.complete",
                          "session_id",
                          "current-runtime",
                          "payload",
                          obj("status", "interrupted"))));
    }
  }

  private void backGesture() throws Exception {
    assertTrue(
        device.swipe(
            12,
            device.getDisplayHeight() / 2,
            device.getDisplayWidth() * 2 / 3,
            device.getDisplayHeight() / 2,
            60));
    device.waitForIdle();
  }

  @Test
  public void serverStatsAndModulesKeepDraft() throws Exception {
    Assume.assumeTrue("emulator only", Build.MODEL.contains("sdk"));
    until(() -> c().loaded, 15);
    File fixture = new File(activity.getActivity().getFilesDir(), "fixture.json");
    Assume.assumeTrue(fixture.exists());
    JSONObject access =
        new JSONObject(
            new String(java.nio.file.Files.readAllBytes(fixture.toPath()), StandardCharsets.UTF_8));
    assertTrue(fixture.delete());
    CountDownLatch login = new CountDownLatch(1);
    String[] error = {null};
    ui(
        () ->
            c().login(
                    "Hermes",
                    access.optString("url"),
                    access.optString("username"),
                    access.optString("password"),
                    true,
                    (v, e) -> {
                      error[0] = e;
                      login.countDown();
                    }));
    assertTrue(login.await(30, TimeUnit.SECONDS));
    assertNull(error[0]);
    until(() -> c().gateway != null && c().gateway.online(), 15);
    ui(
        () -> {
          c().newChat();
          c().setDraft("keep this draft");
        });
    device.findObject(By.desc("server and connection")).click();
    assertTrue(device.wait(Until.hasObject(By.text("CPU")), 10000));
    assertNotNull(device.findObject(By.desc("about lcb-hermes")));
    assertNotNull(device.findObject(By.desc("toggle light or dark")));
    capture("server-modules-light");
    device.findObject(By.desc("toggle light or dark")).click();
    assertTrue(device.wait(Until.hasObject(By.text("CPU")), 10000));
    capture("server-modules-dark");
    device.findObject(By.desc("toggle light or dark")).click();
    device.findObject(By.desc("about lcb-hermes")).click();
    backGesture();
    backGesture();
    assertTrue(device.wait(Until.hasObject(By.desc("message Hermes")), 5000));
    assertEquals("keep this draft", c().draft);
    assertEquals("", c().storedId);
    device.findObject(By.desc("choose chat")).click();
    assertTrue(device.wait(Until.hasObject(By.text("find a chat")), 3000));
    backGesture();
    assertTrue(device.wait(Until.gone(By.text("find a chat")), 3000));
    assertEquals("keep this draft", c().draft);
  }

  @Test
  public void chatDesignAndImageAttachment() throws Exception {
    Assume.assumeTrue("emulator only", Build.MODEL.contains("sdk"));
    until(() -> c().loaded, 15);
    File fixture = new File(activity.getActivity().getFilesDir(), "fixture.json");
    Assume.assumeTrue("private test access required", fixture.exists());
    JSONObject access =
        new JSONObject(
            new String(java.nio.file.Files.readAllBytes(fixture.toPath()), StandardCharsets.UTF_8));
    assertTrue(fixture.delete());
    ui(() -> c().forget());
    assertTrue(device.wait(Until.hasObject(By.text("connect a server")), 5000));
    device.findObject(By.text("connect a server")).click();
    capture("design-connect-sheet");
    device.pressBack();
    CountDownLatch login = new CountDownLatch(1);
    String[] failure = {null};
    ui(
        () ->
            c().login(
                    "Hermes",
                    access.optString("url"),
                    access.optString("username"),
                    access.optString("password"),
                    true,
                    (v, e) -> {
                      failure[0] = e;
                      login.countDown();
                    }));
    assertTrue(login.await(30, TimeUnit.SECONDS));
    assertNull(failure[0]);
    until(() -> c().gateway != null && c().gateway.online(), 15);
    assertNull("file explorer navigation removed", device.findObject(By.text("files")));
    assertNotNull(device.findObject(By.desc("server and connection")));
    device.findObject(By.desc("choose chat")).click();
    assertTrue(device.wait(Until.hasObject(By.text("find a chat")), 3000));
    capture("design-inline-sessions");
    device.findObject(By.desc("choose chat")).click();
    ui(
        () -> {
          View model = described(activity.getActivity().getWindow().getDecorView(), "choose model");
          assertNotNull(model);
          for (int i = 0; i < 3; i++) model.performClick();
        });
    assertTrue(device.wait(Until.hasObject(By.text("model for a new chat")), 10000));
    capture("design-model-sheet");
    device.pressBack();
    assertTrue(device.wait(Until.gone(By.text("model for a new chat")), 3000));
    ui(() -> c().newChat());
    Bitmap image = Bitmap.createBitmap(320, 160, Bitmap.Config.ARGB_8888);
    android.graphics.Canvas canvas = new android.graphics.Canvas(image);
    android.graphics.Paint paint = new android.graphics.Paint();
    paint.setColor(Color.RED);
    canvas.drawRect(0, 0, 160, 160, paint);
    paint.setColor(Color.BLUE);
    canvas.drawRect(160, 0, 320, 160, paint);
    ByteArrayOutputStream out = new ByteArrayOutputStream();
    image.compress(Bitmap.CompressFormat.PNG, 100, out);
    File photo = new File(activity.getActivity().getCacheDir(), "orientation.jpg");
    try (FileOutputStream file = new FileOutputStream(photo)) {
      image.compress(Bitmap.CompressFormat.JPEG, 90, file);
    }
    android.media.ExifInterface exif = new android.media.ExifInterface(photo.toString());
    exif.setAttribute(android.media.ExifInterface.TAG_ORIENTATION, "6");
    exif.saveAttributes();
    Bitmap oriented =
        Attachments.bitmap(
            obj(
                "thumbnail",
                Attachments.thumbnail(
                    java.nio.file.Files.readAllBytes(photo.toPath()), "image/jpeg")));
    assertNotNull(oriented);
    assertEquals(160, oriented.getWidth());
    assertEquals(320, oriented.getHeight());
    oriented.recycle();
    assertTrue(photo.delete());
    image.recycle();
    byte[] bytes = out.toByteArray();
    String thumb = Attachments.thumbnail(bytes, "image/png");
    assertFalse(thumb.isEmpty());
    String target = "uploads/phone-image-" + java.util.UUID.randomUUID() + ".png";
    CountDownLatch upload = new CountDownLatch(1);
    String[] remote = {null};
    ui(
        () ->
            c().gateway
                .upload(
                    target,
                    "colours.png",
                    bytes,
                    (v, e) -> {
                      failure[0] = e;
                      if (v != null) remote[0] = v.optString("path");
                      upload.countDown();
                    }));
    assertTrue(upload.await(30, TimeUnit.SECONDS));
    assertNull(failure[0]);
    JSONObject attachment =
        obj(
            "id",
            "image-test",
            "name",
            "colours.png",
            "mime",
            "image/png",
            "path",
            remote[0],
            "thumbnail",
            thumb);
    ui(
        () -> {
          c().addAttachment(attachment);
          c().setDraft("What are the two colours in this image? Answer briefly. Do not use tools.");
        });
    assertTrue(device.wait(Until.hasObject(By.desc("attachment preview: colours.png")), 5000));
    capture("design-image-draft");
    device.setOrientationLeft();
    Thread.sleep(700);
    device.setOrientationNatural();
    device.unfreezeRotation();
    Thread.sleep(700);
    assertEquals(1, c().attachments.length());
    assertFalse(c().draft.contains(remote[0]));
    // Remove and re-add without losing the draft or leaking a queued image to the server.
    ui(() -> c().removeAttachment("image-test"));
    assertEquals(0, c().attachments.length());
    ui(() -> c().addAttachment(attachment));
    ui(() -> c().submit(c().draft));
    until(
        () ->
            !c().running
                && !c().submissionPending
                && c().rows.stream()
                    .anyMatch(
                        r ->
                            r.optString("role").equals("assistant")
                                && Protocol.text(r).toLowerCase().contains("red")
                                && Protocol.text(r).toLowerCase().contains("blue")),
        90);
    assertEquals(0, c().attachments.length());
    assertNotNull(device.findObject(By.desc("attached image: colours.png")));
    assertFalse(
        "image must not be submitted as path prose",
        Protocol.text(c().rows.get(0)).contains("Attached file"));
    String stored = c().storedId;
    ui(() -> c().newChat());
    ui(() -> c().resume(stored));
    until(
        () ->
            c().status.equals("connected")
                && c().rows.stream()
                    .anyMatch(
                        r ->
                            r.optJSONArray("attachments") != null
                                && r.optJSONArray("attachments").length() == 1),
        20);
    assertNotNull(device.findObject(By.desc("attached image: colours.png")));
    capture("design-chat-light");
    ui(() -> c().theme("dark"));
    capture("design-chat-dark");
    ui(() -> c().theme("light"));
    device.findObject(By.desc("server and connection")).click();
    assertNotNull(device.findObject(By.text("open dashboard")));
    capture("design-server");
    device.findObject(By.desc("back to chat")).click();
  }

  @Test
  public void phoneFixes() throws Exception {
    Assume.assumeTrue("emulator only", Build.MODEL.contains("sdk"));
    File fixture = new File(activity.getActivity().getFilesDir(), "fixture.json");
    Assume.assumeTrue("private test access required", fixture.exists());
    JSONObject access =
        new JSONObject(
            new String(java.nio.file.Files.readAllBytes(fixture.toPath()), StandardCharsets.UTF_8));
    assertTrue(fixture.delete());
    until(() -> c().loaded, 15);
    ui(() -> c().forget());
    device.findObject(By.text("connect a server")).click();
    assertTrue(device.wait(Until.hasObject(By.text("connect a server")), 5000));
    capture("phone-connect-screenshot");
    Bitmap screenshot =
        BitmapFactory.decodeFile(
            new File(activity.getActivity().getFilesDir(), "captures/phone-connect-screenshot.png")
                .toString());
    assertNotEquals(
        "login screenshot must not be blocked",
        Color.BLACK,
        screenshot.getPixel(screenshot.getWidth() / 2, screenshot.getHeight() / 2));
    screenshot.recycle();
    device.findObject(By.text(java.util.regex.Pattern.compile("(?i)cancel"))).click();
    if (Build.VERSION.SDK_INT >= 33)
      device.executeShellCommand(
          "pm grant com.larkingroup.lcbhermes android.permission.POST_NOTIFICATIONS");
    CountDownLatch login = new CountDownLatch(1);
    String[] failure = {null};
    ui(
        () ->
            c().login(
                    "Test server",
                    access.optString("url").replaceFirst("^http:", "https:"),
                    access.optString("username"),
                    access.optString("password"),
                    true,
                    (v, e) -> {
                      failure[0] = e;
                      login.countDown();
                    }));
    assertTrue("protocol recovery login timeout", login.await(45, TimeUnit.SECONDS));
    assertNull(failure[0]);
    assertTrue(c().profile().optString("url").startsWith("http:"));
    until(() -> c().gateway != null && c().gateway.online(), 15);
    ui(
        () -> {
          View model = described(activity.getActivity().getWindow().getDecorView(), "choose model");
          assertNotNull(model);
          for (int i = 0; i < 3; i++) model.performClick();
        });
    assertTrue(device.wait(Until.hasObject(By.text("model for a new chat")), 10000));
    device.pressBack();
    assertTrue(
        "one back press must close the only picker",
        device.wait(Until.gone(By.text("model for a new chat")), 3000));
    NotificationManager manager =
        activity.getActivity().getSystemService(NotificationManager.class);
    manager.cancel(2);
    ui(
        () -> {
          c().newChat();
          c().submit("Reply exactly LCB_PHONE_REPLY_OK. Do not use tools.");
        });
    until(() -> c().running, 15);
    device.pressHome();
    until(
        () ->
            !c().running
                && c().rows.stream().anyMatch(r -> Protocol.text(r).contains("LCB_PHONE_REPLY_OK")),
        90);
    StatusBarNotification[] replies = manager.getActiveNotifications();
    StatusBarNotification reply =
        java.util.Arrays.stream(replies)
            .filter(n -> n.getId() == 2)
            .findFirst()
            .orElseThrow(() -> new AssertionError("completion notification missing"));
    assertEquals("replies", reply.getNotification().getChannelId());
    assertEquals("Hermes replied", reply.getNotification().extras.getString("android.title"));
    assertTrue(
        reply
            .getNotification()
            .extras
            .getCharSequence("android.text")
            .toString()
            .contains("LCB_PHONE_REPLY_OK"));
    assertEquals(
        NotificationManager.IMPORTANCE_DEFAULT,
        manager.getNotificationChannel("replies").getImportance());
    reply.getNotification().contentIntent.send();
    assertTrue(device.wait(Until.hasObject(By.text("lcb-hermes")), 5000));
    capture("phone-compact-light");
    ui(() -> c().theme("dark"));
    capture("phone-compact-dark");
    ui(() -> c().theme("light"));
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
    device.findObject(By.desc("server and connection")).click();
    Thread.sleep(1200);
    capture("phone-server");
    device.findObject(By.desc("back to chat")).click();
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
