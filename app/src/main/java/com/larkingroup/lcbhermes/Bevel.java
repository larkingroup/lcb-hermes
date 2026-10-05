package com.larkingroup.lcbhermes;

import android.graphics.*;
import android.graphics.drawable.Drawable;

final class Bevel extends Drawable {
  private final Paint p = new Paint();
  private final int face, light, shadow;
  private final boolean inset;

  Bevel(int face, int light, int shadow, boolean inset) {
    this.face = face;
    this.light = light;
    this.shadow = shadow;
    this.inset = inset;
  }

  @Override
  public void draw(Canvas c) {
    Rect b = getBounds();
    p.setColor(face);
    c.drawRect(b, p);
    p.setStrokeWidth(2);
    p.setColor(inset ? shadow : light);
    c.drawLine(b.left, b.bottom - 1, b.left, b.top, p);
    c.drawLine(b.left, b.top, b.right - 1, b.top, p);
    p.setColor(inset ? light : shadow);
    c.drawLine(b.right - 1, b.top, b.right - 1, b.bottom - 1, p);
    c.drawLine(b.right - 1, b.bottom - 1, b.left, b.bottom - 1, p);
  }

  @Override
  public void setAlpha(int a) {
    p.setAlpha(a);
  }

  @Override
  public void setColorFilter(ColorFilter f) {
    p.setColorFilter(f);
  }

  @Override
  public int getOpacity() {
    return PixelFormat.OPAQUE;
  }
}
