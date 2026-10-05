package com.larkingroup.lcbhermes;

import android.app.Dialog;
import android.content.Context;
import android.content.DialogInterface;
import android.content.res.ColorStateList;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.ColorDrawable;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.RippleDrawable;
import android.text.SpannableString;
import android.text.Spanned;
import android.text.style.ForegroundColorSpan;
import android.text.style.RelativeSizeSpan;
import android.text.style.StyleSpan;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.view.Window;
import android.view.WindowManager;
import android.widget.*;
import java.util.LinkedHashMap;
import java.util.Map;

/** Small native sheet shared by connection, choices, and confirmations. */
final class Sheet extends Dialog {
  private final Map<Integer, Button> buttons = new LinkedHashMap<>();
  private final Builder spec;

  private Sheet(Builder spec) {
    super(spec.context);
    this.spec = spec;
    requestWindowFeature(Window.FEATURE_NO_TITLE);
    boolean dark =
        ((HermesApp) spec.context.getApplicationContext())
            .controller
            .data
            .optString("theme")
            .equals("dark");
    int paper = Color.parseColor(dark ? "#1f2930" : "#faf9f4");
    int ink = Color.parseColor(dark ? "#eef2f1" : "#22323d");
    int line = Color.parseColor(dark ? "#43515b" : "#d6d9d6");
    LinearLayout body = new LinearLayout(spec.context);
    body.setOrientation(LinearLayout.VERTICAL);
    body.setPadding(dp(20), dp(12), dp(20), dp(16));
    GradientDrawable background = new GradientDrawable();
    background.setColor(paper);
    background.setCornerRadius(dp(16));
    background.setStroke(dp(1), line);
    body.setBackground(background);
    View handle = new View(spec.context);
    GradientDrawable grip = new GradientDrawable();
    grip.setColor(line);
    grip.setCornerRadius(dp(2));
    handle.setBackground(grip);
    LinearLayout.LayoutParams hp = new LinearLayout.LayoutParams(dp(28), dp(3));
    hp.gravity = Gravity.CENTER;
    hp.bottomMargin = dp(16);
    body.addView(handle, hp);
    if (spec.title != null) {
      TextView title = text(spec.title, 18, ink);
      title.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
      title.setPadding(0, 0, 0, dp(14));
      body.addView(title);
    }
    if (spec.message != null) {
      TextView message = text(spec.message, 14, ink);
      message.setPadding(0, 0, 0, dp(16));
      body.addView(message);
    }
    if (spec.view != null) body.addView(spec.view, new LinearLayout.LayoutParams(-1, -2));
    if (spec.items != null) {
      LinearLayout choices = new LinearLayout(spec.context);
      choices.setOrientation(LinearLayout.VERTICAL);
      for (int i = 0; i < spec.items.length; i++) {
        int index = i;
        TextView choice = text(spec.items[i], 14, ink);
        int split = spec.items[i].indexOf('\n');
        if (split >= 0) {
          SpannableString lines = new SpannableString(spec.items[i]);
          lines.setSpan(new StyleSpan(Typeface.BOLD), 0, split, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
          lines.setSpan(
              new RelativeSizeSpan(.85f),
              split + 1,
              lines.length(),
              Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
          lines.setSpan(
              new ForegroundColorSpan(
                  dark ? Color.parseColor("#aab9bf") : Color.parseColor("#61727a")),
              split + 1,
              lines.length(),
              Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
          choice.setText(lines);
        }
        choice.setGravity(Gravity.CENTER_VERTICAL);
        choice.setMinHeight(dp(48));
        choice.setPadding(dp(4), dp(10), dp(4), dp(10));
        choice.setBackground(
            new RippleDrawable(ColorStateList.valueOf(line), new ColorDrawable(paper), null));
        choice.setOnClickListener(
            v -> {
              dismiss();
              if (spec.itemClick != null) spec.itemClick.onClick(this, index);
            });
        choices.addView(choice, new LinearLayout.LayoutParams(-1, -2));
        if (i < spec.items.length - 1) {
          View rule = new View(spec.context);
          rule.setBackgroundColor(line);
          choices.addView(rule, new LinearLayout.LayoutParams(-1, dp(1)));
        }
      }
      ScrollView list = new ScrollView(spec.context);
      list.setFillViewport(false);
      list.addView(choices);
      body.addView(
          list, new LinearLayout.LayoutParams(-1, Math.min(dp(420), dp(49) * spec.items.length)));
    }
    if (!spec.labels.isEmpty()) {
      LinearLayout actions = new LinearLayout(spec.context);
      actions.setGravity(Gravity.END);
      actions.setPadding(0, dp(16), 0, 0);
      for (int which : new int[] {-3, -2, -1}) {
        if (!spec.labels.containsKey(which)) continue;
        Button b = new Button(spec.context);
        b.setText(spec.labels.get(which));
        b.setAllCaps(false);
        b.setTextSize(14);
        b.setMinHeight(dp(44));
        b.setMinimumHeight(dp(44));
        b.setMinWidth(0);
        b.setMinimumWidth(0);
        b.setPadding(dp(16), 0, dp(16), 0);
        b.setTextColor(which == -1 ? Color.WHITE : ink);
        GradientDrawable fill = new GradientDrawable();
        fill.setCornerRadius(dp(9));
        fill.setColor(which == -1 ? Color.parseColor("#315870") : Color.TRANSPARENT);
        b.setBackground(new RippleDrawable(ColorStateList.valueOf(line), fill, null));
        b.setOnClickListener(
            v -> {
              DialogInterface.OnClickListener cb = spec.clicks.get(which);
              if (cb != null) cb.onClick(this, which);
              dismiss();
            });
        buttons.put(which, b);
        actions.addView(b);
      }
      body.addView(actions);
    }
    ScrollView outer = new ScrollView(spec.context);
    outer.setFillViewport(false);
    outer.addView(body);
    setContentView(outer);
    Window window = getWindow();
    window.setBackgroundDrawable(new ColorDrawable(Color.TRANSPARENT));
    window.addFlags(WindowManager.LayoutParams.FLAG_DIM_BEHIND);
    window.setDimAmount(.25f);
    window.setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_ADJUST_RESIZE);
  }

  private int dp(float n) {
    return Math.round(n * getContext().getResources().getDisplayMetrics().density);
  }

  private TextView text(String value, int size, int color) {
    TextView v = new TextView(getContext());
    v.setText(value);
    v.setTextSize(size);
    v.setTextColor(color);
    return v;
  }

  Button getButton(int which) {
    return buttons.get(which);
  }

  @Override
  public void show() {
    super.show();
    Window w = getWindow();
    w.setGravity(Gravity.BOTTOM | Gravity.CENTER_HORIZONTAL);
    int width =
        Math.min(dp(520), getContext().getResources().getDisplayMetrics().widthPixels - dp(20));
    w.setLayout(width, ViewGroup.LayoutParams.WRAP_CONTENT);
    WindowManager.LayoutParams p = w.getAttributes();
    p.y = dp(10);
    w.setAttributes(p);
    View content = w.getDecorView();
    content.setPadding(0, 0, 0, 0);
    int max = getContext().getResources().getDisplayMetrics().heightPixels * 4 / 5;
    content.post(
        () -> {
          if (content.getHeight() > max) w.setLayout(width, max);
        });
  }

  static final class Builder {
    final Context context;
    String title, message;
    View view;
    String[] items;
    DialogInterface.OnClickListener itemClick;
    final Map<Integer, String> labels = new LinkedHashMap<>();
    final Map<Integer, DialogInterface.OnClickListener> clicks = new LinkedHashMap<>();

    Builder(Context context) {
      this.context = context;
    }

    Builder setTitle(String value) {
      title = value;
      return this;
    }

    Builder setMessage(String value) {
      message = value;
      return this;
    }

    Builder setView(View value) {
      view = value;
      return this;
    }

    Builder setItems(String[] value, DialogInterface.OnClickListener cb) {
      items = value;
      itemClick = cb;
      return this;
    }

    private Builder action(int which, String value, DialogInterface.OnClickListener cb) {
      labels.put(which, value);
      clicks.put(which, cb);
      return this;
    }

    Builder setPositiveButton(String value, DialogInterface.OnClickListener cb) {
      return action(-1, value, cb);
    }

    Builder setNegativeButton(String value, DialogInterface.OnClickListener cb) {
      return action(-2, value, cb);
    }

    Builder setNeutralButton(String value, DialogInterface.OnClickListener cb) {
      return action(-3, value, cb);
    }

    Sheet create() {
      return new Sheet(this);
    }

    Sheet show() {
      Sheet d = create();
      d.show();
      return d;
    }
  }
}
