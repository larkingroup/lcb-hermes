package com.larkingroup.lcbhermes;

import static com.larkingroup.lcbhermes.Protocol.obj;

import android.Manifest;
import android.app.*;
import android.content.*;
import android.content.pm.PackageManager;
import android.database.Cursor;
import android.graphics.*;
import android.graphics.drawable.*;
import android.net.Uri;
import android.os.*;
import android.provider.OpenableColumns;
import android.speech.RecognizerIntent;
import android.text.*;
import android.view.*;
import android.widget.*;
import io.noties.markwon.*;
import io.noties.markwon.ext.tables.TablePlugin;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.util.*;
import org.json.*;

public final class MainActivity extends Activity implements Controller.Observer {
  Controller c;
  private LinearLayout root, content, transcript, toolsPanel;
  private TextView connection, heading, modelLabel, notice;
  private EditText composer;
  private Button send, approval;
  private ScrollView chatScroll;
  private final Map<JSONObject, TextView> messageViews = new IdentityHashMap<>();
  private final ArrayList<Button> tabs = new ArrayList<>();
  private Markwon markdown;
  private int face, paper, light, shadow, ink, muted, blue, blueInk;
  private String page = "chat", theme = "", filesPath = "", parentPath = "";
  private boolean syncingDraft, filesLoaded, needsScroll = true, dialogOpen;
  private boolean followTail = true;
  private byte[] pendingExport;
  private static final int PICK_FILE = 2, SAVE_FILE = 3, VOICE = 4;

  @Override
  public void onCreate(Bundle saved) {
    super.onCreate(saved);
    c = ((HermesApp) getApplication()).controller;
    if (saved != null) page = saved.getString("page", "chat");
    markdown =
        Markwon.builder(this)
            .usePlugin(TablePlugin.create(this))
            .usePlugin(
                new AbstractMarkwonPlugin() {
                  @Override
                  public void configureConfiguration(MarkwonConfiguration.Builder b) {
                    b.linkResolver((view, link) -> openLink(link));
                  }
                })
            .build();
    build();
    c.attach(this);
  }

  @Override
  protected void onSaveInstanceState(Bundle b) {
    super.onSaveInstanceState(b);
    b.putString("page", page);
  }

  @Override
  protected void onDestroy() {
    c.detach(this);
    super.onDestroy();
  }

  @Override
  protected void onResume() {
    super.onResume();
    if (c.loaded && !c.demo && c.profile() != null) c.connectSaved();
  }

  @Override
  protected void onPause() {
    c.save();
    super.onPause();
  }

  int dp(float n) {
    return (int) (getResources().getDisplayMetrics().density * n + 0.5f);
  }

  private int color(String hex) {
    return Color.parseColor(hex);
  }

  private boolean compact() {
    return getResources().getConfiguration().screenHeightDp < 500;
  }

  private int gutter() {
    return dp(8 + Math.max(0, (getResources().getConfiguration().screenWidthDp - 720) / 2f));
  }

  private void colors() {
    theme = c.data.optString("theme", "light");
    boolean dark = theme.equals("dark");
    face = color(dark ? "#282e32" : "#ece9d8");
    paper = color(dark ? "#171e22" : "#ffffff");
    light = color(dark ? "#505b61" : "#faf9f1");
    shadow = color(dark ? "#101619" : "#999a91");
    ink = color(dark ? "#e1e5e7" : "#2f373a");
    muted = color(dark ? "#a6b3ba" : "#677173");
    blue = color(dark ? "#354c5c" : "#c0d3e0");
    blueInk = color(dark ? "#d4e4ed" : "#314c60");
  }

  private LinearLayout column() {
    LinearLayout v = new LinearLayout(this);
    v.setOrientation(LinearLayout.VERTICAL);
    return v;
  }

  private LinearLayout row() {
    LinearLayout v = new LinearLayout(this);
    v.setOrientation(LinearLayout.HORIZONTAL);
    v.setGravity(Gravity.CENTER_VERTICAL);
    return v;
  }

  private TextView text(String value, int size, int col) {
    TextView v = new TextView(this);
    v.setText(value);
    v.setTextSize(size);
    v.setTextColor(col);
    v.setPadding(dp(10), dp(6), dp(10), dp(6));
    v.setIncludeFontPadding(false);
    return v;
  }

  private TextView label(String value) {
    TextView v = text(value, 12, muted);
    v.setTypeface(Typeface.MONOSPACE);
    return v;
  }

  private Drawable bevel(int color, boolean inset) {
    return new Bevel(color, light, shadow, inset);
  }

  private Button button(String value, Runnable click) {
    Button b = new Button(this);
    b.setText(value);
    b.setAllCaps(false);
    b.setTextSize(14);
    b.setTextColor(ink);
    b.setMinWidth(0);
    b.setMinimumWidth(0);
    b.setMinHeight(dp(48));
    b.setMinimumHeight(dp(48));
    b.setPadding(dp(14), dp(4), dp(14), dp(4));
    StateListDrawable bg = new StateListDrawable();
    bg.addState(new int[] {android.R.attr.state_pressed}, bevel(face, true));
    bg.addState(new int[] {}, bevel(face, false));
    b.setBackground(bg);
    b.setOnClickListener(v -> click.run());
    return b;
  }

