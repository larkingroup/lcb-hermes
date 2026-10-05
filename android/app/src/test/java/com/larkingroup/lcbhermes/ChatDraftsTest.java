package com.larkingroup.lcbhermes;

import static com.larkingroup.lcbhermes.Protocol.obj;
import static org.junit.Assert.*;

import org.json.*;
import org.junit.Test;

public class ChatDraftsTest {
  @Test
  public void upgradesTheExistingDraftWithItsImages() {
    JSONArray image = new JSONArray().put(obj("id", "photo", "preview", "saved thumb"));
    JSONObject old = obj("session", "a", "draft", "keep me", "draft_attachments", image);
    JSONObject restored = ChatDrafts.read(old, "a");
    assertEquals("keep me", restored.optString("text"));
    assertEquals(image.toString(), restored.optJSONArray("attachments").toString());
    ChatDrafts.write(old, "a", restored.optString("text"), restored.optJSONArray("attachments"));
    ChatDrafts.write(old, "b", "another draft", new JSONArray());
    assertEquals("keep me", ChatDrafts.read(old, "a").optString("text"));
    assertEquals("another draft", ChatDrafts.read(old, "b").optString("text"));
    assertEquals("", ChatDrafts.read(old, "").optString("text"));
  }

  @Test
  public void unsentChatMovesToItsServerIdWithoutDuplicatingAttachments() {
    JSONObject profile = obj();
    JSONArray image = new JSONArray().put(obj("id", "photo"));
    ChatDrafts.write(profile, "", "look at this", image);
    ChatDrafts.move(profile, "", "created");
    assertEquals("look at this", ChatDrafts.read(profile, "created").optString("text"));
    assertEquals(1, ChatDrafts.read(profile, "created").optJSONArray("attachments").length());
    assertEquals("", ChatDrafts.read(profile, "").optString("text"));
    ChatDrafts.write(profile, "created", "", new JSONArray());
    assertEquals("", ChatDrafts.read(profile, "created").optString("text"));
    ChatDrafts.write(profile, "other", "untouched", new JSONArray());
    ChatDrafts.remove(profile, "created");
    assertEquals("untouched", ChatDrafts.read(profile, "other").optString("text"));
  }
}
