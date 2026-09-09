package com.cyberlym.project8probe;

import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.graphics.Color;
import android.view.Gravity;
import android.view.ViewGroup;
import android.widget.FrameLayout;
import android.widget.TextView;
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
    private static final long PERF_OVERLAY_INTERVAL_MS = 250;
    private static final long PERF_LOG_INTERVAL_NS = 5_000_000_000L;
    private static final int PERF_SNAPSHOT_VALUES = 21;

    private final Handler performanceHandler = new Handler(Looper.getMainLooper());
    private TextView performanceOverlay;
    private long lastPerformanceLogNs;

    private static native boolean nativePrepareGameplay(String gameRoot);
    private static native void nativeInitializeGameplayLog();
    private static native long[] nativeGetPresentationPerformance();

    private final Runnable performanceOverlayUpdate = new Runnable() {
        @Override public void run() {
            updatePerformanceOverlay();
            performanceHandler.postDelayed(this, PERF_OVERLAY_INTERVAL_MS);
        }
    };

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
        installPerformanceOverlay();
    }

    @Override protected void onDestroy() {
        performanceHandler.removeCallbacks(performanceOverlayUpdate);
        super.onDestroy();
    }

    private void installPerformanceOverlay() {
        if (!BuildConfig.DEBUG) return;
        performanceOverlay = new TextView(this);
        performanceOverlay.setTextColor(Color.WHITE);
        performanceOverlay.setTextSize(12);
        performanceOverlay.setShadowLayer(2.0f, 1.0f, 1.0f, Color.BLACK);
        performanceOverlay.setBackgroundColor(0x33000000);
        performanceOverlay.setPadding(8, 5, 8, 5);
        performanceOverlay.setText("FPS -- | -- ms");
        performanceOverlay.setClickable(false);
        performanceOverlay.setFocusable(false);
        performanceOverlay.setEnabled(false);
        FrameLayout.LayoutParams layout = new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT,
                Gravity.TOP | Gravity.START);
        layout.setMargins(12, 12, 0, 0);
        addContentView(performanceOverlay, layout);
        performanceHandler.post(performanceOverlayUpdate);
    }

    private void updatePerformanceOverlay() {
        if (performanceOverlay == null) return;
        long[] snapshot = nativeGetPresentationPerformance();
        if (snapshot == null || snapshot.length != PERF_SNAPSHOT_VALUES) return;

        final long nowNs = snapshot[3];
        final long gameFrameCount = snapshot[10];
        final long windowFrameCount = snapshot[13];
        final long windowElapsedNs = snapshot[14];
        double fps = 0.0;
        double frametimeMs = 0.0;
        if (windowFrameCount >= 2 && windowElapsedNs > 0) {
            fps = (windowFrameCount - 1) * 1_000_000_000.0 / windowElapsedNs;
            frametimeMs = windowElapsedNs / (windowFrameCount - 1) / 1_000_000.0;
        }
        performanceOverlay.setText(String.format(Locale.US, "FPS %.1f | %.1f ms", fps, frametimeMs));

        if (nowNs - lastPerformanceLogNs >= PERF_LOG_INTERVAL_NS) {
            lastPerformanceLogNs = nowNs;
            writeSessionLog(String.format(Locale.US,
                    "P8_PERF2 game_fps=%.1f game_frametime_ms=%.1f game_frames=%d " +
                            "presents=%d skipped=%d xenos_draws=%d queue_submits=%d " +
                            "occlusion_queries=%d occlusion_waits=%d " +
                            "native_cpu_preps=%d native_cpu_ns=%d pipelines=%d " +
                            "shader_translates=%d pipeline_desc_hit=%d pipeline_desc_miss=%d " +
                            "swapchain_recreations=%d",
                    fps, frametimeMs, gameFrameCount, snapshot[0], snapshot[4], snapshot[15],
                    snapshot[16], snapshot[17], snapshot[18], snapshot[19], snapshot[20],
                    snapshot[5], snapshot[6], snapshot[7], snapshot[8], snapshot[9]));
        }
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
