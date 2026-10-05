package com.larkingroup.lcbhermes;

import android.content.Context;
import android.security.keystore.KeyGenParameterSpec;
import android.security.keystore.KeyProperties;
import android.util.AtomicFile;
import java.io.File;
import java.io.FileOutputStream;
import java.security.KeyStore;
import javax.crypto.Cipher;
import javax.crypto.KeyGenerator;
import javax.crypto.SecretKey;
import javax.crypto.spec.GCMParameterSpec;
import org.json.JSONObject;

final class SecureStore {
  private final AtomicFile file;
  private static final String ALIAS = "lcb-hermes.local.v1";

  SecureStore(Context c) {
    file = new AtomicFile(new File(c.getFilesDir(), "private.bin"));
  }

  private SecretKey key() throws Exception {
    KeyStore ks = KeyStore.getInstance("AndroidKeyStore");
    ks.load(null);
    if (ks.containsAlias(ALIAS)) return (SecretKey) ks.getKey(ALIAS, null);
    KeyGenerator gen = KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES, "AndroidKeyStore");
    gen.init(
        new KeyGenParameterSpec.Builder(
                ALIAS, KeyProperties.PURPOSE_ENCRYPT | KeyProperties.PURPOSE_DECRYPT)
            .setBlockModes(KeyProperties.BLOCK_MODE_GCM)
            .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
            .build());
    return gen.generateKey();
  }

  synchronized JSONObject read() throws Exception {
    if (!file.getBaseFile().exists()) return new JSONObject();
    byte[] bytes = file.readFully();
    if (bytes.length < 29 || bytes[0] != 1)
      throw new IllegalStateException("Local data format is invalid");
    Cipher cipher = Cipher.getInstance("AES/GCM/NoPadding");
    cipher.init(Cipher.DECRYPT_MODE, key(), new GCMParameterSpec(128, bytes, 1, 12));
    return new JSONObject(
        new String(
            cipher.doFinal(bytes, 13, bytes.length - 13), java.nio.charset.StandardCharsets.UTF_8));
  }

  synchronized void write(JSONObject value) throws Exception {
    Cipher cipher = Cipher.getInstance("AES/GCM/NoPadding");
    cipher.init(Cipher.ENCRYPT_MODE, key());
    FileOutputStream out = null;
    try {
      out = file.startWrite();
      out.write(1);
      out.write(cipher.getIV());
      out.write(cipher.doFinal(value.toString().getBytes(java.nio.charset.StandardCharsets.UTF_8)));
      file.finishWrite(out);
    } catch (Exception e) {
      if (out != null) file.failWrite(out);
      throw e;
    }
  }
}
