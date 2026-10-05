package com.larkingroup.lcbhermes;

import android.graphics.*;
import android.graphics.drawable.Drawable;

/** Consistent small line icons; no font glyph or emoji dependency. */
final class Glyph extends Drawable {
  final String name;
  final int color;
  final Paint p = new Paint(Paint.ANTI_ALIAS_FLAG);

  Glyph(String name, int color) {
    this.name = name;
    this.color = color;
  }

  @Override
  public void draw(Canvas c) {
    Rect b = getBounds();
    c.save();
    c.translate(b.left, b.top);
    c.scale(b.width() / 24f, b.height() / 24f);
    p.setColor(color);
    p.setStyle(Paint.Style.STROKE);
    p.setStrokeWidth(1.7f);
    p.setStrokeCap(Paint.Cap.ROUND);
    p.setStrokeJoin(Paint.Join.ROUND);
    Path path = new Path();
    switch (name) {
      case "server":
        c.drawRoundRect(3, 4, 21, 16, 1.5f, 1.5f, p);
        c.drawLine(12, 16, 12, 20, p);
        c.drawLine(8, 20, 16, 20, p);
        break;
      case "send":
        c.drawLine(12, 19, 12, 5, p);
        c.drawLine(6, 11, 12, 5, p);
        c.drawLine(18, 11, 12, 5, p);
        break;
      case "stop":
        p.setStyle(Paint.Style.FILL);
        c.drawRoundRect(6, 6, 18, 18, 2, 2, p);
        break;
      case "attach":
        path.moveTo(9, 17);
        path.lineTo(16, 10);
        path.cubicTo(19, 7, 15, 3, 12, 6);
        path.lineTo(5, 13);
        path.cubicTo(0, 18, 7, 25, 12, 20);
        path.lineTo(20, 12);
        c.drawPath(path, p);
        break;
      case "voice":
        c.drawRoundRect(9, 3, 15, 14, 3, 3, p);
        c.drawArc(6, 7, 18, 18, 0, 180, false, p);
        c.drawLine(12, 18, 12, 21, p);
        c.drawLine(8, 21, 16, 21, p);
        break;
      case "back":
        c.drawLine(16, 5, 9, 12, p);
        c.drawLine(9, 12, 16, 19, p);
        break;
      case "close":
        c.drawLine(7, 7, 17, 17, p);
        c.drawLine(17, 7, 7, 17, p);
        break;
      default:
        p.setStyle(Paint.Style.FILL);
        for (int x = 5; x <= 19; x += 7) c.drawCircle(x, 12, 1.3f, p);
    }
    c.restore();
  }

  @Override
  public void setAlpha(int alpha) {
    p.setAlpha(alpha);
  }

  @Override
  public void setColorFilter(ColorFilter f) {
    p.setColorFilter(f);
  }

  @Override
  public int getOpacity() {
    return PixelFormat.TRANSLUCENT;
  }
}