  private void weighted(LinearLayout parent, View view) {
    parent.addView(view, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1));
  }

  private void add(LinearLayout parent, View view) {
    parent.addView(
        view,
        new LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
  }

  private void gap(LinearLayout parent, int height) {
    View v = new View(this);
    parent.addView(v, new LinearLayout.LayoutParams(1, dp(height)));
  }

  private void build() {
    colors();
    root = column();
    root.setBackgroundColor(face);
    root.setPadding(gutter(), 0, gutter(), 0);
    setContentView(root);
    if (Build.VERSION.SDK_INT >= 30) getWindow().setDecorFitsSystemWindows(false);
    if (Build.VERSION.SDK_INT >= 30)
      root.setOnApplyWindowInsetsListener(
          (v, insets) -> {
            Insets bars =
                insets.getInsets(
                    WindowInsets.Type.systemBars() | WindowInsets.Type.displayCutout());
            Insets ime = insets.getInsets(WindowInsets.Type.ime());
            root.setPadding(
                gutter() + bars.left,
                bars.top,
                gutter() + bars.right,
                Math.max(bars.bottom, ime.bottom));
            return insets;
          });
    if (Build.VERSION.SDK_INT >= 30)
      getWindow()
          .getDecorView()
          .getWindowInsetsController()
          .setSystemBarsAppearance(
              theme.equals("dark")
                  ? 0
                  : WindowInsetsController.APPEARANCE_LIGHT_STATUS_BARS
                      | WindowInsetsController.APPEARANCE_LIGHT_NAVIGATION_BARS,
              WindowInsetsController.APPEARANCE_LIGHT_STATUS_BARS
                  | WindowInsetsController.APPEARANCE_LIGHT_NAVIGATION_BARS);
    LinearLayout bar = row();
    bar.setPadding(0, dp(6), 0, dp(4));
    ImageView logo = new ImageView(this);
    logo.setImageResource(R.drawable.lcb_logo);
    logo.setBackgroundColor(color("#faf9f1"));
    logo.setPadding(dp(2), dp(2), dp(2), dp(2));
    logo.setContentDescription("LCB");
    bar.addView(logo, new LinearLayout.LayoutParams(dp(42), dp(42)));
    LinearLayout names = compact() ? row() : column();
    TextView app = text("lcb-hermes", 19, ink);
    app.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
    names.addView(app);
    connection = label("offline");
    connection.setSingleLine(true);
    connection.setEllipsize(TextUtils.TruncateAt.END);
    names.addView(connection);
    bar.addView(names, new LinearLayout.LayoutParams(0, -2, 1));
    Button menu = button("···", this::menu);
    menu.setContentDescription("settings and connection");
    bar.addView(menu);
    add(root, bar);
    content = column();
    root.addView(content, new LinearLayout.LayoutParams(-1, 0, 1));
    LinearLayout tabbar = row();
    tabs.clear();
    for (String name : new String[] {"chat", "sessions", "files", "server"}) {
      Button b = button(name, () -> show(name));
      tabs.add(b);
      weighted(tabbar, b);
    }
    add(root, tabbar);
    gap(root, 4);
    show(page);
  }

  private void show(String name) {
    page = name;
    content.removeAllViews();
    composer = null;
    messageViews.clear();
    notice = null;
    for (Button b : tabs) {
      b.setBackground(
          bevel(
              b.getText().toString().equals(name) ? blue : face,
              b.getText().toString().equals(name)));
      b.setTextColor(b.getText().toString().equals(name) ? blueInk : ink);
    }
    switch (name) {
      case "sessions":
        sessions();
        break;
      case "files":
        files();
        break;
      case "server":
        server();
        break;
      default:
        chat();
    }
    update(false);
  }

  private void chat() {
    LinearLayout top = row();
    top.setBackground(bevel(blue, false));
    heading = text(c.title, 14, blueInk);
    heading.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
    heading.setSingleLine(true);
    heading.setEllipsize(TextUtils.TruncateAt.END);
    weighted(top, heading);
    Button fresh =
        button(
            "+",
            () -> {
              c.newChat();
              needsScroll = true;
            });
    fresh.setContentDescription("new chat");
    top.addView(fresh);
    add(content, top);
    LinearLayout info = row();
    modelLabel = label(c.model.isEmpty() ? "server default" : c.model);
    weighted(info, modelLabel);
    Button choose = button("model", this::models);
    if (compact()) top.addView(choose);
    else {
      info.addView(choose);
      add(content, info);
    }
    chatScroll = new ScrollView(this);
    chatScroll.setOnScrollChangeListener(
        (view, x, y, oldX, oldY) -> {
          if (y != oldY && chatScroll.getChildCount() > 0)
            followTail =
                chatScroll.getChildAt(0).getHeight() - chatScroll.getHeight() - y < dp(100);
        });
    chatScroll.setFillViewport(true);
    chatScroll.setBackground(bevel(paper, true));
    transcript = column();
    transcript.setPadding(dp(8), dp(8), dp(8), dp(8));
    chatScroll.addView(transcript);
    content.addView(chatScroll, new LinearLayout.LayoutParams(-1, 0, 1));
    notice = label("");
    add(content, notice);
    approval = button("answer request", this::pendingRequest);
    add(content, approval);
    LinearLayout composeBox = column();
    composeBox.setPadding(dp(6), dp(6), dp(6), dp(6));
    composeBox.setBackground(bevel(face, false));
    composer = new EditText(this);
    composer.setTextColor(ink);
    composer.setHintTextColor(muted);
    composer.setHint("message Hermes…");
    composer.setTextSize(16);
    composer.setGravity(Gravity.TOP);
    composer.setMinLines(compact() ? 1 : 2);
    composer.setMaxLines(compact() ? 3 : 5);
    composer.setFilters(new InputFilter[] {new InputFilter.LengthFilter(100000)});
    composer.setPadding(dp(8), dp(8), dp(8), dp(8));
    composer.setBackground(bevel(paper, true));
    composer.setInputType(
        android.text.InputType.TYPE_CLASS_TEXT
            | android.text.InputType.TYPE_TEXT_FLAG_MULTI_LINE
            | android.text.InputType.TYPE_TEXT_FLAG_CAP_SENTENCES);
    composer.setImeOptions(android.view.inputmethod.EditorInfo.IME_FLAG_NO_EXTRACT_UI);
    composer.setContentDescription("message Hermes");
    composer.setText(c.draft);
    composer.addTextChangedListener(
        new TextWatcher() {
          public void beforeTextChanged(CharSequence s, int st, int count, int after) {}

          public void onTextChanged(CharSequence s, int st, int before, int count) {
            if (!syncingDraft) c.setDraft(s.toString());
          }

          public void afterTextChanged(Editable e) {}
        });
    add(composeBox, composer);
    LinearLayout actions = row();
    actions.addView(button("attach", () -> pickFile(true)));
    actions.addView(button("voice", this::voice));
    View spacer = new View(this);
    actions.addView(spacer, new LinearLayout.LayoutParams(0, 1, 1));
    send =
        button(
            c.running ? "stop" : "send",
            () -> {
              if (c.running) c.stop();
              else {
                notifications();
                needsScroll = true;
                c.submit(composer.getText().toString());
              }
            });
    actions.addView(send);
    add(composeBox, actions);
    add(content, composeBox);
    gap(content, 6);
    renderTranscript();
  }

  private void renderTranscript() {
    if (transcript == null || !page.equals("chat")) return;
    boolean bottom =
        chatScroll.getChildAt(0).getHeight() - chatScroll.getHeight() - chatScroll.getScrollY()
            < dp(120);
    transcript.removeAllViews();
    messageViews.clear();
    if (c.rows.isEmpty()) {
      LinearLayout welcome = column();
      welcome.setGravity(Gravity.CENTER);
      welcome.setPadding(dp(16), dp(32), dp(16), dp(32));
      ImageView logo = new ImageView(this);
      logo.setImageResource(R.drawable.lcb_logo);
      logo.setBackgroundColor(color("#faf9f1"));
      welcome.addView(logo, new LinearLayout.LayoutParams(dp(100), dp(100)));
      gap(welcome, 16);
      TextView ready = text("ready when you are.", 20, ink);
      ready.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
      welcome.addView(ready);
      welcome.addView(
          text(
              c.profile() == null
                  ? "Your phone. Your Hermes server."
                  : "Ask, browse, build. Hermes does the work.",
              15,
              muted));
      gap(welcome, 12);
      welcome.addView(
          button(
              c.profile() == null ? "connect a server" : "reconnect",
              () -> {
                if (c.profile() == null) loginDialog();
                else c.connectSaved();
              }));
      if (c.profile() == null) welcome.addView(button("look around", c::demo));
      add(transcript, welcome);
    }
    int shown = 0;
    for (int i = Math.max(0, c.rows.size() - 160); i < c.rows.size(); i++) {
      JSONObject item = c.rows.get(i);
      String role = item.optString("role"), body = Protocol.text(item);
      if (!(role.equals("user") || role.equals("assistant")) || body.trim().isEmpty()) continue;
      LinearLayout block = column();
      block.setBackground(bevel(role.equals("user") ? blue : paper, role.equals("assistant")));
      block.setPadding(dp(5), dp(5), dp(5), dp(5));
      LinearLayout meta = row();
      TextView who = label(role.equals("user") ? "you" : "hermes");
      who.setTextColor(role.equals("user") ? blueInk : muted);
      weighted(meta, who);
      TextView copy = text("copy", 12, muted);
      copy.setPadding(dp(12), dp(12), dp(12), dp(12));
      copy.setContentDescription("copy " + role + " message");
      copy.setOnClickListener(v -> copy(Protocol.text(item)));
      meta.addView(copy);
      add(block, meta);
      TextView bodyView = text(body, 16, ink);
      bodyView.setTextIsSelectable(true);
      bodyView.setLineSpacing(dp(3), 1);
      markdown.setMarkdown(bodyView, body);
      add(block, bodyView);
      messageViews.put(item, bodyView);
      add(transcript, block);
      gap(transcript, 10);
      shown++;
    }
    toolsPanel = column();
    if (!c.tools.isEmpty()) {
      TextView cap = label("tools · " + c.tools.size());
      add(toolsPanel, cap);
      for (JSONObject tool : c.tools.values()) {
        String name = tool.optString("name", "tool");
        String summary = tool.optString("summary", tool.optString("context", ""));
        if (summary.length() > 90) summary = summary.substring(0, 90) + "…";
        Button b =
            button(
                (tool.optBoolean("done") ? "✓ " : "· ")
                    + name
                    + (summary.isEmpty() ? "" : "   " + summary),
                () -> toolDetails(tool));
        b.setGravity(Gravity.START | Gravity.CENTER_VERTICAL);
        b.setTextSize(13);
        add(toolsPanel, b);
        gap(toolsPanel, 3);
      }
      add(transcript, toolsPanel);
    }
    if (c.running) {
      TextView working = label("· working…");
      add(transcript, working);
    }
    if (needsScroll || bottom || followTail) {
      chatScroll.post(() -> chatScroll.fullScroll(View.FOCUS_DOWN));
      needsScroll = false;
    }
  }

  @Override
  public void changed(boolean structure) {
    if (isFinishing() || isDestroyed()) return;
    if (!theme.equals(c.data.optString("theme", "light"))) {
      build();
      return;
    }
    update(structure);
  }

  private void update(boolean structure) {
    JSONObject p = c.profile();
    connection.setText(
        (p == null ? "no server" : p.optString("name"))
            + "  ·  "
            + (c.running ? "working" : c.status));
    if (!page.equals("chat")) return;
    heading.setText(c.title);
    modelLabel.setText(c.model.isEmpty() ? "server default" : c.model);
    if (notice != null) {
      String n = c.notice;
      notice.setText(n);
      notice.setVisibility(n.isEmpty() ? View.GONE : View.VISIBLE);
    }
    approval.setText("answer request · " + c.requests.size());
    approval.setVisibility(c.requests.isEmpty() ? View.GONE : View.VISIBLE);
    send.setText(c.running ? "stop" : "send");
    send.setEnabled(!c.submissionPending || c.running);
    if (composer != null
        && !composer.getText().toString().equals(c.draft)
        && !c.submissionPending) {
      syncingDraft = true;
      composer.setText(c.draft);
      composer.setSelection(composer.length());
      syncingDraft = false;
    }
    if (structure) renderTranscript();
    else
      for (Map.Entry<JSONObject, TextView> e : messageViews.entrySet()) {
        String value = Protocol.text(e.getKey());
        if (!value.equals(e.getValue().getTag())) {
          markdown.setMarkdown(e.getValue(), value);
          e.getValue().setTag(value);
        }
      }
    if (!structure && c.running && followTail)
      chatScroll.post(() -> chatScroll.fullScroll(View.FOCUS_DOWN));
  }

  private void menu() {
    String[] options = {
      c.gateway != null && c.gateway.online() ? "reconnect" : "connect",
      "servers",
      "light / dark",
      "export chat",
      "open dashboard",
      "forget this server",
      "about"
    };
    new AlertDialog.Builder(this)
        .setTitle("lcb-hermes")
        .setItems(
            options,
            (d, n) -> {
              switch (n) {
                case 0:
                  if (c.profile() == null) loginDialog();
                  else {
                    c.close();
                    c.connectSaved();
                  }
                  break;
                case 1:
                  profiles();
                  break;
                case 2:
                  c.theme(theme.equals("dark") ? "light" : "dark");
                  break;
                case 3:
                  exportChat();
                  break;
                case 4:
                  if (c.profile() != null) openLink(c.profile().optString("url"));
                  break;
                case 5:
                  if (c.profile() != null)
                    new AlertDialog.Builder(this)
                        .setTitle("forget this server?")
                        .setMessage(
                            "Remove its saved login, draft, and cached chat from this phone. Server"
                                + " chats stay there.")
                        .setNegativeButton("cancel", null)
                        .setPositiveButton("forget", (x, y) -> c.forget())
                        .show();
                  break;
                case 6:
                  new AlertDialog.Builder(this)
                      .setTitle("lcb-hermes 0.1.1")
                      .setMessage(
                          "A small client for Hermes.\n\n"
                              + "No ads. No purchases. No analytics.\n\n"
                              + "Messages go to your server. OpenAI stays there. Voice input uses"
                              + " Android's speech provider.")
                      .setPositiveButton("okay", null)
                      .setNeutralButton("licenses", (x, y) -> licenses())
                      .show();
              }
            })
        .show();
  }

  private EditText field(LinearLayout box, String hint, String value, boolean password) {
    EditText e = new EditText(this);
    e.setTextColor(ink);
    e.setHintTextColor(muted);
    e.setHint(hint);
    e.setText(value);
    e.setTextSize(16);
    e.setSingleLine();
    e.setInputType(
        password
            ? android.text.InputType.TYPE_CLASS_TEXT
                | android.text.InputType.TYPE_TEXT_VARIATION_PASSWORD
            : android.text.InputType.TYPE_CLASS_TEXT
                | android.text.InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS);
    e.setContentDescription(hint);
    e.setImportantForAutofill(
        password ? View.IMPORTANT_FOR_AUTOFILL_YES : View.IMPORTANT_FOR_AUTOFILL_NO);
    if (password) e.setAutofillHints(View.AUTOFILL_HINT_PASSWORD);
    e.setBackground(bevel(paper, true));
    e.setPadding(dp(10), dp(12), dp(10), dp(12));
    add(box, label(hint));
    add(box, e);
    gap(box, 6);
    return e;
  }

  private void licenses() {
    try (InputStream in = getAssets().open("notices.txt")) {
      ByteArrayOutputStream out = new ByteArrayOutputStream();
      byte[] buffer = new byte[8192];
      int n;
      while ((n = in.read(buffer)) != -1) out.write(buffer, 0, n);
      TextView text = text(out.toString("UTF-8"), 13, ink);
      text.setBackgroundColor(paper);
      ScrollView scroll = new ScrollView(this);
      scroll.addView(text);
      new AlertDialog.Builder(this)
          .setTitle("licenses")
          .setView(scroll)
          .setPositiveButton("close", null)
          .show();
    } catch (IOException e) {
      toast("Could not open the licenses.");
    }
  }

  private void loginDialog() {
    LinearLayout box = column();
    box.setBackgroundColor(face);
    box.setPadding(dp(16), dp(8), dp(16), dp(8));
    EditText name = field(box, "server name", "", false);
    JSONObject current = c.profile();
    EditText url =
        field(
            box,
            "dashboard URL",
            current == null ? "" : current.optString("url"),
            false);
    url.setInputType(
        android.text.InputType.TYPE_CLASS_TEXT | android.text.InputType.TYPE_TEXT_VARIATION_URI);
    EditText user =
        field(box, "username", current == null ? "" : current.optString("username"), false);
    EditText pass = field(box, "password", "", true);
    CheckBox http = new CheckBox(this);
    http.setText("allow HTTP on private addresses");
    http.setTextColor(ink);
    http.setChecked(current != null && current.optBoolean("http"));
    add(box, http);
    TextView explanation =
        text("Use your dashboard login. Model providers are configured on your server.", 13, muted);
    add(box, explanation);
    TextView error = text("", 13, color("#ba4b35"));
    add(box, error);
    ScrollView scroll = new ScrollView(this);
    scroll.addView(box);
    AlertDialog dialog =
        new AlertDialog.Builder(this)
            .setTitle("connect a server")
            .setView(scroll)
            .setNegativeButton("cancel", (d, w) -> pass.setText(""))
            .setPositiveButton("connect", null)
            .create();
    dialog.setOnShowListener(
        d -> {
          dialog.getWindow().addFlags(WindowManager.LayoutParams.FLAG_SECURE);
          dialog
              .getButton(-1)
              .setOnClickListener(
                  v -> {
                    String password = pass.getText().toString();
                    pass.setText("");
                    dialog.getButton(-1).setEnabled(false);
                    error.setText("connecting…");
                    c.login(
                        name.getText().toString(),
                        url.getText().toString(),
                        user.getText().toString(),
                        password,
                        http.isChecked(),
                        (result, err) -> {
                          if (isDestroyed()) return;
                          if (err != null) {
                            error.setText(err);
                            dialog.getButton(-1).setEnabled(true);
                          } else {
                            dialog.dismiss();
                            show("chat");
                          }
                        });
                  });
        });
    dialog.show();
  }

  private void profiles() {
    JSONArray list = c.profiles();
    String[] names = new String[list.length() + 1];
    for (int i = 0; i < list.length(); i++) names[i] = list.optJSONObject(i).optString("name");
    names[list.length()] = "+ add server";
    new AlertDialog.Builder(this)
        .setTitle("servers")
        .setItems(
            names,
            (d, n) -> {
              if (n == list.length()) loginDialog();
              else {
                c.select(list.optJSONObject(n).optString("id"));
                show("chat");
              }
            })
        .show();
  }

  private boolean connected() {
    if (c.gateway == null || !c.gateway.online()) {
      add(content, text("Connect to your server to load this view.", 16, ink));
      add(
          content,
          button(
              "connect",
              () -> {
                if (c.profile() == null) loginDialog();
                else c.connectSaved();
              }));
      return false;
    }
    return true;
  }

  private void sessions() {
    LinearLayout top = row();
    weighted(top, text("sessions", 18, ink));
    top.addView(button("refresh", () -> show("sessions")));
    add(content, top);
    if (!connected()) return;
    EditText search = new EditText(this);
    search.setTextColor(ink);
    search.setHintTextColor(muted);
    search.setHint("find a chat");
    search.setSingleLine();
    search.setBackground(bevel(paper, true));
    search.setPadding(dp(12), dp(10), dp(12), dp(10));
    add(content, search);
    gap(content, 8);
    ScrollView scroll = new ScrollView(this);
    LinearLayout list = column();
    scroll.addView(list);
    content.addView(scroll, new LinearLayout.LayoutParams(-1, 0, 1));
    TextView wait = label("loading…");
    add(list, wait);
    Runnable render =
        () -> {
          list.removeAllViews();
          String query = search.getText().toString().toLowerCase(Locale.ROOT);
          JSONArray all = c.sessions;
          if (all == null) return;
          int count = 0;
          for (int i = 0; i < all.length(); i++) {
            JSONObject session = all.optJSONObject(i);
            if (session == null) continue;
            String title = session.optString("title", "untitled"),
                preview = session.optString("preview");
            if (!(title + preview).toLowerCase(Locale.ROOT).contains(query)) continue;
            LinearLayout item = column();
            item.setBackground(bevel(paper, false));
            TextView titleView = text(title, 16, ink);
            titleView.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
            add(item, titleView);
            TextView previewView = text(preview, 13, muted);
            previewView.setMaxLines(2);
            add(item, previewView);
            add(
                item,
                label(
                    session.optInt("message_count")
                        + " messages · "
                        + session.optString("source")));
            item.setPadding(dp(4), dp(5), dp(4), dp(5));
            item.setFocusable(true);
            item.setContentDescription(title + ", open chat");
            item.setOnClickListener(
                v -> {
                  if (c.running) {
                    toast("Finish or stop the current task first.");
                    return;
                  }
                  c.save();
                  c.resume(session.optString("id"));
                  needsScroll = true;
                  show("chat");
                });
            item.setOnLongClickListener(
                v -> {
                  sessionMenu(session);
                  return true;
                });
            add(list, item);
            gap(list, 8);
            count++;
          }
          if (count == 0) add(list, text("no sessions yet.", 16, muted));
        };
    search.addTextChangedListener(
        new TextWatcher() {
          public void beforeTextChanged(CharSequence s, int st, int co, int a) {}

          public void onTextChanged(CharSequence s, int st, int b, int co) {
            render.run();
          }

          public void afterTextChanged(Editable e) {}
        });
    c.list(
        (v, e) -> {
          if (!page.equals("sessions") || isDestroyed()) return;
          if (e != null) {
            list.removeAllViews();
            add(list, text(e, 15, ink));
          } else render.run();
        });
  }

  private void sessionMenu(JSONObject session) {
    String id = session.optString("id");
    new AlertDialog.Builder(this)
        .setTitle(session.optString("title", "chat"))
        .setItems(
            new String[] {"rename", "delete"},
            (d, n) -> {
              if (n == 0) {
                LinearLayout box = column();
                box.setPadding(dp(16), 0, dp(16), 0);
                EditText name = field(box, "title", session.optString("title"), false);
                new AlertDialog.Builder(this)
                    .setTitle("rename chat")
                    .setView(box)
                    .setNegativeButton("cancel", null)
                    .setPositiveButton(
                        "save",
                        (x, y) ->
                            c.gateway.rpc(
                                "session.title",
                                obj("session_id", id, "title", name.getText().toString()),
                                (v, e) -> {
                                  if (e != null) toast(e);
                                  else show("sessions");
                                }))
                    .show();
              } else
                new AlertDialog.Builder(this)
                    .setTitle("delete this chat?")
                    .setMessage("This removes it from the server. It cannot be undone.")
                    .setNegativeButton("cancel", null)
                    .setPositiveButton(
                        "delete",
                        (x, y) ->
                            c.gateway.rpc(
                                "session.delete",
                                obj("session_id", id),
                                (v, e) -> {
                                  if (e != null) toast(e);
                                  else {
                                    if (id.equals(c.storedId)) c.newChat();
                                    show("sessions");
                                  }
                                }))
                    .show();
            })
        .show();
  }

  private void models() {
    if (c.gateway == null || !c.gateway.online()) {
      toast("Connect to select a model.");
      return;
    }
    c.gateway.rpc(
        "model.options",
        obj("include_unconfigured", false),
        (v, e) -> {
          if (e != null) {
            toast(e);
            return;
          }
          ArrayList<String> labels = new ArrayList<>(),
              models = new ArrayList<>(),
              providers = new ArrayList<>();
          labels.add("server default");
          models.add("");
          providers.add("");
          JSONArray ps = v.optJSONArray("providers");
          if (ps != null)
            for (int i = 0; i < ps.length(); i++) {
              JSONObject p = ps.optJSONObject(i);
              if (p == null || !p.optBoolean("authenticated", p.optBoolean("is_current"))) continue;
              JSONArray ms = p.optJSONArray("models");
              if (ms != null)
                for (int j = 0; j < ms.length(); j++) {
                  String m = ms.optString(j);
                  if (m.isEmpty()) continue;
                  labels.add(p.optString("name", p.optString("slug")) + " · " + m);
                  models.add(m);
                  providers.add(p.optString("slug"));
                }
            }
          new AlertDialog.Builder(this)
              .setTitle("model for a new chat")
              .setItems(
                  labels.toArray(new String[0]),
                  (d, n) -> {
                    if (c.running) {
                      toast("Finish this task first.");
                      return;
                    }
                    c.newModel = models.get(n);
                    c.newProvider = providers.get(n);
                    if (!c.sessionId.isEmpty() || !c.rows.isEmpty())
                      new AlertDialog.Builder(this)
                          .setTitle("start a new chat?")
                          .setMessage(
                              "Use "
                                  + labels.get(n)
                                  + " in a new session. This chat is saved on the server.")
                          .setNegativeButton("cancel", null)
                          .setPositiveButton(
                              "new chat",
                              (x, y) -> {
                                c.newChat();
                                show("chat");
                              })
                          .show();
                    else {
                      c.model = c.newModel;
                      update(false);
                    }
                  })
              .show();
        });
  }

  private void files() {
    LinearLayout bar = row();
    weighted(bar, text("files", 18, ink));
    bar.addView(button("upload", () -> pickFile(false)));
    bar.addView(button("refresh", () -> show("files")));
    add(content, bar);
    if (!connected()) return;
    TextView path = label(filesPath.isEmpty() ? "managed files" : filesPath);
    add(content, path);
    ScrollView scroll = new ScrollView(this);
    LinearLayout list = column();
    scroll.addView(list);
    content.addView(scroll, new LinearLayout.LayoutParams(-1, 0, 1));
    add(list, label("loading…"));
    String query = filesPath.isEmpty() ? "" : "?path=" + Uri.encode(filesPath);
    c.gateway.rest(
        "/api/files" + query,
        null,
        (v, e) -> {
          if (!page.equals("files") || isDestroyed()) return;
          list.removeAllViews();
          if (e != null) {
            add(list, text(e, 15, ink));
            return;
          }
          filesLoaded = true;
          filesPath = v.optString("path");
          parentPath = v.optString("parent");
          path.setText(filesPath);
          if (!parentPath.isEmpty() && !parentPath.equals("null"))
            add(
                list,
                button(
                    ".. parent",
                    () -> {
                      filesPath = parentPath;
                      show("files");
                    }));
          JSONArray entries = v.optJSONArray("entries");
          if (entries == null || entries.length() == 0)
            add(list, text("nothing here yet.", 16, muted));
          else
            for (int i = 0; i < entries.length(); i++) {
              JSONObject file = entries.optJSONObject(i);
              if (file == null) continue;
              boolean dir =
                  file.optBoolean(
                      "is_directory",
                      file.optBoolean("is_dir", file.optString("type").equals("directory")));
              Button b =
                  button(
                      (dir ? "▣  " : "□  ") + file.optString("name"),
                      () -> {
                        if (dir) {
                          filesPath = file.optString("path");
                          show("files");
                        } else readFile(file);
                      });
              b.setGravity(Gravity.START | Gravity.CENTER_VERTICAL);
              add(list, b);
              gap(list, 5);
            }
        });
  }

  private void readFile(JSONObject file) {
    long size = file.optLong("size");
    if (size > 20 * 1024 * 1024) {
      toast("This file is too large for the mobile preview. Use the dashboard.");
      return;
    }
    c.gateway.rest(
        "/api/files/read?path=" + Uri.encode(file.optString("path")),
        null,
        (v, e) -> {
          if (e != null) {
            toast(e);
            return;
          }
          try {
            String data = v.optString("data_url");
            int comma = data.indexOf(',');
            if (comma < 0) throw new IOException();
            byte[] bytes =
                android.util.Base64.decode(data.substring(comma + 1), android.util.Base64.DEFAULT);
            LinearLayout box = column();
            box.setPadding(dp(12), dp(12), dp(12), dp(12));
            box.setBackgroundColor(paper);
            String mime = v.optString("mime_type");
            if (mime.startsWith("image/") && !mime.contains("svg")) {
              BitmapFactory.Options o = new BitmapFactory.Options();
              o.inJustDecodeBounds = true;
              BitmapFactory.decodeByteArray(bytes, 0, bytes.length, o);
              o.inSampleSize = Math.max(1, Math.max(o.outWidth, o.outHeight) / 1600);
              o.inJustDecodeBounds = false;
              Bitmap bitmap = BitmapFactory.decodeByteArray(bytes, 0, bytes.length, o);
              ImageView image = new ImageView(this);
              image.setAdjustViewBounds(true);
              image.setImageBitmap(bitmap);
              add(box, image);
            } else if (mime.startsWith("text/")
                || mime.contains("json")
                || mime.contains("xml")
                || mime.contains("javascript")) {
              String t = new String(bytes, StandardCharsets.UTF_8);
              if (t.length() > 64000) t = t.substring(0, 64000) + "\n… preview truncated";
              TextView preview = text(t, 14, ink);
              preview.setTypeface(Typeface.MONOSPACE);
              preview.setTextIsSelectable(true);
              add(box, preview);
            } else add(box, text(mime + " · " + bytes.length + " bytes", 15, ink));
            ScrollView scroll = new ScrollView(this);
            scroll.addView(box);
            new AlertDialog.Builder(this)
                .setTitle(v.optString("name", "file"))
                .setView(scroll)
                .setNegativeButton("close", null)
                .setNeutralButton(
                    "attach to chat",
                    (x, y) -> {
                      appendDraft("Attached file on server: " + v.optString("path"));
                      show("chat");
                    })
                .setPositiveButton(
                    "save a copy", (x, y) -> saveAs(bytes, v.optString("name", "file"), mime))
                .show();
          } catch (Exception x) {
            toast("Could not preview this file.");
          }
        });
  }

  private boolean attachToChat;

  private void pickFile(boolean chat) {
    if (c.gateway == null || !c.gateway.online()) {
      toast("Connect before attaching files.");
      return;
    }
    attachToChat = chat;
    Intent i =
        new Intent(Intent.ACTION_OPEN_DOCUMENT)
            .setType("*/*")
            .addCategory(Intent.CATEGORY_OPENABLE);
    startActivityForResult(i, PICK_FILE);
  }

  private void upload(Uri uri) {
    String name = "attachment";
    try (Cursor q =
        getContentResolver()
            .query(uri, new String[] {OpenableColumns.DISPLAY_NAME}, null, null, null)) {
      if (q != null && q.moveToFirst()) name = q.getString(0);
    } catch (Exception ignored) {
    }
    name = name.replaceAll("[\\\\/\\p{Cntrl}]", "_");
    final String filename = name;
    boolean chat = attachToChat;
    String target =
        chat
            ? "uploads/" + System.currentTimeMillis() + "-" + filename
            : (filesPath.isEmpty() ? filename : filesPath + "/" + filename);
    toast("uploading…");
    c.disk.execute(
        () -> {
          try (InputStream in = getContentResolver().openInputStream(uri);
              ByteArrayOutputStream out = new ByteArrayOutputStream()) {
            if (in == null) throw new IOException();
            byte[] buf = new byte[8192];
            int count;
            while ((count = in.read(buf)) != -1) {
              out.write(buf, 0, count);
              if (out.size() > 20 * 1024 * 1024)
                throw new IOException("Mobile uploads are limited to 20 MB.");
            }
            byte[] bytes = out.toByteArray();
            runOnUiThread(
                () -> {
                  if (c.gateway == null || !c.gateway.online()) {
                    toast("Connection lost. Choose the file again after reconnecting.");
                    return;
                  }
                  c.gateway.upload(
                      target,
                      filename,
                      bytes,
                      (v, e) -> {
                        if (e != null) {
                          toast(e);
                          return;
                        }
                        toast("uploaded");
                        if (chat) {
                          appendDraft("Attached file on server: " + v.optString("path", target));
                          show("chat");
                        } else show("files");
                      });
                });
          } catch (Exception e) {
            runOnUiThread(
                () -> toast(e.getMessage() == null ? "Could not read that file." : e.getMessage()));
          }
        });
  }

  private void appendDraft(String value) {
    c.setDraft(c.draft + (c.draft.isEmpty() ? "" : "\n\n") + value);
    update(false);
  }

  private void server() {
    LinearLayout top = row();
    weighted(top, text("server", 18, ink));
    top.addView(button("refresh", () -> show("server")));
    add(content, top);
    if (!connected()) return;
    ScrollView scroll = new ScrollView(this);
    LinearLayout body = column();
    scroll.addView(body);
    content.addView(scroll, new LinearLayout.LayoutParams(-1, 0, 1));
    JSONObject p = c.profile();
    add(body, text(p.optString("name"), 18, ink));
    add(body, label(p.optString("url")));
    add(body, label("model · " + c.model));
    add(
        body,
        text(
            "The agent runs on your server. Browser, terminal, and other tools stay there.",
            15,
            muted));
    add(body, button("open dashboard", () -> openLink(p.optString("url"))));
    c.gateway.rest(
        "/api/system/stats",
        null,
        (v, e) -> {
          if (!page.equals("server") || isDestroyed()) return;
          if (e == null) {
            add(body, label("resources"));
            add(body, text(resourceSummary(v), 15, ink));
          } else add(body, label(e));
        });
    c.gateway.rest(
        "/api/cron/jobs",
        null,
        (v, e) -> {
          if (!page.equals("server") || isDestroyed()) return;
          add(body, label("scheduled jobs"));
          if (e != null) add(body, label(e));
          else {
            JSONArray jobs = v.optJSONArray("jobs");
            if (jobs == null || jobs.length() == 0) add(body, text("none yet.", 15, muted));
            else
              for (int n = 0; n < jobs.length(); n++) {
                JSONObject job = jobs.optJSONObject(n);
                if (job != null)
                  add(
                      body,
                      text(
                          job.optString("name", job.optString("id"))
                              + "\n"
                              + job.optString("schedule"),
                          15,
                          ink));
              }
          }
        });
  }

  private String resourceSummary(JSONObject v) {
    StringBuilder out = new StringBuilder("Hermes " + v.optString("hermes_version", "unknown"));
    if (v.has("cpu_percent"))
      out.append("\nCPU ").append(Math.round(v.optDouble("cpu_percent"))).append("%");
    JSONObject mem = v.optJSONObject("memory");
    if (mem != null)
      out.append(
          String.format(
              Locale.ROOT,
              "\nMemory %.1f / %.1f GB",
              mem.optDouble("used") / 1073741824,
              mem.optDouble("total") / 1073741824));
    JSONObject proc = v.optJSONObject("process");
    if (proc != null)
      out.append(
          String.format(Locale.ROOT, "\nHermes process %.0f MB", proc.optDouble("rss") / 1048576));
    if (v.has("uptime_seconds"))
      out.append("\nServer uptime ").append(v.optLong("uptime_seconds") / 3600).append("h");
    return out.toString();
  }

  private String pretty(JSONObject value) {
    try {
      return value.toString(2);
    } catch (Exception e) {
      return value.toString();
    }
  }

  private void toolDetails(JSONObject tool) {
    TextView details = text(pretty(tool), 13, ink);
    details.setTextIsSelectable(true);
    details.setTypeface(Typeface.MONOSPACE);
    details.setBackgroundColor(paper);
    ScrollView scroll = new ScrollView(this);
    scroll.addView(details);
    new AlertDialog.Builder(this)
        .setTitle(tool.optString("name", "tool"))
        .setView(scroll)
        .setPositiveButton("close", null)
        .show();
  }

  private void pendingRequest() {
    if (!c.requests.isEmpty()) request(c.requests.values().iterator().next());
  }

  @Override
  public void request(JSONObject r) {
    if (dialogOpen || isFinishing()) return;
    JSONObject p = r.optJSONObject("params");
    if (p == null) p = obj();
    String method = r.optString("method");
    if (method.equals("approval")) {
      dialogOpen = true;
      String command = p.optString("command"),
          description = p.optString("description", "Hermes is asking to run a command.");
      JSONObject params = p;
      AlertDialog.Builder b =
          new AlertDialog.Builder(this)
              .setTitle("allow this action?")
              .setMessage(description + (command.isEmpty() ? "" : "\n\n" + command))
              .setNegativeButton("deny", (d, w) -> c.answer(r, obj("choice", "deny")));
      JSONArray choices = params.optJSONArray("choices");
      boolean once = choices == null;
      if (choices != null)
        for (int i = 0; i < choices.length(); i++) once |= choices.optString(i).equals("once");
      if (once) b.setPositiveButton("allow once", (d, w) -> c.answer(r, obj("choice", "once")));
      AlertDialog dialog = b.create();
      dialog.setOnCancelListener(d -> c.answer(r, obj("choice", "deny")));
      dialog.setOnDismissListener(d -> dialogOpen = false);
      dialog.show();
    } else if (method.equals("clarify")) {
      JSONArray qs = p.optJSONArray("questions");
      if (qs == null) {
        qs = new JSONArray();
        qs.put(p);
      }
      clarify(r, qs, 0, obj());
    }
  }

  private void clarify(JSONObject request, JSONArray questions, int index, JSONObject answers) {
    if (index >= questions.length()) {
      c.answer(
          request,
          questions.length() == 1 && !questions.optJSONObject(0).has("qid")
              ? obj("answer", answers.optString("0"))
              : obj("answers", answers));
      return;
    }
    JSONObject q = questions.optJSONObject(index);
    if (q == null) {
      c.answer(request, obj("answer", ""));
      return;
    }
    dialogOpen = true;
    LinearLayout box = column();
    box.setPadding(dp(16), dp(8), dp(16), dp(8));
    add(box, text(q.optString("question", "Hermes has a question."), 16, ink));
    EditText answer = field(box, "your answer", "", false);
    JSONArray choices = q.optJSONArray("choices");
    if (choices != null)
      for (int i = 0; i < choices.length(); i++) {
        String value = choices.optString(i);
        add(
            box,
            button(
                value,
                () -> {
                  if (q.optBoolean("multi_select")) {
                    String current = answer.getText().toString();
                    answer.setText(current.isEmpty() ? value : current + ", " + value);
                  } else answer.setText(value);
                }));
      }
    ScrollView scroll = new ScrollView(this);
    scroll.addView(box);
    AlertDialog dialog =
        new AlertDialog.Builder(this)
            .setTitle(
                "Hermes asks"
                    + (questions.length() > 1
                        ? " · " + (index + 1) + "/" + questions.length()
                        : ""))
            .setView(scroll)
            .setNegativeButton(
                "skip",
                (d, w) -> {
                  try {
                    answers.put(q.optString("qid", String.valueOf(index)), "");
                  } catch (Exception ignored) {
                  }
                  mainClarify(request, questions, index, answers);
                })
            .setPositiveButton(
                "answer",
                (d, w) -> {
                  try {
                    answers.put(
                        q.optString("qid", String.valueOf(index)), answer.getText().toString());
                  } catch (Exception ignored) {
                  }
                  mainClarify(request, questions, index, answers);
                })
            .create();
    dialog.setOnCancelListener(d -> c.answer(request, obj("answer", "")));
    dialog.setOnDismissListener(d -> dialogOpen = false);
    dialog.show();
  }

  private void mainClarify(JSONObject r, JSONArray qs, int i, JSONObject a) {
    c.main.post(
        () -> {
          dialogOpen = false;
          clarify(r, qs, i + 1, a);
        });
  }

  private void voice() {
    try {
      Intent i =
          new Intent(RecognizerIntent.ACTION_RECOGNIZE_SPEECH)
              .putExtra(
                  RecognizerIntent.EXTRA_LANGUAGE_MODEL, RecognizerIntent.LANGUAGE_MODEL_FREE_FORM)
              .putExtra(RecognizerIntent.EXTRA_PROMPT, "message Hermes");
      startActivityForResult(i, VOICE);
    } catch (ActivityNotFoundException e) {
      toast("Install or enable an Android speech provider to use voice input.");
    }
  }

  private void notifications() {
    if (Build.VERSION.SDK_INT >= 33
        && checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS)
            != PackageManager.PERMISSION_GRANTED
        && !getPreferences(0).getBoolean("asked-notifications", false)) {
      getPreferences(0).edit().putBoolean("asked-notifications", true).apply();
      requestPermissions(new String[] {Manifest.permission.POST_NOTIFICATIONS}, 10);
    }
  }

  private void exportChat() {
    new AlertDialog.Builder(this)
        .setTitle("export chat")
        .setItems(
            new String[] {"save Markdown", "share text", "copy text"},
            (d, n) -> {
              String text = c.export();
              if (n == 0)
                saveAs(text.getBytes(StandardCharsets.UTF_8), "hermes-chat.md", "text/markdown");
              else if (n == 1)
                startActivity(
                    Intent.createChooser(
                        new Intent(Intent.ACTION_SEND)
                            .setType("text/plain")
                            .putExtra(Intent.EXTRA_TEXT, text),
                        "share chat"));
              else copy(text);
            })
        .show();
  }

  private void saveAs(byte[] bytes, String name, String mime) {
    pendingExport = bytes;
    startActivityForResult(
        new Intent(Intent.ACTION_CREATE_DOCUMENT)
            .addCategory(Intent.CATEGORY_OPENABLE)
            .setType(mime.isEmpty() ? "application/octet-stream" : mime)
            .putExtra(Intent.EXTRA_TITLE, name),
        SAVE_FILE);
  }

  @Override
  protected void onActivityResult(int request, int result, Intent data) {
    super.onActivityResult(request, result, data);
    if (result != RESULT_OK || data == null) {
      if (request == SAVE_FILE) pendingExport = null;
      return;
    }
    if (request == PICK_FILE && data.getData() != null) upload(data.getData());
    else if (request == SAVE_FILE && data.getData() != null && pendingExport != null) {
      byte[] bytes = pendingExport;
      pendingExport = null;
      Uri uri = data.getData();
      c.disk.execute(
          () -> {
            try (OutputStream out = getContentResolver().openOutputStream(uri)) {
              if (out == null) throw new IOException();
              out.write(bytes);
              runOnUiThread(() -> toast("saved"));
            } catch (Exception e) {
              runOnUiThread(() -> toast("Could not save that copy."));
            }
          });
    } else if (request == VOICE) {
      ArrayList<String> words = data.getStringArrayListExtra(RecognizerIntent.EXTRA_RESULTS);
      if (words != null && !words.isEmpty()) appendDraft(words.get(0));
    }
  }

  private void copy(String value) {
    getSystemService(android.content.ClipboardManager.class)
        .setPrimaryClip(ClipData.newPlainText("Hermes", value));
    toast("copied");
  }

  private void openLink(String link) {
    try {
      Uri u = Uri.parse(link);
      String scheme = u.getScheme();
      if (!"http".equals(scheme) && !"https".equals(scheme) && !"mailto".equals(scheme)) {
        toast("This link type is not supported.");
        return;
      }
      startActivity(new Intent(Intent.ACTION_VIEW, u));
    } catch (Exception e) {
      toast("No app can open that link.");
    }
  }

  private void toast(String value) {
    Toast.makeText(this, value, Toast.LENGTH_LONG).show();
  }
}
