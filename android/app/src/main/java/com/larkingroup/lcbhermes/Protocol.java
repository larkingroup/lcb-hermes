package com.larkingroup.lcbhermes;

import java.util.ArrayList;
import java.util.List;
import org.json.JSONObject;

final class Protocol {
  static List<JSONObject> frames(String frame) throws Exception {
    if (frame.length() > 8 * 1024 * 1024)
      throw new IllegalArgumentException("Server frame is too large");
    List<JSONObject> messages = new ArrayList<>();
    for (String line : frame.split("\n"))
      if (!line.trim().isEmpty()) messages.add(new JSONObject(line));
    return messages;
  }

  static JSONObject obj(Object... pairs) {
    JSONObject o = new JSONObject();
    try {
      for (int i = 0; i < pairs.length; i += 2) o.put((String) pairs[i], pairs[i + 1]);
    } catch (Exception e) {
      throw new IllegalArgumentException(e);
    }
    return o;
  }

  static String text(JSONObject row) {
    if (row.has("text") && !row.isNull("text")) return row.optString("text");
    Object content = row.opt("content");
    if (content instanceof String) return (String) content;
    if (content instanceof org.json.JSONArray) {
      StringBuilder s = new StringBuilder();
      org.json.JSONArray a = (org.json.JSONArray) content;
      for (int i = 0; i < a.length(); i++) {
        JSONObject p = a.optJSONObject(i);
        if (p != null && p.has("text")) s.append(p.optString("text")).append('\n');
      }
      return s.toString().trim();
    }
    return "";
  }

  private Protocol() {}
}
