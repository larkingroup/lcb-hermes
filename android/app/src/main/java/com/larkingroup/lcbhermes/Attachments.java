package com.larkingroup.lcbhermes;

import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Matrix;
import android.media.ExifInterface;
import android.util.Base64;
import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import org.json.JSONArray;
import org.json.JSONObject;

final class Attachments {
  static String thumbnail(byte[] bytes, String mime) {
    if (!mime.startsWith("image/") || mime.contains("svg")) return "";
    BitmapFactory.Options options = new BitmapFactory.Options();
    options.inJustDecodeBounds = true;
    BitmapFactory.decodeByteArray(bytes, 0, bytes.length, options);
    if (options.outWidth <= 0 || options.outHeight <= 0) return "";
    options.inSampleSize = 1;
    while (Math.max(options.outWidth, options.outHeight) / options.inSampleSize > 720)
      options.inSampleSize *= 2;
    options.inJustDecodeBounds = false;
    Bitmap bitmap = BitmapFactory.decodeByteArray(bytes, 0, bytes.length, options);
    if (bitmap == null) return "";
    try {
      ExifInterface exif = new ExifInterface(new ByteArrayInputStream(bytes));
      int orientation = exif.getAttributeInt(ExifInterface.TAG_ORIENTATION, 1);
      Matrix matrix = new Matrix();
      switch (orientation) {
        case 2:
          matrix.setScale(-1, 1);
          break;
        case 3:
          matrix.setRotate(180);
          break;
        case 4:
          matrix.setScale(1, -1);
          break;
        case 5:
          matrix.setRotate(90);
          matrix.postScale(-1, 1);
          break;
        case 6:
          matrix.setRotate(90);
          break;
        case 7:
          matrix.setRotate(-90);
          matrix.postScale(-1, 1);
          break;
        case 8:
          matrix.setRotate(-90);
          break;
        default:
          break;
      }
      if (!matrix.isIdentity()) {
        Bitmap oriented =
            Bitmap.createBitmap(bitmap, 0, 0, bitmap.getWidth(), bitmap.getHeight(), matrix, true);
        if (oriented != bitmap) bitmap.recycle();
        bitmap = oriented;
      }
    } catch (Exception ignored) {
    }
    ByteArrayOutputStream out = new ByteArrayOutputStream();
    bitmap.compress(Bitmap.CompressFormat.JPEG, 78, out);
    bitmap.recycle();
    return Base64.encodeToString(out.toByteArray(), Base64.NO_WRAP);
  }

  static Bitmap bitmap(JSONObject attachment) {
    try {
      String encoded = attachment.optString("thumbnail");
      if (encoded.isEmpty() || encoded.length() > 1024 * 1024) return null;
      byte[] bytes = Base64.decode(encoded, Base64.DEFAULT);
      return BitmapFactory.decodeByteArray(bytes, 0, bytes.length);
    } catch (Exception error) {
      return null;
    }
  }

  static String displayText(JSONObject row) {
    return row.has("display_text") ? row.optString("display_text") : Protocol.text(row);
  }

  static JSONArray forRow(JSONObject row) {
    JSONArray local = row.optJSONArray("attachments");
    if (local != null) return local;
    JSONArray parts = row.optJSONArray("content"), images = new JSONArray();
    if (parts != null)
      for (int i = 0; i < parts.length(); i++) {
        JSONObject part = parts.optJSONObject(i);
        if (part == null || !part.optString("type").equals("image_url")) continue;
        JSONObject image = part.optJSONObject("image_url");
        String url = image == null ? part.optString("image_url") : image.optString("url");
        if (!url.isEmpty()) images.put(Protocol.obj("name","image","mime","image/*"));
      }
    return images;
  }

  private Attachments() {}
}
