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
  private LinearLayout root, content, transcript, toolsPanel, sidebar, sideList, attachmentTray;
  private LinearLayout monitorPanel, topBar;
  private View sidebarDivider;
  private boolean tightTyping, compactSearch;
  private LinearLayout sidebarHeading, sidebarFooter;
  private TextView activityText;
  private boolean activityExpanded, wizardShown;
  private String trayState = "";
  private final Map<JSONObject, LinearLayout> messageBlocks = new IdentityHashMap<>();
  private ListView chatList;
  private final List<JSONObject> sidebarRows = new ArrayList<>();
  private BaseAdapter sidebarAdapter;
  private final Set<String> collapsedFolders = new HashSet<>();
  private FrameLayout workspace;
  private View drawerScrim;
  private EditText sideSearch;
  private String chatQuery = "", sidebarState = "";
  private String notificationSession = "", notificationProfile = "";
  private TextView chatTitle;
  private TextView connection, modelLabel, notice, serverStatus;
  private EditText composer;
  private Button send, approval, folderButton;
  private LinearLayout chatHeading;
  private TextView activityHeading;
  private ScrollView chatScroll;
  private final Map<JSONObject, TextView> messageViews = new IdentityHashMap<>();
  private boolean sessionsExpanded;
  private Markwon markdown;
  private int face, paper, shadow, ink, muted, blue, blueInk, accent, accentInk;
  private String page = "chat", theme = "";
  private String serverPageProfile = "";
  private boolean syncingDraft, needsScroll = true, dialogOpen;
  private boolean followTail = true;
  private boolean modelPickerPending;
  private Sheet modelDialog, requestDialog;
  private String requestDialogId = "";
  private android.window.OnBackInvokedCallback backCallback;
  private boolean backRegistered, started, statsLoading;
  private final Handler screenHandler = new Handler(Looper.getMainLooper());
  private LinearLayout statsRows;
  private TextView statsNotice;
  private int statsGeneration;
  private Gateway statsSource;
  private final Runnable pollStats =
      new Runnable() {
        @Override
        public void run() {
          if (!started || statsRows == null) return;
          refreshServerStats();
          screenHandler.postDelayed(this, 5000);
        }
      };
  private byte[] pendingExport;
  private static final int PICK_FILE = 2, SAVE_FILE = 3, VOICE = 4;

  @Override
  public void onCreate(Bundle saved) {
    super.onCreate(saved);
    c = ((HermesApp) getApplication()).controller;
    if (saved != null) {
      page = saved.getString("page", "chat");
      sessionsExpanded = saved.getBoolean("sessions", false);
      chatQuery = saved.getString("chatQuery", "");
      notificationSession = saved.getString("notificationSession", "");
      notificationProfile = saved.getString("notificationProfile", "");
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
    if (saved == null) notificationIntent(getIntent());
    else routeNotification();
  }

  @Override
  protected void onStart() {
    super.onStart();
    started = true;
    c.attach(this);
    updateBackHandler();
    if (statsRows != null) startServerStats();
  }

  @Override
  protected void onStop() {
    started = false;
    stopServerStats();
    c.detach(this);
    super.onStop();
  }

  private boolean navigateBack() {
    if (sessionsExpanded) {
      hideKeyboard();
      sessionsExpanded = false;
      setSidebarVisible();
      updateBackHandler();
      return true;
    }
    if (!page.equals("chat")) {
      show("chat");
      return true;
    }
    return false;
  }

  private void updateBackHandler() {
    if (Build.VERSION.SDK_INT < 33) return;
    if (backCallback == null) backCallback = () -> navigateBack();
    boolean needed = !page.equals("chat") || sessionsExpanded;
    if (needed && !backRegistered) {
      getOnBackInvokedDispatcher()
          .registerOnBackInvokedCallback(
              android.window.OnBackInvokedDispatcher.PRIORITY_DEFAULT, backCallback);
      backRegistered = true;
    } else if (!needed && backRegistered) {
      getOnBackInvokedDispatcher().unregisterOnBackInvokedCallback(backCallback);
      backRegistered = false;
    }
  }

  @Override
  @android.annotation.SuppressLint(
      "GestureBackNavigation") // Android 8–12 fallback; newer devices use the dispatcher above.
  public void onBackPressed() {
    if (!navigateBack()) super.onBackPressed();
  }

  @Override
  protected void onSaveInstanceState(Bundle b) {
    super.onSaveInstanceState(b);
    b.putString("page", page);
    b.putBoolean("sessions", sessionsExpanded);
    b.putString("chatQuery", chatQuery);
    b.putString("notificationSession", notificationSession);
    b.putString("notificationProfile", notificationProfile);
  }

  @Override
  protected void onDestroy() {
    c.detach(this);
    stopServerStats();
    if (Build.VERSION.SDK_INT >= 33 && backRegistered)
      getOnBackInvokedDispatcher().unregisterOnBackInvokedCallback(backCallback);
    if (modelDialog != null) modelDialog.dismiss();
    if (requestDialog != null) requestDialog.dismiss();
    super.onDestroy();
  }

  @Override
  protected void onNewIntent(Intent intent) {
    super.onNewIntent(intent);
    setIntent(intent);
    notificationIntent(intent);
  }

  private void notificationIntent(Intent intent) {
    if (intent == null
        || (!intent.getBooleanExtra("reply", false) && !intent.getBooleanExtra("task", false)))
      return;
    notificationSession =
        intent.getStringExtra("session") == null ? "" : intent.getStringExtra("session");
    notificationProfile =
        intent.getStringExtra("profile") == null ? "" : intent.getStringExtra("profile");
    intent.removeExtra("reply");
    intent.removeExtra("task");
    intent.removeExtra("session");
    intent.removeExtra("profile");
    sessionsExpanded = false;
    show("chat");
    routeNotification();
  }

  private void routeNotification() {
    if (!c.loaded || notificationSession.isEmpty()) return;
    if (!notificationProfile.isEmpty() && !notificationProfile.equals(c.profileId)) {
      notificationSession = "";
      c.notice = "This reply belongs to another saved server.";
      show("server");
      return;
    }
    if (c.submissionPending || c.uploadsPending > 0 || c.status.equals("loading chat")) return;
    if (c.gateway == null || !c.gateway.online()) return;
    String id = notificationSession;
    notificationSession = "";
    if (!id.equals(c.storedId)) c.resume(id);
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

  private boolean wide() {
    return getResources().getConfiguration().screenWidthDp >= 600;
  }

  private int gutter() {
    if (wide()) return dp(12);
    return dp(12 + Math.max(0, (getResources().getConfiguration().screenWidthDp - 820) / 2f));
  }

  private void colors() {
    theme = "light";
    face = color("#dedac7");
    paper = color("#fffdee");
    shadow = color("#8c8b7e");
    ink = color("#2b302a");
    muted = color("#5e6350");
    blue = color("#c0cbb3");
    blueInk = ink;
    accent = color("#657855");
    accentInk = color("#fffdee");
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
    b.setMinHeight(dp(48));
    b.setMinimumHeight(dp(48));
    b.setPadding(dp(10), dp(2), dp(10), dp(2));
    buttonColors(b, face, ink);
    b.setOnClickListener(v -> click.run());
    return b;
  }

  private void buttonColors(Button b, int fill, int textColor) {
    b.setTextColor(textColor);
    StateListDrawable bg = new StateListDrawable();
    bg.addState(
        new int[] {android.R.attr.state_pressed}, new Bevel(blue, paper, shadow, true, dp(1)));
    bg.addState(new int[] {}, new Bevel(fill, paper, shadow, false, dp(1)));
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
    root.setFocusableInTouchMode(true);
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
            boolean tight =
                compact()
                    && insets.isVisible(WindowInsets.Type.ime())
                    && composer != null
                    && page.equals("chat");
            compactSearch = tight && sideSearch != null && sideSearch.hasFocus();
            boolean changed = tightTyping != tight;
            tightTyping = tight;
            if (topBar != null) topBar.setVisibility(tight ? View.GONE : View.VISIBLE);
            if (chatHeading != null) chatHeading.setVisibility(tight ? View.GONE : View.VISIBLE);
            if (modelLabel != null) modelLabel.setVisibility(tight ? View.GONE : View.VISIBLE);
            if (activityHeading != null)
              activityHeading.setVisibility(tight ? View.GONE : View.VISIBLE);
            if (activityText != null && activityHeading != null)
              activityText.setVisibility(!tight && activityExpanded ? View.VISIBLE : View.GONE);
            if (monitorPanel != null && monitorPanel.getParent() != null)
              monitorPanel.setVisibility(!tight && page.equals("chat") ? View.VISIBLE : View.GONE);
            setSidebarVisible();
            if (changed) {
              trayState = "";
              renderAttachmentTray();
            }
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
    topBar = bar;
    bar.setPadding(0, dp(8), 0, dp(8));
    if (!wide()) bar.addView(iconButton("sidebar", "choose chat", this::toggleSessions));
    ImageView logo = new ImageView(this);
    logo.setImageResource(R.drawable.lcb_logo);
    logo.setContentDescription("LCB");
    logo.setBackground(surface(color("#faf9f1"), 4, false));
    logo.setClipToOutline(true);
    bar.addView(logo, new LinearLayout.LayoutParams(dp(24), dp(24)));
    TextView app = text("lcb-hermes", 16, ink);
    app.setSingleLine(true);
    app.setEllipsize(TextUtils.TruncateAt.END);
    app.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
    weighted(bar, app);
    connection = label("offline");
    connection.setSingleLine(true);
    connection.setMaxWidth(dp(140));
    connection.setEllipsize(TextUtils.TruncateAt.END);
    if (wide() && !compact()) bar.addView(connection);
    bar.addView(iconButton("server", "server and connection", this::serverDropdown));
    bar.addView(iconButton("more", "Chat menu", this::chatMenu));
    add(root, bar);
    workspace = new FrameLayout(this);
    root.addView(workspace, new LinearLayout.LayoutParams(-1, 0, 1));
    LinearLayout panes = row();
    panes.setGravity(Gravity.TOP);
    workspace.addView(panes, new FrameLayout.LayoutParams(-1, -1));
    sidebar = column();
    if (wide()) {
      sessionsExpanded = false;
      panes.addView(
          sidebar,
          new LinearLayout.LayoutParams(
              dp(
                  Math.min(
                      Math.max(180, c.data.optInt("sidebar_width", 224)),
                      getResources().getConfiguration().screenWidthDp >= 900
                          ? getResources().getConfiguration().screenWidthDp - 580
                          : getResources().getConfiguration().screenWidthDp - 330)),
              -1));
      View line = new View(this);
      sidebarDivider = line;
      line.setBackgroundColor(shadow);
      LinearLayout.LayoutParams edge = new LinearLayout.LayoutParams(dp(10), -1);
      edge.setMargins(dp(8), 0, dp(12), dp(8));
      line.setBackground(new Bevel(face, paper, shadow, false, dp(1)));
      line.setContentDescription("resize chats pane");
      line.setOnTouchListener(
          (v, event) -> {
            if (event.getAction() == MotionEvent.ACTION_MOVE) {
              int[] location = new int[2];
              panes.getLocationOnScreen(location);
              int width =
                  Math.max(
                      dp(180),
                      Math.min(
                          dp(
                              getResources().getConfiguration().screenWidthDp
                                  - (monitorPanel != null && monitorPanel.getParent() != null
                                      ? 580
                                      : 330)),
                          (int) event.getRawX() - location[0]));
              sidebar.getLayoutParams().width = width;
              sidebar.requestLayout();
              try {
                c.data.put("sidebar_width", width / getResources().getDisplayMetrics().density);
              } catch (JSONException ignored) {
              }
            }
            if (event.getAction() == MotionEvent.ACTION_UP) c.save();
            return true;
          });
      panes.addView(line, edge);
    }
    content = column();
    panes.addView(content, new LinearLayout.LayoutParams(0, -1, 1));
    monitorPanel = column();
    if (getResources().getConfiguration().screenWidthDp >= 900) {
      LinearLayout.LayoutParams monitor = new LinearLayout.LayoutParams(dp(220), -1);
      monitor.setMargins(dp(10), 0, 0, dp(8));
      panes.addView(monitorPanel, monitor);
    }
    if (!wide()) {
      drawerScrim = new View(this);
      drawerScrim.setBackgroundColor(0x70000000);
      drawerScrim.setContentDescription("close chats");
      drawerScrim.setOnClickListener(v -> toggleSessions());
      workspace.addView(drawerScrim, new FrameLayout.LayoutParams(-1, -1));
      FrameLayout.LayoutParams drawer =
          new FrameLayout.LayoutParams(
              dp(Math.min(288, getResources().getConfiguration().screenWidthDp - 56)),
              -1,
              Gravity.START);
      drawer.setMargins(0, 0, 0, dp(8));
      sidebar.setElevation(dp(8));
      workspace.addView(sidebar, drawer);
    }
    buildSidebar();
    setSidebarVisible();
    show(page);
    root.requestFocus();
  }

  private Drawable surface(int fill, int radius, boolean border) {
    GradientDrawable d = new GradientDrawable();
    d.setColor(fill);
    d.setCornerRadius(dp(radius));
    if (border) return new Bevel(fill, paper, shadow, true, dp(1));
    return d;
  }

  private Button iconButton(String glyph, String description, Runnable click) {
    Button b = button("", click);
    b.setContentDescription(description);
    b.setMinWidth(dp(48));
    b.setMinimumWidth(dp(48));
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
    stopServerStats();
    if (name.equals("sessions")) {
      sessionsExpanded = !wide();
      refreshSessions();
      name = "chat";
    }
    page = name.equals("server") ? "server" : "chat";
    content.removeAllViews();
    composer = null;
    modelLabel = null;
    chatHeading = null;
    activityHeading = null;
    messageViews.clear();
    messageBlocks.clear();
    activityText = null;
    trayState = "";
    monitorPanel.removeAllViews();
    monitorPanel.setVisibility(page.equals("chat") ? View.VISIBLE : View.GONE);
    notice = null;
    attachmentTray = null;
    serverStatus = null;
    statsRows = null;
    statsNotice = null;
    setSidebarVisible();
    if (page.equals("server")) {
      sessionsExpanded = false;
      setSidebarVisible();
      server();
    } else chat();
    update(false);
    updateBackHandler();
  }

  private void chat() {
    LinearLayout top = row();
    chatHeading = top;
    chatTitle = text(c.title, 17, ink);
    chatTitle.setTypeface(Typeface.MONOSPACE, Typeface.BOLD);
    chatTitle.setSingleLine(true);
    chatTitle.setEllipsize(TextUtils.TruncateAt.END);
    chatTitle.setPadding(dp(4), dp(6), dp(8), dp(6));
    weighted(top, chatTitle);
    if (!wide()) top.addView(iconButton("new", "new chat", this::freshChat));
    add(content, top);
    modelLabel = label((c.model.isEmpty() ? "server default" : c.model) + "  ⌄");
    modelLabel.setOnClickListener(v -> models());
    modelLabel.setContentDescription("choose model");
    modelLabel.setPadding(dp(4), dp(4), dp(8), dp(7));
    add(content, modelLabel);
    if (getResources().getConfiguration().screenWidthDp >= 900) {
      add(monitorPanel, paneHeading("Server monitor"));
      statsNotice = label("Connect to read server stats.");
      add(monitorPanel, statsNotice);
      statsRows = column();
      add(monitorPanel, statsRows);
      add(monitorPanel, paneHeading("Hermes activity"));
      ScrollView activityScroll = new ScrollView(this);
      activityText = text("Ready", 13, ink);
      activityText.setBackground(surface(paper, 0, true));
      activityScroll.addView(activityText);
      monitorPanel.addView(activityScroll, new LinearLayout.LayoutParams(-1, 0, 1));
      if (started) startServerStats();
    } else {
      TextView heading = paneHeading("Hermes activity  ▸");
      activityHeading = heading;
      heading.setMinHeight(dp(40));
      heading.setContentDescription("expand Hermes activity");
      heading.setOnClickListener(
          v -> {
            activityExpanded = !activityExpanded;
            heading.setText("Hermes activity  " + (activityExpanded ? "▾" : "▸"));
            activityText.setVisibility(activityExpanded ? View.VISIBLE : View.GONE);
          });
      add(content, heading);
      activityText = text("Ready", 13, ink);
      activityText.setMaxLines(compact() ? 3 : 7);
      activityText.setBackground(surface(paper, 0, true));
      activityText.setOnClickListener(
          v ->
              new Sheet.Builder(this)
                  .setTitle("Hermes activity")
                  .setMessage(activityValue())
                  .setPositiveButton("Close", null)
                  .show());
      activityText.setVisibility(activityExpanded ? View.VISIBLE : View.GONE);
      add(content, activityText);
    }
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
    send.setBackground(new Bevel(accent, paper, shadow, false, dp(1)));
    input.addView(send);
    add(composeBox, input);
    add(content, composeBox);
    gap(content, 8);
    renderTranscript();
  }

  private void freshChat() {
    c.newChat();
    needsScroll = true;
    sessionsExpanded = false;
    hideKeyboard();
    show("chat");
    setSidebarVisible();
    refreshSessions();
  }

  private void hideKeyboard() {
    android.view.inputmethod.InputMethodManager input =
        getSystemService(android.view.inputmethod.InputMethodManager.class);
    input.hideSoftInputFromWindow(root.getWindowToken(), 0);
    root.requestFocus();
  }

  private void toggleSessions() {
    if (wide()) return;
    sessionsExpanded = !sessionsExpanded;
    hideKeyboard();
    setSidebarVisible();
    if (sessionsExpanded) refreshSessions();
    updateBackHandler();
  }

  private void setSidebarVisible() {
    if (sidebar == null) return;
    sidebar.setVisibility(
        (!tightTyping || compactSearch) && (wide() || sessionsExpanded) ? View.VISIBLE : View.GONE);
    if (sidebarDivider != null)
      sidebarDivider.setVisibility(tightTyping && !compactSearch ? View.GONE : View.VISIBLE);
    if (sidebarHeading != null)
      sidebarHeading.setVisibility(compactSearch ? View.GONE : View.VISIBLE);
    if (sidebarFooter != null)
      sidebarFooter.setVisibility(compactSearch ? View.GONE : View.VISIBLE);
    if (drawerScrim != null)
      drawerScrim.setVisibility(!tightTyping && sessionsExpanded ? View.VISIBLE : View.GONE);
    if (!wide() && content != null)
      content.setImportantForAccessibility(
          sessionsExpanded
              ? View.IMPORTANT_FOR_ACCESSIBILITY_NO_HIDE_DESCENDANTS
              : View.IMPORTANT_FOR_ACCESSIBILITY_AUTO);
  }

  private void buildSidebar() {
    sidebar.setPadding(dp(8), dp(4), dp(8), dp(8));
    sidebar.setBackground(surface(face, 0, true));
    sidebar.setFocusableInTouchMode(true);
    LinearLayout heading = row();
    sidebarHeading = heading;
    TextView name = text("chats", 14, muted);
    name.setTypeface(Typeface.MONOSPACE, Typeface.BOLD);
    weighted(heading, name);
    heading.addView(iconButton("new", "create chat", this::freshChat));
    if (!wide()) {
      heading.addView(
          iconButton(
              "sidebar",
              "expand chats pane",
              () -> {
                int narrow =
                    dp(Math.min(288, getResources().getConfiguration().screenWidthDp - 56));
                sidebar.getLayoutParams().width =
                    sidebar.getLayoutParams().width == narrow
                        ? dp(getResources().getConfiguration().screenWidthDp - 24)
                        : narrow;
                sidebar.requestLayout();
              }));
      heading.addView(iconButton("close", "hide chats", this::toggleSessions));
    }
    add(sidebar, heading);
    sideSearch = new EditText(this);
    sideSearch.setContentDescription("find a chat");
    sideSearch.setTextSize(13);
    sideSearch.setTextColor(ink);
    sideSearch.setHintTextColor(muted);
    sideSearch.setHint("find a chat");
    sideSearch.setSingleLine();
    sideSearch.setPadding(dp(12), dp(10), dp(12), dp(10));
    sideSearch.setBackground(surface(paper, 4, true));
    sideSearch.setText(chatQuery);
    add(sidebar, sideSearch);
    gap(sidebar, 8);
    chatList = new ListView(this);
    chatList.setDivider(new ColorDrawable(shadow));
    chatList.setDividerHeight(dp(1));
    chatList.setBackground(surface(paper, 0, true));
    sidebarAdapter =
        new BaseAdapter() {
          public int getCount() {
            return sidebarRows.size();
          }

          public Object getItem(int position) {
            return sidebarRows.get(position);
          }

          public long getItemId(int position) {
            return sidebarRows.get(position).optString("id").hashCode();
          }

          public boolean hasStableIds() {
            return true;
          }

          public View getView(int position, View recycled, ViewGroup parent) {
            JSONObject session = sidebarRows.get(position);
            LinearLayout item =
                recycled instanceof LinearLayout ? (LinearLayout) recycled : column();
            if (item.getChildCount() == 0) {
              add(item, text("", 14, ink));
              add(item, text("", 11, muted));
              item.setMinimumHeight(dp(52));
              item.setPadding(dp(3), dp(4), dp(3), dp(4));
            }
            TextView title = (TextView) item.getChildAt(0), meta = (TextView) item.getChildAt(1);
            String id = session.optString("id"), badge = c.chatBadge(id);
            boolean group = session.optBoolean("group"), selected = !group && c.sameChat(id);
            title.setText(
                group
                    ? (collapsedFolders.contains(id) ? "▸ " : "▾ ") + session.optString("title")
                    : session.optString("title", "untitled"));
            title.setTextColor(!selected && badge.equals("reply") ? color("#1a4da1") : ink);
            title.setTypeface(
                Typeface.DEFAULT,
                selected || group || badge.equals("reply") ? Typeface.BOLD : Typeface.NORMAL);
            title.setMaxLines(1);
            title.setEllipsize(TextUtils.TruncateAt.END);
            String stamp = Workbench.date(session, false), source = session.optString("source");
            meta.setText(
                stamp
                    + (source.isEmpty() ? "" : " · " + source)
                    + (badge.isEmpty() ? "" : " · " + badge));
            meta.setVisibility(group || meta.getText().length() == 0 ? View.GONE : View.VISIBLE);
            item.setBackgroundColor(selected || group ? blue : paper);
            item.setContentDescription(
                session.optString("title") + " " + Workbench.date(session, true) + " " + badge);
            item.setOnClickListener(
                v -> {
                  if (group) {
                    if (!collapsedFolders.remove(id)) collapsedFolders.add(id);
                    sidebarState = "";
                    renderSidebar();
                    return;
                  }
                  if (c.submissionPending
                      || c.uploadsPending > 0
                      || c.status.equals("loading chat")) {
                    toast("Wait for this message to finish loading.");
                    return;
                  }
                  if (id.isEmpty()) c.newChat();
                  else if (!c.sameChat(id)) c.resume(id);
                  sessionsExpanded = false;
                  hideKeyboard();
                  needsScroll = true;
                  show("chat");
                });
            item.setOnLongClickListener(
                group || id.isEmpty()
                    ? null
                    : v -> {
                      sessionMenu(session);
                      return true;
                    });
            return item;
          }
        };
    chatList.setAdapter(sidebarAdapter);
    sidebar.addView(chatList, new LinearLayout.LayoutParams(-1, 0, 1));
    LinearLayout folder = row();
    folderButton = button("Folder for new chats", this::folderPicker);
    folderButton.setMaxLines(2);
    folderButton.setEllipsize(TextUtils.TruncateAt.END);
    weighted(folder, folderButton);
    folder.addView(iconButton("folder", "manage workspaces", this::workspaces));
    sidebarFooter = column();
    add(sidebarFooter, folder);
    add(sidebarFooter, label("Hermes · your server"));
    add(sidebar, sidebarFooter);

    sideSearch.addTextChangedListener(
        new TextWatcher() {
          public void beforeTextChanged(CharSequence s, int st, int co, int a) {}

          public void onTextChanged(CharSequence s, int st, int b, int co) {
            chatQuery = s.toString();
            renderSidebar();
          }

          public void afterTextChanged(Editable e) {}
        });
    sidebarState = "";
    renderSidebar();
    refreshSessions();
  }

  private void refreshSessions() {
    Gateway source = c.gateway;
    String profile = c.profileId;
    if (source == null || !source.online()) return;
    c.list(
        (v, e) -> {
          if (source != c.gateway || !profile.equals(c.profileId) || isDestroyed()) return;
          if (e == null) renderSidebar();
          else if (sessionsExpanded) toast(e);
        });
  }

  private void renderSidebar() {
    if (chatList == null) return;
    JSONArray catalog = c.sidebarSessions();
    String state =
        catalog
            + "|"
            + c.storedId
            + "|"
            + c.title
            + "|"
            + chatQuery
            + "|"
            + c.sidebarBadges()
            + c.chatFolders;
    if (state.equals(sidebarState)) return;
    sidebarState = state;
    int position = chatList.getFirstVisiblePosition();
    View top = chatList.getChildAt(0);
    int offset = top == null ? 0 : top.getTop();
    String anchor = position < sidebarRows.size() ? sidebarRows.get(position).optString("id") : "";
    sidebarRows.clear();
    String query = chatQuery.toLowerCase(Locale.ROOT).trim();
    if ("new chat".contains(query)) sidebarRows.add(obj("id", "", "title", "new chat"));
    LinkedHashMap<String, List<JSONObject>> groups = new LinkedHashMap<>();
    boolean activeListed = false;
    for (int i = 0; i < catalog.length(); i++) {
      JSONObject row = catalog.optJSONObject(i);
      if (row == null) continue;
      activeListed |= c.storedId.equals(row.optString("id"));
      if (!(row.optString("title") + " " + row.optString("preview"))
          .toLowerCase(Locale.ROOT)
          .contains(query)) continue;
      groups.computeIfAbsent(row.optString("cwd"), k -> new ArrayList<>()).add(row);
    }
    if (!activeListed && !c.storedId.isEmpty() && c.title.toLowerCase(Locale.ROOT).contains(query))
      groups
          .computeIfAbsent(c.cwd, k -> new ArrayList<>())
          .add(0, obj("id", c.storedId, "title", c.title));
    for (Map.Entry<String, List<JSONObject>> group : groups.entrySet()) {
      String key = "folder:" + group.getKey();
      sidebarRows.add(
          obj(
              "id",
              key,
              "title",
              group.getKey().isEmpty()
                  ? "Unfiled chats"
                  : c.folders.getOrDefault(group.getKey(), group.getKey()),
              "group",
              true));
      if (!collapsedFolders.contains(key) || !query.isEmpty()) sidebarRows.addAll(group.getValue());
    }
    sidebarAdapter.notifyDataSetChanged();
    for (int i = 0; i < sidebarRows.size(); i++)
      if (sidebarRows.get(i).optString("id").equals(anchor)) {
        position = i;
        break;
      }
    chatList.setSelectionFromTop(Math.min(position, Math.max(0, sidebarRows.size() - 1)), offset);
  }

  private TextView paneHeading(String title) {
    TextView heading = text(title, 14, ink);
    heading.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
    heading.setBackground(new Bevel(blue, paper, shadow, false, dp(1)));
    return heading;
  }

  private String activityValue() {
    StringBuilder value = new StringBuilder();
    for (String line : c.activity) value.append("• ").append(line).append("\n");
    return value.length() == 0 ? (c.running ? "Working" : "Ready") : value.toString().trim();
  }

  private void renderTranscript() {
    if (transcript == null || !page.equals("chat")) return;
    int oldY = chatScroll.getScrollY();
    List<JSONObject> visible = new ArrayList<>();
    for (int i = Math.max(0, c.rows.size() - 160); i < c.rows.size(); i++) {
      JSONObject item = c.rows.get(i);
      String role = item.optString("role"), body = Attachments.displayText(item);
      JSONArray media = Attachments.forRow(item);
      if (!(role.equals("user") || role.equals("assistant"))
          || (body.isBlank() && (media == null || media.length() == 0))) continue;
      visible.add(item);
      LinearLayout block = messageBlocks.get(item);
      if (block == null) {
        block = column();
        block.setPadding(dp(4), dp(4), dp(4), dp(8));
        block.setBackground(surface(paper, 0, true));
        LinearLayout meta = row();
        TextView who = text(role.equals("user") ? "You" : "Hermes", 13, ink);
        who.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        weighted(meta, who);
        TextView copy = text("Copy", 12, muted);
        copy.setMinHeight(dp(40));
        copy.setGravity(Gravity.CENTER);
        copy.setContentDescription("copy " + role + " message");
        copy.setOnClickListener(v -> copy(Attachments.displayText(item)));
        meta.addView(copy);
        meta.setBackgroundColor(blue);
        add(block, meta);
        if (media != null)
          for (int j = 0; j < media.length(); j++) {
            JSONObject file = media.optJSONObject(j);
            if (file != null) add(block, label("Attachment · " + file.optString("name", "image")));
          }
        TextView bodyView = text(body, 16, ink);
        bodyView.setTextIsSelectable(true);
        bodyView.setBackgroundColor(Color.TRANSPARENT);
        bodyView.setLineSpacing(dp(1), 1);
        add(block, bodyView);
        messageViews.put(item, bodyView);
        messageBlocks.put(item, block);
        LinearLayout.LayoutParams spacing = new LinearLayout.LayoutParams(-1, -2);
        spacing.bottomMargin = dp(6);
        block.setLayoutParams(spacing);
      }
      TextView bodyView = messageViews.get(item);
      if (!body.equals(bodyView.getTag())) {
        markdown.setMarkdown(bodyView, body);
        bodyView.setTag(body);
      }
    }
    for (JSONObject removed : new ArrayList<>(messageBlocks.keySet()))
      if (!visible.contains(removed)) {
        transcript.removeView(messageBlocks.remove(removed));
        messageViews.remove(removed);
      }
    for (int i = 0; i < visible.size(); i++) {
      View block = messageBlocks.get(visible.get(i));
      if (transcript.indexOfChild(block) != i) {
        transcript.removeView(block);
        transcript.addView(block, i);
      }
    }
    if (needsScroll || followTail) {
      chatScroll.post(() -> chatScroll.fullScroll(View.FOCUS_DOWN));
      needsScroll = false;
    } else chatScroll.post(() -> chatScroll.scrollTo(0, oldY));
  }

  @Override
  public void changed(boolean structure) {
    if (isFinishing() || isDestroyed()) return;
    if (!theme.equals("light")) {
      build();
      return;
    }
    update(structure);
  }

  private void update(boolean structure) {
    if (requestDialog != null && !c.requests.containsKey(requestDialogId)) requestDialog.dismiss();
    routeNotification();
    if (c.loaded && c.profile() == null && !c.demo && !wizardShown) {
      wizardShown = true;
      root.post(this::loginDialog);
    }
    JSONObject p = c.profile();
    connection.setText(
        (p == null ? "no server" : p.optString("name"))
            + "  ·  "
            + (c.running ? "working" : c.status));
    renderSidebar();
    if (folderButton != null) {
      String folder = c.newCwd.isEmpty() ? "Server default folder" : c.newCwd;
      folderButton.setText(folder);
      folderButton.setContentDescription("Folder for new chats: " + folder);
    }
    if (!page.equals("chat")) {
      if (!serverPageProfile.equals(c.profileId)) {
        show("server");
        return;
      }
      if (serverStatus != null) serverStatus.setText(c.status);
      return;
    }
    setSidebarVisible();
    chatTitle.setText(c.title);
    modelLabel.setText(
        (c.model.isEmpty() ? "Server default: " + c.defaultModel : c.model)
            + (c.reasoning.isEmpty() ? "" : " · " + c.reasoning)
            + "  ▾");
    if (activityText != null) activityText.setText(activityValue());
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
    renderTranscript();
  }

  private void about() {
    LinearLayout body = column();
    LinearLayout logos = row();
    ImageView lcb = new ImageView(this);
    lcb.setImageResource(R.drawable.lcb_logo);
    logos.addView(lcb, new LinearLayout.LayoutParams(dp(48), dp(48)));
    weighted(logos, text("lcb-hermes\n" + BuildConfig.VERSION_NAME, 18, ink));
    add(body, logos);
    gap(body, 16);
    ImageView nous = new ImageView(this);
    nous.setImageResource(R.drawable.nous_logo);
    nous.setAdjustViewBounds(true);
    nous.setContentDescription("Nous Research");
    body.addView(nous, new LinearLayout.LayoutParams(-1, dp(80)));
    add(
        body,
        text(
            "Powered by Hermes Agent\nWith thanks to Nous Research and the Hermes team.", 14, ink));
    TextView link = text("Hermes documentation", 14, muted);
    link.setMinHeight(dp(48));
    link.setOnClickListener(v -> openLink("https://hermes-agent.nousresearch.com/docs/"));
    add(body, link);
    add(
        body,
        label(
            "No ads, purchases, or analytics. Messages go to your server. Voice input uses"
                + " Android’s speech provider."));
    new Sheet.Builder(this)
        .setTitle("About lcb-hermes")
        .setView(body)
        .setPositiveButton("Close", null)
        .setNeutralButton("Licenses", (d, n) -> licenses())
        .show();
  }

  private void chatMenu() {
    new Sheet.Builder(this)
        .setTitle("Chat")
        .setItems(
            new String[] {
              "New chat",
              "Model and effort…",
              "Workspaces…",
              "Export chat…",
              "Rename or delete…",
              "About…",
              "Recover saved draft…"
            },
            (d, n) -> {
              switch (n) {
                case 0:
                  freshChat();
                  break;
                case 1:
                  models();
                  break;
                case 2:
                  workspaces();
                  break;
                case 3:
                  exportChat();
                  break;
                case 4:
                  if (c.gateway != null && c.gateway.online() && !c.storedId.isEmpty())
                    sessionMenu(obj("id", c.storedId, "title", c.title));
                  break;
                case 5:
                  about();
                  break;
                case 6:
                  recoverDraft();
                  break;
              }
            })
        .show();
  }

  private void recoverDraft() {
    JSONObject profile = c.profile();
    JSONArray drafts = profile == null ? null : profile.optJSONArray("retained_drafts");
    if (drafts == null || drafts.length() == 0) {
      toast("No recovered drafts.");
      return;
    }
    List<String> labels = new ArrayList<>();
    for (int i = 0; i < drafts.length(); i++) {
      JSONObject saved = drafts.optJSONObject(i);
      JSONObject draft = saved == null ? null : saved.optJSONObject("draft");
      labels.add(draft == null ? "Saved draft" : draft.optString("text", "Attachments"));
    }
    new Sheet.Builder(this)
        .setTitle("Recovered drafts")
        .setItems(
            labels.toArray(new String[0]),
            (d, n) -> {
              JSONObject saved = drafts.optJSONObject(n),
                  draft = saved == null ? null : saved.optJSONObject("draft");
              if (draft == null) return;
              new Sheet.Builder(this)
                  .setTitle("Saved draft")
                  .setMessage(draft.optString("text"))
                  .setPositiveButton("Copy text", (x, y) -> copy(draft.optString("text")))
                  .setNegativeButton("Close", null)
                  .show();
            })
        .show();
  }

  private void serverDropdown() {
    List<JSONObject> servers = new ArrayList<>();
    List<String> names = new ArrayList<>();
    for (int i = 0; i < c.profiles().length(); i++) {
      JSONObject p = c.profiles().optJSONObject(i);
      if (p != null && !p.optBoolean("hidden")) {
        servers.add(p);
        names.add((p.optString("id").equals(c.profileId) ? "✓ " : "") + p.optString("name"));
      }
    }
    int count = servers.size();
    names.add("Server monitor…");
    names.add("Server manager…");
    names.add("Add server…");
    new Sheet.Builder(this)
        .setTitle("Servers")
        .setItems(
            names.toArray(new String[0]),
            (d, n) -> {
              if (n < count) {
                c.select(servers.get(n).optString("id"));
                show("chat");
              } else if (n == count) show("server");
              else if (n == count + 1) menu();
              else loginDialog();
            })
        .show();
  }

  private void menu() {
    new Sheet.Builder(this)
        .setTitle("Server manager")
        .setMessage(c.profile() == null ? "Add your Hermes server." : c.profile().optString("name"))
        .setItems(
            new String[] {
              "Choose server…", "Add server…", "Reconnect", "Remove saved server…", "About…"
            },
            (d, n) -> {
              if (n == 0) profiles();
              else if (n == 1) loginDialog();
              else if (n == 2) c.reconnect();
              else if (n == 3 && c.profile() != null)
                new Sheet.Builder(this)
                    .setTitle("Remove saved server?")
                    .setMessage(
                        "Remove its saved login from this phone. Drafts and cached history are"
                            + " retained; add this server again to recover them.")
                    .setNegativeButton("Cancel", null)
                    .setPositiveButton("Remove", (x, y) -> c.forget())
                    .show();
              else if (n == 4) about();
            })
        .show();
  }

  private void folderPicker() {
    List<String> paths = new ArrayList<>();
    List<String> labels = new ArrayList<>();
    paths.add("");
    labels.add("Server default folder");
    for (Map.Entry<String, String> folder : c.folders.entrySet()) {
      paths.add(folder.getKey());
      labels.add(folder.getValue());
    }
    new Sheet.Builder(this)
        .setTitle("Folder for new chats")
        .setItems(labels.toArray(new String[0]), (d, n) -> c.chooseFolder(paths.get(n)))
        .setNeutralButton("Workspaces…", (d, n) -> workspaces())
        .show();
  }

  private void workspaces() {
    if (c.gateway == null || !c.gateway.online()) {
      toast("Connect to manage server workspaces.");
      return;
    }
    List<String> labels = new ArrayList<>();
    List<JSONObject> projects = new ArrayList<>();
    for (int i = 0; i < c.projects.length(); i++) {
      JSONObject p = c.projects.optJSONObject(i);
      if (p != null && !p.optBoolean("archived")) {
        labels.add(p.optString("name", p.optString("slug")));
        projects.add(p);
      }
    }
    labels.add("Register workspace…");
    labels.add("Choose folder for new chats…");
    int count = projects.size();
    new Sheet.Builder(this)
        .setTitle("Server workspaces")
        .setItems(
            labels.toArray(new String[0]),
            (d, n) -> {
              if (n == count + 1) folderPicker();
              else workspaceEditor(n < count ? projects.get(n) : null);
            })
        .setNeutralButton("Refresh", (d, n) -> c.refreshWorkspaces())
        .show();
  }

  private void workspaceEditor(JSONObject project) {
    LinearLayout body = column();
    EditText name = project == null ? field(body, "Workspace name", "", false) : null;
    if (project != null)
      add(
          body,
          text(
              project.optJSONArray("folders") == null
                  ? ""
                  : project.optJSONArray("folders").toString(),
              13,
              muted));
    EditText path = field(body, "Existing folder on the server", "", false);
    add(body, label("Use a folder that already exists on your Hermes server."));
    TextView error = label("");
    add(body, error);
    Gateway source = c.gateway;
    Sheet dialog =
        new Sheet.Builder(this)
            .setTitle(
                project == null
                    ? "Register workspace"
                    : "Attach folder to " + project.optString("name"))
            .setView(body)
            .setNegativeButton("Cancel", null)
            .setPositiveButton("Save", null)
            .create();
    dialog.setOnShowListener(
        d ->
            dialog
                .getButton(-1)
                .setOnClickListener(
                    v -> {
                      String folder = path.getText().toString().trim();
                      if (folder.isEmpty()) {
                        error.setText("Enter an existing server folder.");
                        return;
                      }
                      JSONObject params =
                          project == null
                              ? obj(
                                  "name",
                                  name.getText().toString().trim(),
                                  "folders",
                                  new JSONArray().put(folder),
                                  "primary_path",
                                  folder)
                              : obj(
                                  "id",
                                  project.optString("id"),
                                  "path",
                                  folder,
                                  "is_primary",
                                  false);
                      if (project == null && name.getText().toString().trim().isEmpty()) {
                        error.setText("Enter a workspace name.");
                        return;
                      }
                      dialog.getButton(-1).setEnabled(false);
                      source.rpc(
                          project == null ? "projects.create" : "projects.add_folder",
                          params,
                          (r, e) -> {
                            if (source != c.gateway || isDestroyed()) return;
                            if (e != null) {
                              error.setText(e);
                              dialog.getButton(-1).setEnabled(true);
                            } else {
                              c.chooseFolder(folder);
                              c.refreshWorkspaces();
                              dialog.dismiss();
                            }
                          });
                    }));
    dialog.show();
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
    EditText url = field(box, "dashboard URL", "", false);
    url.setInputType(
        android.text.InputType.TYPE_CLASS_TEXT | android.text.InputType.TYPE_TEXT_VARIATION_URI);
    EditText user = field(box, "username", "", false);
    EditText pass = field(box, "password", "", true);
    name.setHint("a name for this server");
    url.setHint("https://server:port");
    user.setHint("");
    pass.setHint("");
    CheckBox http = new CheckBox(this);
    http.setText("allow private HTTP (try automatically)");
    http.setTextSize(13);
    http.setTextColor(ink);
    http.setChecked(false);
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
    serverDropdown();
  }

  private void sessionMenu(JSONObject session) {
    String id = session.optString("id");
    new Sheet.Builder(this)
        .setTitle(session.optString("title", "chat"))
        .setItems(
            new String[] {"rename", "delete"},
            (d, n) -> {
              if (n == 1
                  && (c.chatBadge(id).equals("working")
                      || c.chatBadge(id).equals("answer needed"))) {
                toast("Stop this task before deleting its chat.");
                return;
              }
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
                                    c.removedChat(id);
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
    String chat = c.storedId;
    source.rpc(
        "model.options",
        obj("include_unconfigured", false),
        (options, e) -> {
          modelPickerPending = false;
          if (source != c.gateway || isDestroyed() || !chat.equals(c.storedId)) return;
          if (e != null) {
            toast(e);
            return;
          }
          List<JSONObject> all = Workbench.models(options, c.defaultModel),
              filtered = new ArrayList<>(all);
          LinearLayout body = column();
          EditText search = field(body, "Search models and providers", "", false);
          ListView list = new ListView(this);
          add(body, list);
          list.getLayoutParams().height = dp(compact() ? 140 : 260);
          TextView detail = label("Select a model. Changes apply to this chat.");
          add(body, detail);
          Spinner effort = new Spinner(this);
          effort.setContentDescription("reasoning effort");
          add(body, effort);
          JSONObject[] choice = {null};
          List<String> levels = new ArrayList<>();
          ArrayAdapter<String> adapter =
              new ArrayAdapter<>(
                  this, android.R.layout.simple_list_item_activated_1, new ArrayList<>());
          Runnable fill =
              () -> {
                adapter.clear();
                for (JSONObject m : filtered)
                  adapter.add(m.optString("label") + "\n" + m.optString("provider"));
                adapter.notifyDataSetChanged();
              };
          list.setAdapter(adapter);
          list.setChoiceMode(ListView.CHOICE_MODE_SINGLE);
          fill.run();
          java.util.function.Consumer<JSONObject> select =
              m -> {
                choice[0] = m;
                levels.clear();
                levels.addAll(Workbench.efforts(m, c.defaultModel));
                List<String> labels = new ArrayList<>();
                for (String level : levels)
                  labels.add(
                      level.isEmpty()
                          ? (chat.isEmpty() ? "Server default effort" : "Keep current effort")
                          : level.equals("xhigh")
                              ? "Extra high"
                              : level.equals("max") ? "Maximum" : level);
                effort.setAdapter(
                    new ArrayAdapter<>(
                        this, android.R.layout.simple_spinner_dropdown_item, labels));
                effort.setSelection(Math.max(0, levels.indexOf(c.reasoning)));
                effort.setEnabled(levels.size() > 1);
                detail.setText(
                    m.optString("label")
                        + " · "
                        + m.optString("provider")
                        + (c.running ? "\nApplies to the next turn." : "\nApplies to this chat.")
                        + (levels.size() == 1 ? " No reasoning control reported." : ""));
              };
          list.setOnItemClickListener((parent, v, n, id) -> select.accept(filtered.get(n)));
          search.addTextChangedListener(
              new TextWatcher() {
                public void beforeTextChanged(CharSequence t, int a, int b, int d) {}

                public void afterTextChanged(Editable t) {}

                public void onTextChanged(CharSequence t, int a, int b, int d) {
                  filtered.clear();
                  for (JSONObject m : all)
                    if ((m.optString("label") + m.optString("provider"))
                        .toLowerCase(Locale.ROOT)
                        .contains(t.toString().toLowerCase(Locale.ROOT))) filtered.add(m);
                  fill.run();
                }
              });
          for (int i = 0; i < all.size(); i++)
            if (all.get(i).optString("model").equals(c.model)
                && (c.provider.isEmpty() || all.get(i).optString("provider").equals(c.provider))) {
              select.accept(all.get(i));
              list.setItemChecked(i, true);
              list.setSelection(i);
              break;
            }
          modelDialog =
              new Sheet.Builder(this)
                  .setTitle("Model and effort")
                  .setView(body)
                  .setNegativeButton("Cancel", null)
                  .setPositiveButton("Apply", null)
                  .create();
          modelDialog.setOnShowListener(
              d ->
                  modelDialog
                      .getButton(-1)
                      .setOnClickListener(
                          v -> {
                            if (choice[0] == null) {
                              detail.setText("Select a model first.");
                              return;
                            }
                            if (source != c.gateway || !chat.equals(c.storedId)) {
                              detail.setText("Chat changed. Reopen this picker.");
                              return;
                            }
                            applyModel(
                                choice[0],
                                levels.get(effort.getSelectedItemPosition()),
                                false,
                                detail);
                          }));
          modelDialog.show();
        });
  }

  private void applyModel(JSONObject choice, String effort, boolean confirmed, TextView detail) {
    String chat = c.storedId;
    Gateway source = c.gateway;
    modelDialog.getButton(-1).setEnabled(false);
    c.chooseModel(
        choice.optString("model"),
        choice.optString("provider"),
        effort,
        confirmed,
        (v, e) -> {
          if (isDestroyed() || modelDialog == null || !modelDialog.isShowing()) return;
          modelDialog.getButton(-1).setEnabled(true);
          if (e != null) {
            detail.setText(e);
            return;
          }
          if (v.optBoolean("confirm_required"))
            new Sheet.Builder(this)
                .setTitle("Confirm model selection")
                .setMessage(
                    v.optString(
                        "confirm_message",
                        v.optString("warning", "Confirm this model’s cost before continuing.")))
                .setNegativeButton("Cancel", null)
                .setPositiveButton(
                    "Confirm",
                    (d, n) -> {
                      if (source == c.gateway && chat.equals(c.storedId))
                        applyModel(choice, effort, true, detail);
                    })
                .show();
          else modelDialog.dismiss();
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
    String state =
        c.storedId + c.attachments + "|" + c.uploadsPending + "|" + c.running + c.submissionPending;
    if (state.equals(trayState)) return;
    trayState = state;
    attachmentTray.removeAllViews();
    if (tightTyping) {
      if (c.attachments.length() > 0 || c.uploadsPending > 0) {
        TextView summary =
            label(
                c.attachments.length()
                    + " attachment"
                    + (c.attachments.length() == 1 ? "" : "s")
                    + (c.uploadsPending > 0 ? " · loading…" : " · view"));
        summary.setSingleLine(true);
        summary.setEllipsize(TextUtils.TruncateAt.END);
        summary.setContentDescription("view draft attachments");
        summary.setOnClickListener(v -> hideKeyboard());
        add(attachmentTray, summary);
        attachmentTray.setVisibility(View.VISIBLE);
      } else attachmentTray.setVisibility(View.GONE);
      return;
    }
    if (c.attachments.length() > 0) add(attachmentTray, paneHeading("Attachments"));
    renderImages(attachmentTray, c.attachments, true);
    if (c.uploadsPending > 0) add(attachmentTray, label("loading attachment…"));
    attachmentTray.setVisibility(
        c.attachments.length() == 0 && c.uploadsPending == 0 ? View.GONE : View.VISIBLE);
  }

  private void renderImages(LinearLayout parent, JSONArray attachments, boolean pending) {
    if (attachments == null || attachments.length() == 0) return;
    HorizontalScrollView scroll = new HorizontalScrollView(this);
    scroll.setHorizontalScrollBarEnabled(true);
    LinearLayout tiles = row();
    scroll.addView(tiles);
    add(parent, scroll);
    for (int i = 0; i < attachments.length(); i++) {
      JSONObject file = attachments.optJSONObject(i);
      if (file == null) continue;
      LinearLayout tile = row();
      tile.setPadding(dp(4), dp(4), dp(6), dp(4));
      tile.setBackground(surface(paper, 0, true));
      Bitmap bitmap = Attachments.bitmap(file);
      ImageView image = new ImageView(this);
      if (bitmap != null) image.setImageBitmap(bitmap);
      else image.setImageDrawable(new Glyph("attach", ink));
      image.setScaleType(ImageView.ScaleType.CENTER_CROP);
      image.setContentDescription("attachment preview: " + file.optString("name"));
      tile.addView(
          image, new LinearLayout.LayoutParams(dp(compact() ? 40 : 64), dp(compact() ? 40 : 56)));
      if (bitmap != null)
        image.setOnClickListener(
            v -> {
              ImageView full = new ImageView(this);
              full.setImageBitmap(bitmap);
              full.setAdjustViewBounds(true);
              full.setMaxHeight(dp(460));
              new Sheet.Builder(this)
                  .setTitle(file.optString("name", "image"))
                  .setView(full)
                  .setPositiveButton("Close", null)
                  .show();
            });
      TextView name = text(file.optString("name", "file"), 13, ink);
      name.setMaxLines(2);
      name.setMaxWidth(dp(150));
      name.setEllipsize(TextUtils.TruncateAt.END);
      tile.addView(name);
      Button remove =
          iconButton(
              "close",
              "remove attachment " + file.optString("name"),
              () -> c.removeAttachment(file.optString("id")));
      remove.setEnabled(!c.running && !c.submissionPending);
      tile.addView(remove);
      LinearLayout.LayoutParams space = new LinearLayout.LayoutParams(-2, -2);
      space.rightMargin = dp(6);
      tiles.addView(tile, space);
    }
  }

  private void appendDraft(String value) {
    c.setDraft(c.draft + (c.draft.isEmpty() ? "" : "\n\n") + value);
    update(false);
  }

  private Button module(String glyph, String title, String description, Runnable click) {
    Button b = button(title, click);
    b.setContentDescription(description);
    b.setTextSize(11);
    b.setPadding(dp(4), dp(10), dp(4), dp(8));
    b.setMinHeight(dp(62));
    b.setGravity(Gravity.CENTER);
    Glyph icon = new Glyph(glyph, ink);
    icon.setBounds(0, 0, dp(22), dp(22));
    b.setCompoundDrawables(null, icon, null, null);
    b.setCompoundDrawablePadding(dp(6));
    b.setBackground(
        new RippleDrawable(
            android.content.res.ColorStateList.valueOf(blue),
            surface(Color.TRANSPARENT, 8, false),
            null));
    return b;
  }

  private void server() {
    serverPageProfile = c.profileId;
    LinearLayout top = row();
    top.addView(iconButton("back", "back to chat", () -> show("chat")));
    weighted(top, text("server", 18, ink));
    top.addView(iconButton("more", "settings", this::menu));
    add(content, top);
    ScrollView scroll = new ScrollView(this);
    LinearLayout body = column();
    scroll.addView(body);
    content.addView(scroll, new LinearLayout.LayoutParams(-1, 0, 1));
    LinearLayout actions = row();
    actions.addView(iconButton("server", "server manager", this::menu));
    actions.addView(
        iconButton(
            "dashboard",
            "open dashboard",
            () -> {
              if (c.profile() != null) openLink(c.profile().optString("url"));
            }));
    actions.addView(iconButton("about", "about lcb-hermes", this::about));
    add(body, actions);
    gap(body, 8);
    LinearLayout card = column();
    card.setPadding(dp(8), dp(10), dp(8), dp(10));
    card.setBackground(surface(paper, 12, true));
    add(body, card);
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
    gap(body, 8);
    LinearLayout statsHeader = row();
    TextView heading = text("server stats", 16, ink);
    heading.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
    weighted(statsHeader, heading);
    statsHeader.addView(iconButton("refresh", "refresh server stats", this::refreshServerStats));
    add(body, statsHeader);
    statsNotice = label("loading…");
    statsNotice.setContentDescription("server stats status");
    add(body, statsNotice);
    statsRows = column();
    add(body, statsRows);
    gap(body, 8);
    add(body, label("System figures are reported by Hermes. Process memory is Hermes itself."));
    gap(body, 8);
    if (started) startServerStats();
  }

  private void startServerStats() {
    screenHandler.removeCallbacks(pollStats);
    refreshServerStats();
    screenHandler.postDelayed(pollStats, 5000);
  }

  private void stopServerStats() {
    screenHandler.removeCallbacks(pollStats);
    statsGeneration++;
    statsLoading = false;
  }

  private void refreshServerStats() {
    if (!started || statsRows == null) return;
    Gateway source = c.gateway;
    if (source == null || !source.online()) {
      statsNotice.setText("Connect to read server stats.");
      return;
    }
    if (statsLoading && statsSource == source) return;
    statsLoading = true;
    statsSource = source;
    int generation = ++statsGeneration;
    source.rest(
        "/api/system/stats",
        null,
        (v, e) -> {
          if (!started || statsRows == null || generation != statsGeneration || source != c.gateway)
            return;
          statsLoading = false;
          if (e != null) {
            statsNotice.setText("Stats unavailable. " + e);
            return;
          }
          statsRows.removeAllViews();
          add(statsRows, paneHeading("Hermes " + v.optString("hermes_version")));
          metricBar(statsRows, "CPU", v.optDouble("cpu_percent", Double.NaN));
          JSONObject memory = v.optJSONObject("memory"), disk = v.optJSONObject("disk");
          metricBar(
              statsRows,
              "Memory",
              memory == null ? Double.NaN : memory.optDouble("percent", Double.NaN));
          metricBar(
              statsRows, "Disk", disk == null ? Double.NaN : disk.optDouble("percent", Double.NaN));
          add(statsRows, label("Server default model\n" + c.defaultModel));
          if (!page.equals("server")) {
            JSONObject process = v.optJSONObject("process");
            if (process != null && process.has("rss"))
              add(
                  statsRows,
                  label("Hermes process · " + ServerStats.size(process.optDouble("rss"))));
            statsNotice.setText("Live · server host");
            return;
          }
          Map<String, LinkedHashMap<String, String>> sections =
              ServerStats.sections(v, System.currentTimeMillis() / 1000);
          for (Map.Entry<String, LinkedHashMap<String, String>> section : sections.entrySet()) {
            LinearLayout panel = column();
            panel.setPadding(dp(6), dp(6), dp(6), dp(8));
            panel.setBackground(surface(paper, 10, true));
            TextView cap = text(section.getKey(), 15, ink);
            cap.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
            add(panel, cap);
            for (Map.Entry<String, String> item : section.getValue().entrySet()) {
              LinearLayout metric = row();
              metric.setGravity(Gravity.TOP);
              weighted(metric, text(item.getKey(), 13, muted));
              TextView value = text(item.getValue(), 13, ink);
              value.setGravity(Gravity.END);
              value.setTextIsSelectable(true);
              weighted(metric, value);
              add(panel, metric);
            }
            add(statsRows, panel);
            gap(statsRows, 8);
          }
          statsNotice.setText(
              sections.isEmpty()
                  ? "Stats unavailable on this server."
                  : "live · refreshes every 5 seconds");
        });
  }

  private void metricBar(LinearLayout parent, String name, double percent) {
    if (Double.isNaN(percent)) return;
    TextView heading = text(name + "  ·  " + ServerStats.percent(percent), 13, ink);
    Glyph icon = new Glyph(name.equals("Disk") ? "folder" : "resource", ink);
    icon.setBounds(0, 0, dp(14), dp(14));
    heading.setCompoundDrawables(icon, null, null, null);
    heading.setCompoundDrawablePadding(dp(6));
    add(parent, heading);
    ProgressBar bar = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
    bar.setMax(1000);
    bar.setProgress((int) Math.max(0, Math.min(1000, percent * 10)));
    bar.setProgressTintList(android.content.res.ColorStateList.valueOf(accent));
    bar.setProgressBackgroundTintList(android.content.res.ColorStateList.valueOf(face));
    bar.setContentDescription(name + " usage " + ServerStats.percent(percent));
    parent.addView(bar, new LinearLayout.LayoutParams(-1, dp(8)));
    gap(parent, 8);
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
      requestDialog = dialog;
      requestDialogId = r.optString("id");
      dialog.setOnCancelListener(d -> c.answer(r, obj("choice", "deny")));
      dialog.setOnDismissListener(
          d -> {
            dialogOpen = false;
            if (requestDialog == dialog) requestDialog = null;
          });
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
    if (!c.requests.containsKey(request.optString("id"))) return;
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
    requestDialog = dialog;
    requestDialogId = request.optString("id");
    dialog.setOnCancelListener(d -> c.answer(request, obj("answer", "")));
    dialog.setOnDismissListener(
        d -> {
          dialogOpen = false;
          if (requestDialog == dialog) requestDialog = null;
        });
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
