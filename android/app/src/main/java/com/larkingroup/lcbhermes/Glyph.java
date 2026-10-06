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
      case "folder":
        path.moveTo(3, 7);
        path.lineTo(10, 7);
        path.lineTo(12, 10);
        path.lineTo(21, 10);
        path.lineTo(21, 20);
        path.lineTo(3, 20);
        path.close();
        c.drawPath(path, p);
        break;
      case "resource":
        c.drawRect(6, 6, 18, 18, p);
        c.drawRect(9, 9, 15, 15, p);
        for (int i = 8; i <= 16; i += 4) {
          c.drawLine(i, 3, i, 6, p);
          c.drawLine(i, 18, i, 21, p);
          c.drawLine(3, i, 6, i, p);
          c.drawLine(18, i, 21, i, p);
        }
        break;
      case "sidebar":
        c.drawRoundRect(3, 4, 21, 20, 1, 1, p);
        c.drawLine(10, 4, 10, 20, p);
        c.drawLine(6, 8, 7, 8, p);
        c.drawLine(6, 12, 7, 12, p);
        break;
      case "new":
        c.drawLine(12, 5, 12, 19, p);
        c.drawLine(5, 12, 19, 12, p);
        break;
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
      case "about":
        c.drawCircle(12, 12, 9, p);
        c.drawLine(12, 11, 12, 17, p);
        p.setStyle(Paint.Style.FILL);
        c.drawCircle(12, 7, 1, p);
        break;
      case "sun":
        c.drawCircle(12, 12, 4, p);
        for (int i = 0; i < 8; i++) {
          double angle = i * Math.PI / 4;
          c.drawLine(
              12 + (float) Math.cos(angle) * 7,
              12 + (float) Math.sin(angle) * 7,
              12 + (float) Math.cos(angle) * 10,
              12 + (float) Math.sin(angle) * 10,
              p);
        }
        break;
      case "moon":
        path.moveTo(17, 3);
        path.cubicTo(3, 0, 0, 20, 14, 21);
        path.cubicTo(19, 21, 22, 17, 22, 13);
        path.cubicTo(12, 18, 9, 8, 17, 3);
        c.drawPath(path, p);
        break;
      case "dashboard":
        c.drawRoundRect(3, 4, 21, 20, 2, 2, p);
        c.drawLine(3, 9, 21, 9, p);
        c.drawLine(9, 9, 9, 20, p);
        break;
      case "refresh":
        c.drawArc(4, 4, 20, 20, 35, 290, false, p);
        c.drawLine(20, 4, 20, 10, p);
        c.drawLine(14, 10, 20, 10, p);
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
