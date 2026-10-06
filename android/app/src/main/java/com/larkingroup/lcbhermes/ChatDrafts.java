package com.larkingroup.lcbhermes;

import static com.larkingroup.lcbhermes.Protocol.obj;

import org.json.*;

final class ChatDrafts {
  private static String key(String id) {
    return id.isEmpty() ? "@new" : id;
  }

  static JSONObject read(JSONObject profile, String id) {
    if (profile == null) return obj();
    JSONObject all = profile.optJSONObject("chat_drafts");
    JSONObject value = all == null ? null : all.optJSONObject(key(id));
    if (value != null) return value;
    if (all == null && id.equals(profile.optString("session")))
      return obj(
          "text",
          profile.optString("draft"),
          "attachments",
          profile.optJSONArray("draft_attachments"));
    return obj();
  }

  static void write(JSONObject profile, String id, String text, JSONArray attachments) {
    if (profile == null) return;
    try {
      JSONObject all = profile.optJSONObject("chat_drafts");
      if (all == null) {
        all = obj();
        profile.put("chat_drafts", all);
      }
      if (text.isEmpty() && attachments.length() == 0) all.remove(key(id));
      else all.put(key(id), obj("text", text, "attachments", attachments));
    } catch (JSONException ignored) {
    }
  }

  static void move(JSONObject profile, String from, String to) {
    if (profile == null || from.equals(to)) return;
    JSONObject settings = profile.optJSONObject("chat_settings");
    if (settings != null && settings.has(key(from))) {
      try {
        settings.put(key(to), settings.opt(key(from)));
      } catch (JSONException ignored) {
      }
      settings.remove(key(from));
    }
    JSONObject value = read(profile, from);
    if (!value.has("text") && !value.has("attachments")) return;
    JSONObject existing = read(profile, to);
    if ((!existing.optString("text").isEmpty()
            || (existing.optJSONArray("attachments") != null
                && existing.optJSONArray("attachments").length() > 0))
        && !existing.toString().equals(value.toString())) {
      try {
        JSONArray retained = profile.optJSONArray("retained_drafts");
        if (retained == null) {
          retained = new JSONArray();
          profile.put("retained_drafts", retained);
        }
        retained.put(obj("chat", to, "draft", existing));
      } catch (JSONException ignored) {
      }
    }
    JSONArray attachments = value.optJSONArray("attachments");
    write(
        profile, to, value.optString("text"), attachments == null ? new JSONArray() : attachments);
    write(profile, from, "", new JSONArray());
  }

  static void remove(JSONObject profile, String id) {
    if (profile == null) return;
    JSONObject all = profile.optJSONObject("chat_drafts");
    if (all != null) all.remove(key(id));
  }
}
