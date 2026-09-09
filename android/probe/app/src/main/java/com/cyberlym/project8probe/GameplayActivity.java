package com.cyberlym.project8probe;

import android.os.Bundle;
import android.util.Log;
import android.widget.Toast;

import org.libsdl.app.SDLActivity;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;

/** Owns the SDL gameplay session; diagnostics keeps its independent Activity. */
public final class GameplayActivity extends SDLActivity {
    public static final String EXTRA_GAME_ROOT = "com.cyberlym.project8probe.GAME_ROOT";
    private static final String LOG_TAG = "Project8Game";
    private static final String LOG_PREFS = "gameplay-log";
    private static final String LOG_PATH_KEY = "path";
    private static final Object LOG_LOCK = new Object();
    private static final long MAX_LOG_BYTES = 1024L * 1024L;
    // Current file plus two previous files keeps private gameplay diagnostics
    // within the same approximately 1 MiB x 3 retention policy as ReXGlue.
    private static final int LOG_ROTATIONS = 2;
    private static OutputStream persistentLog;
    private static File persistentLogFile;
    private static String persistentLogPath = "";

    private static native boolean nativePrepareGameplay(String gameRoot);
    private static native void nativeInitializeGameplayLog();

    @Override protected void onCreate(Bundle state) {
        System.loadLibrary("main");
        startPersistentLog();
        nativeInitializeGameplayLog();
        writeSessionLog("P8_PLAY_REQUEST");
        String gameRoot = getIntent() != null ? getIntent().getStringExtra(EXTRA_GAME_ROOT) : null;
        writeSessionLog("P8_GAMEPLAY_INSTALL_ROOT value=" + gameRoot);
        if (gameRoot == null) {
            writeSessionLog("P8_GAMEPLAY_FAIL reason=missing_installed_game");
        }
        if (gameRoot == null || !prepareGame(gameRoot)) {
            Toast.makeText(this, "The installed game could not be opened.", Toast.LENGTH_LONG).show();
            finish();
            return;
        }
        super.onCreate(state);
    }

    private boolean prepareGame(String gameRoot) {
        try {
            writeSessionLog("P8_GAMEPLAY_INSTALLED stage=prepare-begin");
            boolean ready = nativePrepareGameplay(gameRoot);
            if (!ready) writeSessionLog("P8_GAMEPLAY_FAIL reason=native_prepare_failed");
            return ready;
        } catch (Exception exception) {
            writeSessionLog("P8_GAMEPLAY_FAIL reason=open_file_descriptor_exception type=" +
                    exception.getClass().getName() + " message=" + exception.getMessage());
            return false;
        }
    }

    /** Called synchronously by native code after the Java bridge is initialized. */
    public static void appendNativeGameplayLog(String message) {
        writeSessionLog(message);
    }

    private void startPersistentLog() {
        synchronized (LOG_LOCK) {
            closePersistentLogLocked();
            try {
                File directory = new File(getFilesDir(), "logs");
                if (!directory.exists() && !directory.mkdirs()) {
                    throw new IOException("private log directory unavailable");
                }
                persistentLogFile = new File(directory, "gameplay.log");
                rotatePersistentLogLocked();
                openPersistentLogLocked();
                persistentLogPath = "private:" + persistentLogFile.getAbsolutePath();
            } catch (Exception exception) {
                persistentLog = null;
                persistentLogFile = null;
                persistentLogPath = "unavailable private_error=" + exception;
            }
            getSharedPreferences(LOG_PREFS, MODE_PRIVATE).edit()
                    .putString(LOG_PATH_KEY, persistentLogPath).apply();
        }
        writeSessionLog("P8_GAMEPLAY_LOG_PATH value=" + persistentLogPath);
    }

    private static void writeSessionLog(String message) {
        String line = new SimpleDateFormat("yyyy-MM-dd HH:mm:ss.SSS", Locale.US).format(new Date()) +
                " pid=" + android.os.Process.myPid() + " tid=" + Thread.currentThread().getId() +
                " " + LOG_TAG + " " + message + "\n";
        Log.i(LOG_TAG, message);
        synchronized (LOG_LOCK) {
            if (persistentLog == null) return;
            try {
                byte[] bytes = line.getBytes(StandardCharsets.UTF_8);
                if (persistentLogFile != null && persistentLogFile.length() + bytes.length > MAX_LOG_BYTES) {
                    rotatePersistentLogLocked();
                    openPersistentLogLocked();
                }
                if (persistentLog == null) return;
                persistentLog.write(bytes);
                persistentLog.flush();
            } catch (IOException exception) {
                Log.e(LOG_TAG, "P8_GAMEPLAY_LOG_WRITE_FAIL " + exception, exception);
            }
        }
    }

    private static void closePersistentLogLocked() {
        if (persistentLog == null) return;
        try { persistentLog.flush(); persistentLog.close(); } catch (IOException ignored) { }
        persistentLog = null;
    }

    private static void rotatePersistentLogLocked() throws IOException {
        if (persistentLogFile == null) return;
        closePersistentLogLocked();
        File directory = persistentLogFile.getParentFile();
        for (int index = LOG_ROTATIONS - 1; index >= 1; --index) {
            File source = new File(directory, "gameplay.log." + index);
            File destination = new File(directory, "gameplay.log." + (index + 1));
            if (destination.exists() && !destination.delete()) throw new IOException("cannot rotate log");
            if (source.exists() && !source.renameTo(destination)) throw new IOException("cannot rotate log");
        }
        File first = new File(directory, "gameplay.log.1");
        if (first.exists() && !first.delete()) throw new IOException("cannot rotate log");
        if (persistentLogFile.exists() && !persistentLogFile.renameTo(first)) {
            throw new IOException("cannot rotate log");
        }
    }

    private static void openPersistentLogLocked() throws IOException {
        if (persistentLogFile == null) throw new IOException("private log unavailable");
        persistentLog = new FileOutputStream(persistentLogFile, false);
    }
}
