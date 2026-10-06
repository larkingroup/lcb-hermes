package com.larkingroup.lcbhermes;

import static com.larkingroup.lcbhermes.Protocol.obj;

import java.text.DateFormat;
import java.text.SimpleDateFormat;
import java.util.*;
import org.json.*;

/** Presentation of authoritative Hermes catalogs; titles are never session identities. */
final class Workbench {
  static String canonical(JSONObject result) {
    JSONObject info = result.optJSONObject("info");
    String id = info == null ? "" : info.optString("stored_session_id");
    for (String key : new String[] {"stored_session_id", "session_key", "session_id"})
      if (id.isEmpty()) id = result.optString(key);
    return id;
  }

  static JSONArray catalog(JSONArray source, JSONObject profile) {
    LinkedHashMap<String, JSONObject> unique = new LinkedHashMap<>();
    Map<String, String> aliases = new HashMap<>();
    if (source != null)
      for (int i = 0; i < source.length(); i++) {
        JSONObject row = source.optJSONObject(i);
        if (row != null && !row.optString("resolved_id").isEmpty())
          aliases.put(row.optString("resolved_id"), row.optString("id"));
      }
    if (source != null)
      for (int i = 0; i < source.length(); i++) {
        JSONObject row = source.optJSONObject(i);
        if (row == null || row.optString("id").isEmpty()) continue;
        String id = aliases.getOrDefault(row.optString("id"), row.optString("id"));
        if (!unique.containsKey(id) || id.equals(row.optString("id"))) {
          try {
            JSONObject copy = new JSONObject(row.toString());
            copy.put("id", id);
            unique.put(id, copy);
          } catch (JSONException ignored) {
          }
        }
      }
    List<JSONObject> rows = new ArrayList<>(unique.values());
    JSONObject completed = profile == null ? null : profile.optJSONObject("completed_order");
    if (completed != null)
      rows.sort(
          (a, b) ->
              Long.compare(
                  completed.optLong(b.optString("id")), completed.optLong(a.optString("id"))));
    return new JSONArray(rows);
  }

  static String date(JSONObject row, boolean full) {
    double seconds = row.optDouble("started_at", 0);
    if (seconds <= 0 || Double.isNaN(seconds)) return "";
    Date date = new Date((long) (seconds * 1000));
    return full
        ? DateFormat.getDateTimeInstance().format(date)
        : new SimpleDateFormat("MMM d · HH:mm", Locale.getDefault()).format(date);
  }

  static void tree(
      Object value,
      String path,
      String label,
      Map<String, String> chats,
      LinkedHashMap<String, String> folders) {
    if (value instanceof JSONArray) {
      JSONArray array = (JSONArray) value;
      for (int i = 0; i < array.length(); i++) tree(array.opt(i), path, label, chats, folders);
    } else if (value instanceof JSONObject) {
      JSONObject node = (JSONObject) value;
      if (node.optBoolean("archived")) return;
      String nextPath = node.optString("path");
      if (!nextPath.isEmpty()) path = nextPath;
      String nextLabel = node.optString("label", node.optString("name"));
      if (!nextLabel.isEmpty()) label = nextLabel;
      if (!path.isEmpty()) folders.put(path, label.isEmpty() ? path : label + " · " + path);
      if (node.has("started_at") || node.has("message_count") || node.has("title")) {
        String id = node.optString("id");
        String cwd = node.optString("cwd", path);
        if (!id.isEmpty() && !cwd.isEmpty()) chats.put(id, cwd);
      }
      for (String key :
          new String[] {
            "tree", "projects", "repos", "groups", "folders", "previewSessions", "sessions"
          }) tree(node.opt(key), path, label, chats, folders);
    }
  }

  static List<JSONObject> models(JSONObject options, String defaultModel) {
    List<JSONObject> all = new ArrayList<>();
    JSONArray providers = options.optJSONArray("providers");
    if (providers == null) return all;
    for (int i = 0; i < providers.length(); i++) {
      JSONObject p = providers.optJSONObject(i);
      if (p == null || !(p.optBoolean("authenticated") || p.optBoolean("is_current"))) continue;
      if (p.optBoolean("is_current"))
        all.add(
            obj(
                "model",
                "",
                "label",
                "Server default: " + defaultModel,
                "provider",
                p.optString("slug"),
                "catalog",
                p));
      JSONArray ms = p.optJSONArray("models");
      if (ms == null) continue;
      for (int j = 0; j < ms.length(); j++) {
        Object raw = ms.opt(j);
        String m = raw instanceof JSONObject ? ((JSONObject) raw).optString("id") : ms.optString(j);
        if (!m.isEmpty())
          all.add(obj("model", m, "label", m, "provider", p.optString("slug"), "catalog", p));
      }
    }
    if (all.stream().noneMatch(r -> r.optString("model").isEmpty()))
      all.add(0, obj("model", "", "label", "Server default: " + defaultModel, "provider", ""));
    return all;
  }

  static List<String> efforts(JSONObject choice, String defaultModel) {
    ArrayList<String> levels = new ArrayList<>(Collections.singletonList(""));
    String model = choice.optString("model");
    if (model.isEmpty()) model = defaultModel;
    JSONObject catalog = choice.optJSONObject("catalog");
    JSONObject caps = catalog == null ? null : catalog.optJSONObject("capabilities");
    caps = caps == null ? null : caps.optJSONObject(model);
    if (caps != null && caps.has("reasoning") && !caps.optBoolean("reasoning")) return levels;
    String slug = choice.optString("provider");
    if (slug.equals("openai") || slug.equals("openai-codex")) {
      boolean noDisable = model.startsWith("gpt-6-astra") || model.startsWith("gpt-6.1-sol");
      boolean modern =
          noDisable
              || model.startsWith("gpt-5.6")
              || model.startsWith("gpt-6-sol")
              || model.startsWith("gpt-6-luna")
              || model.startsWith("gpt-daybreak");
      if (!noDisable) levels.add("none");
      levels.addAll(Arrays.asList("low", "medium", "high", "xhigh"));
      if (modern) levels.add("max");
    } else levels.addAll(Arrays.asList("none", "minimal", "low", "medium", "high", "xhigh", "max"));
    return levels;
  }

  static String modelValue(String model, String provider, String effort) {
    if (!model.matches("[A-Za-z0-9_./:+@-]*")
        || !provider.matches("[A-Za-z0-9_-]*")
        || !effort.matches("|none|minimal|low|medium|high|xhigh|max"))
      throw new IllegalArgumentException("Unsupported model setting.");
    return (model.isEmpty() ? "default" : model)
        + (provider.isEmpty() ? "" : " --provider " + provider)
        + (effort.isEmpty() ? "" : " --reasoning " + effort)
        + " --session";
  }

  private Workbench() {}
}
