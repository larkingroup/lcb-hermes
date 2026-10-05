package com.larkingroup.lcbhermes;

import android.Manifest;
import android.app.*;
import android.content.*;
import android.content.pm.PackageManager;
import android.os.Build;
import android.os.IBinder;

public final class TurnService extends Service {
  static void reply(Context context, String text, String error) {
    NotificationManager manager = context.getSystemService(NotificationManager.class);
    NotificationChannel channel =
        new NotificationChannel(
            "replies", "Hermes replies", NotificationManager.IMPORTANCE_DEFAULT);
    channel.setLockscreenVisibility(Notification.VISIBILITY_PRIVATE);
    manager.createNotificationChannel(channel);
    if (Build.VERSION.SDK_INT >= 33
        && context.checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS)
            != PackageManager.PERMISSION_GRANTED) return;
    String preview = text.trim().replaceAll("\\s+", " ");
    if (preview.isEmpty()) preview = error.isEmpty() ? "Your reply is ready." : error;
    if (preview.length() > 240) preview = preview.substring(0, 240) + "…";
    PendingIntent open =
        PendingIntent.getActivity(
            context,
            0,
            new Intent(context, MainActivity.class)
                .putExtra("reply", true)
                .addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP | Intent.FLAG_ACTIVITY_CLEAR_TOP),
            PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT);
    Notification redacted =
        new Notification.Builder(context, "replies")
            .setSmallIcon(android.R.drawable.stat_notify_chat)
            .setContentTitle("Hermes replied")
            .setContentText("Open your chat to read the reply.")
            .build();
    manager.notify(
        2,
        new Notification.Builder(context, "replies")
            .setSmallIcon(android.R.drawable.stat_notify_chat)
            .setContentTitle(error.isEmpty() ? "Hermes replied" : "Hermes task finished")
            .setContentText(preview)
            .setStyle(new Notification.BigTextStyle().bigText(preview))
            .setContentIntent(open)
            .setAutoCancel(true)
            .setCategory(Notification.CATEGORY_MESSAGE)
            .setVisibility(Notification.VISIBILITY_PRIVATE)
            .setPublicVersion(redacted)
            .build());
  }

  @Override
  public void onCreate() {
    super.onCreate();
    NotificationChannel ch =
        new NotificationChannel("tasks", "Hermes tasks", NotificationManager.IMPORTANCE_LOW);
    ch.setLockscreenVisibility(Notification.VISIBILITY_PRIVATE);
    getSystemService(NotificationManager.class).createNotificationChannel(ch);
  }

  @Override
  public int onStartCommand(Intent intent, int flags, int startId) {
    Controller c = ((HermesApp) getApplication()).controller;
    if (intent != null && "stop".equals(intent.getAction())) {
      c.stop();
      return START_NOT_STICKY;
    }
    PendingIntent open =
        PendingIntent.getActivity(
            this,
            0,
            new Intent(this, MainActivity.class),
            PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT);
    PendingIntent stop =
        PendingIntent.getService(
            this,
            1,
            new Intent(this, TurnService.class).setAction("stop"),
            PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT);
    startForeground(
        1,
        new Notification.Builder(this, "tasks")
            .setSmallIcon(android.R.drawable.stat_notify_sync)
            .setContentTitle("Hermes is working")
            .setContentText("Tap to return to your chat.")
            .setContentIntent(open)
            .setOngoing(true)
            .setVisibility(Notification.VISIBILITY_PRIVATE)
            .addAction(new Notification.Action.Builder(null, "stop", stop).build())
            .build());
    if (!c.running) stopSelf();
    return START_NOT_STICKY;
  }

  @Override
  public void onTimeout(int startId, int fgsType) {
    stopSelf();
  }

  @Override
  public IBinder onBind(Intent intent) {
    return null;
  }
}
