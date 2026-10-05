package com.larkingroup.lcbhermes;

import static com.larkingroup.lcbhermes.Protocol.obj;
import static org.junit.Assert.*;

import org.json.JSONArray;
import org.junit.Test;

public class ServerStatsTest {
  @Test
  public void absentMetricsDoNotBecomeZeroReadings() {
    var sections =
        ServerStats.sections(obj("hermes_version", "test", "memory", obj("total", 1024)), 100);
    assertFalse(sections.containsKey("CPU"));
    assertFalse(sections.containsKey("System memory"));
    assertEquals("test", sections.get("System").get("Hermes"));
  }

  @Test
  public void hostAndProcessMemoryStayDistinct() throws Exception {
    var sections =
        ServerStats.sections(
            obj(
                "cpu_percent",
                0,
                "load_avg",
                new JSONArray().put(1.2).put(2.3).put(3.4),
                "memory",
                obj("used", 1073741824L, "total", 8589934592L, "available", 7516192768L),
                "process",
                obj("rss", 134217728, "create_time", 100)),
            3700);
    assertEquals("0.0%", sections.get("CPU").get("Usage"));
    assertEquals("1.20 / 2.30 / 3.40", sections.get("CPU").get("Load · 1 / 5 / 15 min"));
    assertEquals("1.0 GiB / 8.0 GiB", sections.get("System memory").get("Used / total"));
    assertEquals("128.0 MiB", sections.get("Hermes process").get("Memory"));
    assertEquals("1h 0m", sections.get("Hermes process").get("Running for"));
  }
}
