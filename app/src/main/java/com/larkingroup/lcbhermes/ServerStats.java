package com.larkingroup.lcbhermes;

import java.util.LinkedHashMap;
import java.util.Locale;
import org.json.JSONArray;
import org.json.JSONObject;

final class ServerStats {
  static LinkedHashMap<String, LinkedHashMap<String, String>> sections(JSONObject stats, long now) {
    LinkedHashMap<String, LinkedHashMap<String, String>> result = new LinkedHashMap<>();
    LinkedHashMap<String, String> cpu = new LinkedHashMap<>();
    if (number(stats, "cpu_percent")) cpu.put("Usage", percent(stats.optDouble("cpu_percent")));
    if (number(stats, "cpu_count"))
      cpu.put("Logical CPUs", String.valueOf(stats.optInt("cpu_count")));
    JSONArray load = stats.optJSONArray("load_avg");
    if (load != null && load.length() >= 3)
      cpu.put(
          "Load · 1 / 5 / 15 min",
          String.format(
              Locale.ROOT,
              "%.2f / %.2f / %.2f",
              load.optDouble(0),
              load.optDouble(1),
              load.optDouble(2)));
    result.put("CPU", cpu);
    JSONObject memory = stats.optJSONObject("memory");
    if (memory != null) {
      LinkedHashMap<String, String> rows = new LinkedHashMap<>();
      pair(rows, memory, "Used / total", "used", "total");
      bytes(rows, memory, "Available", "available");
      if (number(memory, "percent")) rows.put("Usage", percent(memory.optDouble("percent")));
      result.put("System memory", rows);
    }
    JSONObject process = stats.optJSONObject("process");
    if (process != null) {
      LinkedHashMap<String, String> rows = new LinkedHashMap<>();
      bytes(rows, process, "Memory", "rss");
      if (number(process, "num_threads"))
        rows.put("Threads", String.valueOf(process.optInt("num_threads")));
      if (number(process, "pid")) rows.put("PID", String.valueOf(process.optInt("pid")));
      if (number(process, "create_time"))
        rows.put("Running for", duration(Math.max(0, now - process.optLong("create_time"))));
      result.put("Hermes process", rows);
    }
    JSONObject disk = stats.optJSONObject("disk");
    if (disk != null) {
      LinkedHashMap<String, String> rows = new LinkedHashMap<>();
      pair(rows, disk, "Used / total", "used", "total");
      bytes(rows, disk, "Free", "free");
      if (number(disk, "percent")) rows.put("Usage", percent(disk.optDouble("percent")));
      result.put("Data volume", rows);
    }
    LinkedHashMap<String, String> system = new LinkedHashMap<>();
    value(system, stats, "Hermes", "hermes_version");
    value(system, stats, "Hostname", "hostname");
    value(system, stats, "OS", "os");
    value(system, stats, "Release", "os_release");
    value(system, stats, "Architecture", "arch");
    value(system, stats, "Python", "python_version");
    if (number(stats, "uptime_seconds"))
      system.put("System uptime", duration(stats.optLong("uptime_seconds")));
    result.put("System", system);
    result.entrySet().removeIf(e -> e.getValue().isEmpty());
    return result;
  }

  static boolean number(JSONObject object, String key) {
    return object.opt(key) instanceof Number;
  }

  private static void value(
      LinkedHashMap<String, String> rows, JSONObject source, String label, String key) {
    String value = source.optString(key);
    if (!value.isEmpty() && !source.isNull(key)) rows.put(label, value);
  }

  private static void bytes(
      LinkedHashMap<String, String> rows, JSONObject source, String label, String key) {
    if (number(source, key)) rows.put(label, size(source.optDouble(key)));
  }

  private static void pair(
      LinkedHashMap<String, String> rows, JSONObject source, String label, String a, String b) {
    if (number(source, a) && number(source, b))
      rows.put(label, size(source.optDouble(a)) + " / " + size(source.optDouble(b)));
  }

  static String size(double bytes) {
    String[] units = {"B", "KiB", "MiB", "GiB", "TiB", "PiB"};
    int unit = 0;
    while (bytes >= 1024 && unit < units.length - 1) {
      bytes /= 1024;
      unit++;
    }
    return String.format(Locale.ROOT, unit == 0 ? "%.0f %s" : "%.1f %s", bytes, units[unit]);
  }

  static String percent(double value) {
    return String.format(Locale.ROOT, "%.1f%%", value);
  }

  static String duration(long seconds) {
    seconds = Math.max(0, seconds);
    if (seconds >= 86400) return (seconds / 86400) + "d " + ((seconds / 3600) % 24) + "h";
    if (seconds >= 3600) return (seconds / 3600) + "h " + ((seconds / 60) % 60) + "m";
    return (seconds / 60) + "m " + (seconds % 60) + "s";
  }

  private ServerStats() {}
}
