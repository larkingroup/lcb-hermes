package com.larkingroup.lcbhermes;

import android.app.*;
import android.content.*;
import android.os.IBinder;

public final class TurnService extends Service {
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
