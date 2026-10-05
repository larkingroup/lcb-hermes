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
  private LinearLayout root, content, transcript, toolsPanel, sessionPane, attachmentTray;
  private TextView connection, modelLabel, notice, serverStatus;
  private EditText composer;
  private Button send, approval;
  private ScrollView chatScroll;
  private final Map<JSONObject, TextView> messageViews = new IdentityHashMap<>();
  private boolean sessionsExpanded;
  private Button sessionPicker;
  private Markwon markdown;
  private int face, paper, shadow, ink, muted, blue, blueInk, accent, accentInk;
  private String page = "chat", theme = "";
  private boolean syncingDraft, needsScroll = true, dialogOpen;
  private boolean followTail = true;
  private boolean modelPickerPending;
  private Sheet modelDialog;
  private byte[] pendingExport;
  private static final int PICK_FILE = 2, SAVE_FILE = 3, VOICE = 4;

  @Override
  public void onCreate(Bundle saved) {
    super.onCreate(saved);
    c = ((HermesApp) getApplication()).controller;
    if (saved != null) {
      page = saved.getString("page", "chat");
      sessionsExpanded = saved.getBoolean("sessions", false);
    }
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
    b.putBoolean("sessions", sessionsExpanded);
  }

  @Override
  protected void onDestroy() {
    c.detach(this);
    if (modelDialog != null) modelDialog.dismiss();
    super.onDestroy();
  }

  @Override
  protected void onNewIntent(Intent intent) {
    super.onNewIntent(intent);
    setIntent(intent);
    if (intent.getBooleanExtra("reply", false)) show("chat");
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
    return dp(12 + Math.max(0, (getResources().getConfiguration().screenWidthDp - 820) / 2f));
  }

  private void colors() {
    theme = c.data.optString("theme", "light");
    boolean dark = theme.equals("dark");
    face = color(dark ? "#1e272e" : "#f0efe7");
    paper = color(dark ? "#182128" : "#fffefb");
    shadow = color(dark ? "#38464f" : "#b8c2c4");
    ink = color(dark ? "#f0f2ed" : "#202e38");
    muted = color(dark ? "#b8c5ce" : "#50616e");
    blue = color(dark ? "#354e62" : "#c8ddec");
    blueInk = color(dark ? "#ecf4fa" : "#203e55");
    accent = color(dark ? "#456c88" : "#315870");
    accentInk = color("#fffefb");
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
    v.setTextSize(Math.max(11, size * .9f));
    v.setTextColor(col);
    v.setPadding(dp(8), dp(4), dp(8), dp(4));
    v.setIncludeFontPadding(false);
    return v;
  }

  private TextView label(String value) {
    TextView v = text(value, 12, muted);
    return v;
  }

  private Button button(String value, Runnable click) {
    Button b = new Button(this);
    b.setText(value);
    b.setAllCaps(false);
    b.setTextSize(12);
    b.setTextColor(ink);
    b.setMinWidth(0);
    b.setMinimumWidth(0);
    b.setMinHeight(dp(40));
    b.setMinimumHeight(dp(40));
    b.setPadding(dp(10), dp(2), dp(10), dp(2));
    buttonColors(b, face, ink);
    b.setOnClickListener(v -> click.run());
    return b;
  }

  private void buttonColors(Button b, int fill, int textColor) {
    b.setTextColor(textColor);
    StateListDrawable bg = new StateListDrawable();
    bg.addState(new int[] {android.R.attr.state_pressed}, surface(blue, 8, true));
    bg.addState(new int[] {}, surface(fill, 8, true));
    b.setBackground(bg);
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
    bar.setPadding(0, dp(8), 0, dp(8));
    ImageView logo = new ImageView(this);
    logo.setImageResource(R.drawable.lcb_logo);
    logo.setContentDescription("LCB");
    logo.setBackground(surface(color("#faf9f1"), 4, false));
    logo.setClipToOutline(true);
    bar.addView(logo, new LinearLayout.LayoutParams(dp(24), dp(24)));
    TextView app = text("lcb-hermes", 16, ink);
    app.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
    weighted(bar, app);
    connection = label("offline");
    connection.setSingleLine(true);
    connection.setMaxWidth(dp(140));
    connection.setEllipsize(TextUtils.TruncateAt.END);
    if (!compact()) bar.addView(connection);
    bar.addView(
        iconButton(
            "server",
            "server and connection",
            () -> show(page.equals("server") ? "chat" : "server")));
    add(root, bar);
    content = column();
    root.addView(content, new LinearLayout.LayoutParams(-1, 0, 1));
    show(page);
  }

  private Drawable surface(int fill, int radius, boolean border) {
    GradientDrawable d = new GradientDrawable();
    d.setColor(fill);
    d.setCornerRadius(dp(radius));
    if (border) d.setStroke(dp(1), shadow);
    return d;
  }

  private Button iconButton(String glyph, String description, Runnable click) {
    Button b = button("", click);
    b.setContentDescription(description);
    b.setPadding(dp(11), dp(11), dp(11), dp(11));
    Glyph icon = new Glyph(glyph, ink);
    icon.setBounds(0, 0, dp(20), dp(20));
    b.setCompoundDrawables(icon, null, null, null);
    b.setBackground(
        new RippleDrawable(
            android.content.res.ColorStateList.valueOf(blue),
            surface(Color.TRANSPARENT, 8, false),
            null));
    return b;
  }

  private void show(String name) {
    if (name.equals("sessions")) {
      sessionsExpanded = true;
      name = "chat";
    }
    page = name.equals("server") ? "server" : "chat";
    content.removeAllViews();
    composer = null;
    messageViews.clear();
    notice = null;
    sessionPane = null;
    attachmentTray = null;
    serverStatus = null;
    if (page.equals("server")) server();
    else chat();
    update(false);
  }

  private void chat() {
    LinearLayout top = row();
    sessionPicker = button(c.title + "  ⌄", this::toggleSessions);
    sessionPicker.setTextSize(16);
    sessionPicker.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
    sessionPicker.setSingleLine(true);
    sessionPicker.setEllipsize(TextUtils.TruncateAt.END);
    sessionPicker.setGravity(Gravity.START | Gravity.CENTER_VERTICAL);
    sessionPicker.setPadding(dp(4), 0, dp(8), 0);
    sessionPicker.setBackground(
        new RippleDrawable(
            android.content.res.ColorStateList.valueOf(blue),
            surface(Color.TRANSPARENT, 6, false),
            null));
    sessionPicker.setContentDescription("choose chat");
    weighted(top, sessionPicker);
    Button fresh =
        button(
            "new chat",
            () -> {
              c.newChat();
              needsScroll = true;
              sessionsExpanded = false;
              show("chat");
            });
    fresh.setContentDescription("new chat");
    fresh.setTextColor(theme.equals("dark") ? blueInk : accent);
    fresh.setBackground(surface(paper, 8, true));
    top.addView(fresh);
    add(content, top);
    sessionPane = column();
    add(content, sessionPane);
    if (sessionsExpanded) renderSessionPicker();
    modelLabel = label((c.model.isEmpty() ? "server default" : c.model) + "  ⌄");
    modelLabel.setOnClickListener(v -> models());
    modelLabel.setContentDescription("choose model");
    modelLabel.setPadding(dp(4), dp(4), dp(8), dp(7));
    add(content, modelLabel);
    chatScroll = new ScrollView(this);
    chatScroll.setOnScrollChangeListener(
        (view, x, y, oldX, oldY) -> {
          if (y != oldY && chatScroll.getChildCount() > 0)
            followTail =
                chatScroll.getChildAt(0).getHeight() - chatScroll.getHeight() - y < dp(100);
        });
    chatScroll.setFillViewport(true);
    transcript = column();
    transcript.setPadding(0, dp(10), 0, dp(12));
    chatScroll.addView(transcript);
    content.addView(chatScroll, new LinearLayout.LayoutParams(-1, 0, 1));
    notice = label("");
    add(content, notice);
    approval = button("answer request", this::pendingRequest);
    add(content, approval);
    LinearLayout composeBox = column();
    composeBox.setPadding(dp(6), dp(6), dp(6), dp(6));
    composeBox.setBackground(surface(paper, 12, true));
    attachmentTray = column();
    add(composeBox, attachmentTray);
    renderAttachmentTray();
    LinearLayout input = row();
    input.addView(iconButton("attach", "attach photo or file", this::pickFile));
    composer = new EditText(this);
    composer.setTextColor(ink);
    composer.setHintTextColor(muted);
    composer.setHint("message Hermes…");
    composer.setTextSize(15);
    composer.setGravity(Gravity.CENTER_VERTICAL);
    composer.setMinLines(1);
    composer.setMaxLines(compact() ? 3 : 5);
    composer.setFilters(new InputFilter[] {new InputFilter.LengthFilter(100000)});
    composer.setPadding(dp(4), dp(10), dp(4), dp(10));
    composer.setBackgroundColor(Color.TRANSPARENT);
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
    weighted(input, composer);
    input.addView(iconButton("voice", "voice input", this::voice));
    send =
        iconButton(
            c.running ? "stop" : "send",
            c.running ? "stop Hermes" : "send message",
            () -> {
              if (c.running) c.stop();
              else {
                notifications();
                needsScroll = true;
                c.submit(composer.getText().toString());
              }
            });
    send.setBackground(surface(accent, 9, false));
    input.addView(send);
    add(composeBox, input);
    add(content, composeBox);
    gap(content, 8);
    renderTranscript();
  }

  private void toggleSessions() {
    sessionsExpanded = !sessionsExpanded;
    if (sessionsExpanded) renderSessionPicker();
    else if (sessionPane != null) sessionPane.removeAllViews();
  }

  private void renderSessionPicker() {
    if (sessionPane == null) return;
    sessionPane.removeAllViews();
    LinearLayout pane = column();
    pane.setPadding(dp(10), dp(8), dp(10), dp(8));
    pane.setBackground(surface(paper, 10, true));
    add(sessionPane, pane);
    EditText search = new EditText(this);
    search.setTextSize(14);
    search.setTextColor(ink);
    search.setHintTextColor(muted);
    search.setHint("find a chat");
    search.setSingleLine();
    search.setBackgroundColor(Color.TRANSPARENT);
    search.setPadding(dp(4), dp(4), dp(4), dp(8));
    add(pane, search);
    ScrollView scroll = new ScrollView(this);
    LinearLayout list = column();
    scroll.addView(list);
    pane.addView(scroll, new LinearLayout.LayoutParams(-1, dp(compact() ? 130 : 230)));
    Runnable render =
        () -> {
          list.removeAllViews();
          JSONArray all = c.sessions;
          int count = 0;
          String query = search.getText().toString().toLowerCase(Locale.ROOT);
          if (all != null)
            for (int i = 0; i < all.length(); i++) {
              JSONObject session = all.optJSONObject(i);
              if (session == null) continue;
              String title = session.optString("title", "untitled"),
                  preview = session.optString("preview");
              if (!(title + preview).toLowerCase(Locale.ROOT).contains(query)) continue;
              LinearLayout item = column();
              item.setPadding(dp(6), dp(7), dp(6), dp(7));
              String id = session.optString("id");
              item.setBackground(surface(id.equals(c.storedId) ? blue : paper, 6, false));
              TextView name = text(title, 15, ink);
              name.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
              name.setSingleLine();
              name.setEllipsize(TextUtils.TruncateAt.END);
              add(item, name);
              if (!preview.isEmpty()) {
                TextView detail = text(preview, 12, muted);
                detail.setSingleLine();
                detail.setEllipsize(TextUtils.TruncateAt.END);
                add(item, detail);
              }
              item.setContentDescription(title + ", open chat");
              item.setOnClickListener(
                  v -> {
                    if (c.running || c.submissionPending) {
                      toast("Finish this task first.");
                      return;
                    }
                    c.save();
                    c.resume(id);
                    sessionsExpanded = false;
                    needsScroll = true;
                    show("chat");
                  });
              item.setOnLongClickListener(
                  v -> {
                    sessionMenu(session);
                    return true;
                  });
              add(list, item);
              count++;
            }
          if (count == 0) add(list, label(query.isEmpty() ? "no chats yet" : "no matching chats"));
        };
    search.addTextChangedListener(
        new TextWatcher() {
          public void beforeTextChanged(CharSequence s, int st, int co, int a) {}

          public void onTextChanged(CharSequence s, int st, int b, int co) {
            render.run();
          }

          public void afterTextChanged(Editable e) {}
        });
    render.run();
    if (c.gateway != null && c.gateway.online())
      c.list(
          (v, e) -> {
            if (!sessionsExpanded || sessionPane == null || isDestroyed()) return;
            if (e == null) render.run();
            else toast(e);
          });
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
      welcome.setPadding(dp(12), dp(24), dp(12), dp(24));
      ImageView logo = new ImageView(this);
      logo.setImageResource(R.drawable.lcb_logo);
      logo.setBackgroundColor(color("#faf9f1"));
      welcome.addView(logo, new LinearLayout.LayoutParams(dp(44), dp(44)));
      gap(welcome, 12);
      TextView ready = text("what are we working on?", 20, ink);
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
      if (c.profile() == null || c.gateway == null || !c.gateway.online())
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
      String role = item.optString("role"), body = Attachments.displayText(item);
      if (!(role.equals("user") || role.equals("assistant"))
          || (body.trim().isEmpty() && item.optJSONArray("attachments") == null)) continue;
      LinearLayout block = column();
      block.setBackground(surface(role.equals("user") ? blue : paper, 10, true));
      block.setPadding(dp(4), dp(4), dp(4), dp(4));
      LinearLayout meta = row();
      TextView who = label(role.equals("user") ? "you" : "hermes");
      who.setTextColor(role.equals("user") ? blueInk : muted);
      weighted(meta, who);
      TextView copy = text("copy", 12, muted);
      copy.setPadding(dp(10), dp(7), dp(10), dp(7));
      copy.setContentDescription("copy " + role + " message");
      copy.setOnClickListener(v -> copy(Attachments.displayText(item)));
      meta.addView(copy);
      add(block, meta);
      renderImages(block, Attachments.forRow(item), false);
      TextView bodyView = text(body, 16, ink);
      bodyView.setTextIsSelectable(true);
      bodyView.setLineSpacing(dp(1), 1);
      markdown.setMarkdown(bodyView, body);
      add(block, bodyView);
      messageViews.put(item, bodyView);
      add(transcript, block);
      gap(transcript, 8);
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
    if (!page.equals("chat")) {
      if (serverStatus != null) serverStatus.setText(c.status);
      return;
    }
    sessionPicker.setText(c.title + "  ⌄");
    modelLabel.setText((c.model.isEmpty() ? "server default" : c.model) + "  ⌄");
    if (notice != null) {
      String n = c.notice;
      notice.setText(n);
      notice.setVisibility(n.isEmpty() ? View.GONE : View.VISIBLE);
    }
    approval.setText("answer request · " + c.requests.size());
    approval.setVisibility(c.requests.isEmpty() ? View.GONE : View.VISIBLE);
    send.setContentDescription(c.running ? "stop Hermes" : "send message");
    Glyph sendIcon = new Glyph(c.running ? "stop" : "send", accentInk);
    sendIcon.setBounds(0, 0, dp(20), dp(20));
    send.setCompoundDrawables(sendIcon, null, null, null);
    send.setEnabled(c.running || (!c.submissionPending && c.uploadsPending == 0));
    renderAttachmentTray();
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
        String value = Attachments.displayText(e.getKey());
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
    new Sheet.Builder(this)
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
                    new Sheet.Builder(this)
                        .setTitle("forget this server?")
                        .setMessage(
                            "Remove its saved login, draft, and cached chat from this phone. Server"
                                + " chats stay there.")
                        .setNegativeButton("cancel", null)
                        .setPositiveButton("forget", (x, y) -> c.forget())
                        .show();
                  break;
                case 6:
                  new Sheet.Builder(this)
                      .setTitle("lcb-hermes 0.2.0")
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
    e.setTextSize(14);
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
    e.setBackground(surface(paper, 8, true));
    e.setPadding(dp(10), dp(8), dp(10), dp(8));
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
      new Sheet.Builder(this)
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
    box.setPadding(0, 0, 0, 0);
    EditText name = field(box, "server name", "", false);
    JSONObject current = c.profile();
    EditText url =
        field(box, "dashboard URL", current == null ? "" : current.optString("url"), false);
    url.setInputType(
        android.text.InputType.TYPE_CLASS_TEXT | android.text.InputType.TYPE_TEXT_VARIATION_URI);
    EditText user =
        field(box, "username", current == null ? "" : current.optString("username"), false);
    EditText pass = field(box, "password", "", true);
    name.setHint("a name for this server");
    url.setHint("http(s)://your-server");
    user.setHint("");
    pass.setHint("");
    CheckBox http = new CheckBox(this);
    http.setText("allow private HTTP (try automatically)");
    http.setTextSize(13);
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
    Sheet dialog =
        new Sheet.Builder(this)
            .setTitle("connect a server")
            .setView(scroll)
            .setNegativeButton("cancel", (d, w) -> pass.setText(""))
            .setPositiveButton("connect", null)
            .create();
    dialog.setOnShowListener(
        d -> {
          dialog
              .getButton(-1)
              .setOnClickListener(
                  v -> {
                    String password = pass.getText().toString();
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
                            pass.setText("");
                            dialog.dismiss();
                            show("chat");
                          }
                        });
                  });
        });
    dialog.show();
    dialog.setOnDismissListener(d -> pass.setText(""));
  }

  private void profiles() {
    JSONArray list = c.profiles();
    String[] names = new String[list.length() + 1];
    for (int i = 0; i < list.length(); i++) names[i] = list.optJSONObject(i).optString("name");
    names[list.length()] = "+ add server";
    new Sheet.Builder(this)
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

  private void sessionMenu(JSONObject session) {
    String id = session.optString("id");
    new Sheet.Builder(this)
        .setTitle(session.optString("title", "chat"))
        .setItems(
            new String[] {"rename", "delete"},
            (d, n) -> {
              if (n == 0) {
                LinearLayout box = column();
                box.setPadding(dp(16), 0, dp(16), 0);
                EditText name = field(box, "title", session.optString("title"), false);
                new Sheet.Builder(this)
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
                new Sheet.Builder(this)
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
    if (modelPickerPending || (modelDialog != null && modelDialog.isShowing())) return;
    if (c.gateway == null || !c.gateway.online()) {
      toast("Connect to select a model.");
      return;
    }
    modelPickerPending = true;
    Gateway source = c.gateway;
    source.rpc(
        "model.options",
        obj("include_unconfigured", false),
        (v, e) -> {
          modelPickerPending = false;
          if (isFinishing() || isDestroyed() || source != c.gateway) return;
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
                  labels.add(m + "\n" + p.optString("name", p.optString("slug")));
                  models.add(m);
                  providers.add(p.optString("slug"));
                }
            }
          modelDialog =
              new Sheet.Builder(this)
                  .setTitle("model for a new chat")
                  .setItems(
                      labels.toArray(new String[0]),
                      (d, n) -> {
                        if (c.running || c.submissionPending) {
                          toast("Finish this task first.");
                          return;
                        }
                        if (!c.sessionId.isEmpty() || !c.rows.isEmpty())
                          new Sheet.Builder(this)
                              .setTitle("start a new chat?")
                              .setMessage(
                                  "Use "
                                      + (models.get(n).isEmpty()
                                          ? "the server default"
                                          : models.get(n))
                                      + " in a new session. This chat is saved on the server.")
                              .setNegativeButton("cancel", null)
                              .setPositiveButton(
                                  "new chat",
                                  (x, y) -> {
                                    c.newModel = models.get(n);
                                    c.newProvider = providers.get(n);
                                    c.newChat();
                                    show("chat");
                                  })
                              .show();
                        else {
                          c.newModel = models.get(n);
                          c.newProvider = providers.get(n);
                          c.model = c.newModel;
                          update(false);
                        }
                      })
                  .show();
        });
  }

  private void pickFile() {
    if (c.gateway == null || !c.gateway.online()) {
      toast("Connect before attaching files.");
      return;
    }
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
    if (c.attachments.length() >= 6) {
      toast("Send these attachments before adding more.");
      return;
    }
    if (name == null || name.trim().isEmpty()) name = "attachment";
    name = name.replaceAll("[\\\\/\\p{Cntrl}]", "_");
    final String filename = name;
    String target = "uploads/" + UUID.randomUUID() + "-" + filename;
    String mime = getContentResolver().getType(uri);
    final String type = mime == null ? "application/octet-stream" : mime;
    final String sourceProfile = c.profileId, sourceChat = c.storedId;
    final Gateway sourceGateway = c.gateway;
    if (c.running || c.submissionPending) {
      toast("Finish this task before attaching a file.");
      return;
    }
    c.uploadsPending++;
    c.changed(false);

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
            String thumbnail = Attachments.thumbnail(bytes, type);
            runOnUiThread(
                () -> {
                  if (sourceGateway != c.gateway
                      || !sourceGateway.online()
                      || !sourceProfile.equals(c.profileId)
                      || !sourceChat.equals(c.storedId)) {
                    c.uploadsPending = Math.max(0, c.uploadsPending - 1);
                    c.changed(false);
                    toast("Connection lost. Choose the file again after reconnecting.");
                    return;
                  }
                  sourceGateway.upload(
                      target,
                      filename,
                      bytes,
                      (v, e) -> {
                        c.uploadsPending = Math.max(0, c.uploadsPending - 1);
                        if (e != null) {
                          toast(e);
                          c.changed(false);
                          return;
                        }
                        if (sourceGateway != c.gateway
                            || !sourceProfile.equals(c.profileId)
                            || !sourceChat.equals(c.storedId)) {
                          toast("Chat changed. Choose the attachment again.");
                          c.changed(false);
                          return;
                        }
                        c.addAttachment(
                            obj(
                                "id",
                                UUID.randomUUID().toString(),
                                "name",
                                filename,
                                "mime",
                                type,
                                "path",
                                v.optString("path", target),
                                "thumbnail",
                                thumbnail));
                      });
                });
          } catch (Exception e) {
            runOnUiThread(
                () -> {
                  c.uploadsPending = Math.max(0, c.uploadsPending - 1);
                  c.changed(false);
                  toast(e.getMessage() == null ? "Could not read that file." : e.getMessage());
                });
          }
        });
  }

  private void renderAttachmentTray() {
    if (attachmentTray == null) return;
    attachmentTray.removeAllViews();
    renderImages(attachmentTray, c.attachments, true);
    if (c.uploadsPending > 0) add(attachmentTray, label("loading attachment…"));
    attachmentTray.setVisibility(
        c.attachments.length() == 0 && c.uploadsPending == 0 ? View.GONE : View.VISIBLE);
  }

  private void renderImages(LinearLayout parent, JSONArray attachments, boolean pending) {
    if (attachments == null || attachments.length() == 0) return;
    HorizontalScrollView scroll = new HorizontalScrollView(this);
    scroll.setHorizontalScrollBarEnabled(false);
    LinearLayout images = row();
    scroll.addView(images);
    add(parent, scroll);
    for (int i = 0; i < attachments.length(); i++) {
      JSONObject a = attachments.optJSONObject(i);
      if (a == null) continue;
      LinearLayout tile = column();
      tile.setPadding(dp(4), dp(4), dp(4), dp(4));
      Bitmap bitmap = Attachments.bitmap(a);
      if (bitmap != null) {
        ImageView image = new ImageView(this);
        image.setImageBitmap(bitmap);
        image.setScaleType(
            pending ? ImageView.ScaleType.CENTER_CROP : ImageView.ScaleType.FIT_CENTER);
        image.setBackground(surface(paper, 8, true));
        image.setClipToOutline(true);
        image.setContentDescription(
            (pending ? "attachment preview: " : "attached image: ") + a.optString("name", "image"));
        float ratio = (float) bitmap.getWidth() / bitmap.getHeight();
        int width = pending ? 82 : Math.min(240, Math.round(220 * ratio));
        int height = pending ? 74 : Math.min(220, Math.round(width / ratio));
        tile.addView(image, new LinearLayout.LayoutParams(dp(width), dp(height)));
        image.setOnClickListener(
            v -> {
              ImageView full = new ImageView(this);
              full.setImageBitmap(bitmap);
              full.setAdjustViewBounds(true);
              full.setMaxHeight(dp(460));
              new Sheet.Builder(this)
                  .setTitle(a.optString("name", "image"))
                  .setView(full)
                  .setPositiveButton("done", null)
                  .show();
            });
      } else {
        TextView file = text(a.optString("name", "file"), 13, ink);
        file.setMaxLines(2);
        file.setEllipsize(TextUtils.TruncateAt.END);
        file.setBackground(surface(blue, 8, false));
        file.setMinHeight(dp(52));
        tile.addView(file, new LinearLayout.LayoutParams(dp(140), -2));
      }
      if (pending) {
        TextView remove = text("remove", 11, muted);
        remove.setGravity(Gravity.CENTER);
        remove.setPadding(0, dp(6), 0, dp(6));
        remove.setContentDescription("remove attachment " + a.optString("name"));
        remove.setOnClickListener(v -> c.removeAttachment(a.optString("id")));
        add(tile, remove);
      }
      images.addView(tile);
    }
  }

  private void appendDraft(String value) {
    c.setDraft(c.draft + (c.draft.isEmpty() ? "" : "\n\n") + value);
    update(false);
  }

  private void server() {
    LinearLayout top = row();
    top.addView(iconButton("back", "back to chat", () -> show("chat")));
    weighted(top, text("server", 18, ink));
    top.addView(iconButton("more", "settings", this::menu));
    add(content, top);
    gap(content, 14);
    LinearLayout card = column();
    card.setPadding(dp(12), dp(14), dp(12), dp(14));
    card.setBackground(surface(paper, 12, true));
    add(content, card);
    JSONObject p = c.profile();
    if (p == null) {
      add(card, text("Connect your Hermes server", 18, ink));
      add(card, text("Your chats and tools stay on your server.", 14, muted));
      gap(card, 10);
      add(card, button("connect a server", this::loginDialog));
      return;
    }
    add(card, text(p.optString("name"), 18, ink));
    add(card, label(p.optString("url")));
    serverStatus = label(c.status);
    add(card, serverStatus);
    gap(card, 12);
    add(
        card,
        button(
            "reconnect",
            () -> {
              c.close();
              c.connectSaved();
            }));
    gap(card, 6);
    add(card, button("servers", this::profiles));
    gap(card, 6);
    add(card, button("open dashboard", () -> openLink(p.optString("url"))));
    gap(content, 16);
    add(
        content,
        text(
            "Hermes works here. Model accounts, browser tools, and other settings live in the"
                + " dashboard.",
            14,
            muted));
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
    new Sheet.Builder(this)
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
      Sheet.Builder b =
          new Sheet.Builder(this)
              .setTitle("allow this action?")
              .setMessage(description + (command.isEmpty() ? "" : "\n\n" + command))
              .setNegativeButton("deny", (d, w) -> c.answer(r, obj("choice", "deny")));
      JSONArray choices = params.optJSONArray("choices");
      boolean once = choices == null;
      if (choices != null)
        for (int i = 0; i < choices.length(); i++) once |= choices.optString(i).equals("once");
      if (once) b.setPositiveButton("allow once", (d, w) -> c.answer(r, obj("choice", "once")));
      Sheet dialog = b.create();
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
    Sheet dialog =
        new Sheet.Builder(this)
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
    new Sheet.Builder(this)
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
