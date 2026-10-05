package com.larkingroup.lcbhermes;

import android.app.Application;

public final class HermesApp extends Application {
  Controller controller;

  @Override
  public void onCreate() {
    super.onCreate();
    controller = new Controller(this);
  }
}
