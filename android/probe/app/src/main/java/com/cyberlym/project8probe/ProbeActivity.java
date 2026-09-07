package com.cyberlym.project8probe;

import android.app.Activity;
import android.app.ActivityManager;
import android.app.ApplicationExitInfo;
import android.content.ClipData;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.res.Configuration;
import android.graphics.Color;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Process;
import android.os.SystemClock;
import android.view.Gravity;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;
import org.libsdl.app.SDLActivity;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.util.List;

/** SDLActivity plus a small, phone-only probe report overlay. */
public final class ProbeActivity extends SDLActivity {
    private static final int CREATE_LOG = 41;
    private static final String DIAGNOSTIC_PREFS = "stage5_lifecycle_diagnostics";
    private static final String TRACE_KEY = "trace";
    private static final String SEQUENCE_KEY = "sequence";
    private static final String BOOT_KEY = "boot_generation";
    private static final int MAX_TRACE_CHARS = 65536;
    private static android.content.Context diagnosticContext;
    private static String sessionId = "";
    private static int bootGeneration;
    private static int activityGeneration;
    private static String previousProcessExitReason = "NOT QUERIED";
    private final Handler handler = new Handler();
    private int recordedSurfaceIdentity;
    private TextView report;
    private final Runnable refresh = new Runnable() {
        @Override public void run() { if (report != null) report.setText(getCombinedReport()); handler.postDelayed(this, 500); }
    };
    private static native String nativeGetReport();
    private static native String nativeGetLog();
    private static native void nativeActivityCreated();
    private static native void nativeActivityDestroyed();
    private static native void nativeAndroidSurfaceCreated(int identity);
    private static native void nativeAndroidSurfaceDestroyed(int identity);
    private static native boolean nativePrepareActivityRecreate();

    @Override protected void onCreate(Bundle state) {
        initializePersistentDiagnostics(getApplicationContext());
        Intent launchIntent = getIntent();
        appendBreadcrumb("ACTIVITY_ON_CREATE_ENTER",
                "saved_state=" + (state != null) + " intent_flags=0x" +
                        Integer.toHexString(launchIntent != null ? launchIntent.getFlags() : 0));
        super.onCreate(state);
        nativeActivityCreated();
        SurfaceView sdlSurface = (SurfaceView)mSurface;
        SurfaceHolder holder = sdlSurface.getHolder();
        holder.addCallback(new SurfaceHolder.Callback() {
            @Override public void surfaceCreated(SurfaceHolder callbackHolder) {
                recordSurface(callbackHolder);
            }
            @Override public void surfaceChanged(SurfaceHolder callbackHolder, int format, int width, int height) {
                recordSurface(callbackHolder);
            }
            @Override public void surfaceDestroyed(SurfaceHolder callbackHolder) {
                Surface surface = callbackHolder.getSurface();
                int identity = surface != null ? System.identityHashCode(surface) : recordedSurfaceIdentity;
                appendBreadcrumb("ANDROID_SURFACE_DESTROYED", "identity=" + identity);
                nativeAndroidSurfaceDestroyed(identity);
                recordedSurfaceIdentity = 0;
            }
        });
        recordSurface(holder);
        LinearLayout panel = new LinearLayout(this);
        panel.setOrientation(LinearLayout.VERTICAL); panel.setPadding(20, 16, 20, 16);
        panel.setBackgroundColor(0xcc101010);
        report = new TextView(this); report.setTextColor(Color.WHITE); report.setTextSize(14); report.setGravity(Gravity.START);
        panel.addView(report, new LinearLayout.LayoutParams(-1, 0, 1));
        LinearLayout buttons = new LinearLayout(this);
        Button export = new Button(this); export.setText("EXPORT LOG"); export.setOnClickListener(v -> exportLog());
        Button copy = new Button(this); copy.setText("COPY REPORT"); copy.setOnClickListener(v -> copyReport());
        buttons.addView(export, new LinearLayout.LayoutParams(0, -2, 1)); buttons.addView(copy, new LinearLayout.LayoutParams(0, -2, 1));
        panel.addView(buttons);
        Button recreateSurface = new Button(this); recreateSurface.setText("RECREATE ACTIVITY / SURFACE");
        recreateSurface.setOnClickListener(v -> recreateActivityForSurfaceTest());
        panel.addView(recreateSurface, new LinearLayout.LayoutParams(-1, -2));
        addContentView(panel, new ViewGroup.LayoutParams(-1, -1)); handler.post(refresh);
        appendBreadcrumb("ACTIVITY_ON_CREATE_EXIT", staticSDLState());
    }
    private void recordSurface(SurfaceHolder holder) {
        Surface surface = holder.getSurface();
        if (surface != null && surface.isValid()) {
            int identity = System.identityHashCode(surface);
            if (identity != recordedSurfaceIdentity) {
                recordedSurfaceIdentity = identity;
                appendBreadcrumb("ANDROID_SURFACE_CREATED", "identity=" + identity);
                nativeAndroidSurfaceCreated(identity);
            }
        }
    }
    private void recreateActivityForSurfaceTest() {
        appendBreadcrumb("RECREATE_REQUESTED", staticSDLState());
        if (nativePrepareActivityRecreate()) {
            try {
                recreate();
            } catch (RuntimeException exception) {
                appendBreadcrumb("RECREATE_CALL_EXCEPTION",
                        exception.getClass().getName() + ": " + exception.getMessage());
                throw exception;
            }
        } else {
            appendBreadcrumb("RECREATE_REQUEST_REJECTED", staticSDLState());
        }
    }
    private String getCombinedReport() {
        return nativeGetReport() + "\nPREVIOUS_PROCESS_EXIT_REASON: " + previousProcessExitReason
                + "\n\nPREVIOUS SESSION LIFECYCLE TRACE\n" + getPersistentTrace();
    }
    private void copyReport() {
        android.content.ClipboardManager clipboard = (android.content.ClipboardManager) getSystemService(CLIPBOARD_SERVICE);
        clipboard.setPrimaryClip(ClipData.newPlainText("Project 8 probe report", getCombinedReport()));
    }
    private void exportLog() {
        Intent intent = new Intent(Intent.ACTION_CREATE_DOCUMENT); intent.setType("text/plain");
        intent.putExtra(Intent.EXTRA_TITLE, "probe.log"); startActivityForResult(intent, CREATE_LOG);
    }
    @Override protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (request == CREATE_LOG && result == Activity.RESULT_OK && data != null) {
            try (OutputStream out = getContentResolver().openOutputStream(data.getData())) {
                out.write((nativeGetLog() + "\n\n" + getCombinedReport()).getBytes(StandardCharsets.UTF_8));
            } catch (Exception ignored) { }
        }
    }
    @Override protected void onDestroy() {
        appendBreadcrumb("ACTIVITY_ON_DESTROY_ENTER", staticSDLState());
        appendBreadcrumb("SDL_NATIVE_THREAD_STOP_REQUEST", staticSDLState());
        handler.removeCallbacks(refresh);
        nativeActivityDestroyed();
        super.onDestroy();
        appendBreadcrumb("ACTIVITY_ON_DESTROY_EXIT", staticSDLState());
    }

    @Override protected void onPause() {
        appendBreadcrumb("ACTIVITY_ON_PAUSE_ENTER", staticSDLState());
        super.onPause();
        appendBreadcrumb("ACTIVITY_ON_PAUSE_EXIT", staticSDLState());
    }

    @Override protected void onResume() {
        appendBreadcrumb("ACTIVITY_ON_RESUME_ENTER", staticSDLState());
        super.onResume();
        appendBreadcrumb("ACTIVITY_ON_RESUME_EXIT", staticSDLState());
    }

    @Override public void onConfigurationChanged(Configuration configuration) {
        appendBreadcrumb("ACTIVITY_ON_CONFIGURATION_CHANGED_ENTER",
                "orientation=" + configuration.orientation + " " + staticSDLState());
        super.onConfigurationChanged(configuration);
        appendBreadcrumb("ACTIVITY_ON_CONFIGURATION_CHANGED_EXIT",
                "orientation=" + configuration.orientation + " " + staticSDLState());
    }

    public static void appendNativeLifecycleBreadcrumb(String event, long handle) {
        String detail = handle == 0 ? "" : "handle=0x" + Long.toHexString(handle);
        appendBreadcrumb(event, detail);
    }

    private static synchronized void initializePersistentDiagnostics(android.content.Context context) {
        if (diagnosticContext == null) {
            diagnosticContext = context;
        }
        if (sessionId.isEmpty()) {
            SharedPreferences preferences = diagnosticContext.getSharedPreferences(DIAGNOSTIC_PREFS, MODE_PRIVATE);
            bootGeneration = preferences.getInt(BOOT_KEY, 0) + 1;
            sessionId = Long.toHexString(System.currentTimeMillis()) + "-" +
                    Integer.toHexString(Process.myPid()) + "-" + bootGeneration;
            preferences.edit().putInt(BOOT_KEY, bootGeneration).commit();
            previousProcessExitReason = queryPreviousProcessExitReason();
        }
        ++activityGeneration;
    }

    private static synchronized void appendBreadcrumb(String event, String detail) {
        if (diagnosticContext == null) {
            return;
        }
        SharedPreferences preferences = diagnosticContext.getSharedPreferences(DIAGNOSTIC_PREFS, MODE_PRIVATE);
        long sequence = preferences.getLong(SEQUENCE_KEY, 0) + 1;
        String line = String.format(
                "%06d t=%d session=%s boot=%d activity=%d pid=%d %s%s\n",
                sequence, SystemClock.elapsedRealtimeNanos(), sessionId, bootGeneration,
                activityGeneration, Process.myPid(), event,
                detail.isEmpty() ? "" : " " + detail);
        String trace = preferences.getString(TRACE_KEY, "") + line;
        if (trace.length() > MAX_TRACE_CHARS) {
            int firstNewline = trace.indexOf('\n', trace.length() - MAX_TRACE_CHARS);
            trace = trace.substring(firstNewline >= 0 ? firstNewline + 1 : trace.length() - MAX_TRACE_CHARS);
        }
        preferences.edit().putLong(SEQUENCE_KEY, sequence).putString(TRACE_KEY, trace).commit();
    }

    private static String getPersistentTrace() {
        if (diagnosticContext == null) {
            return "NOT AVAILABLE\n";
        }
        String trace = diagnosticContext.getSharedPreferences(DIAGNOSTIC_PREFS, MODE_PRIVATE)
                .getString(TRACE_KEY, "");
        return trace.isEmpty() ? "NO BREADCRUMBS\n" : trace;
    }

    private static String staticSDLState() {
        Thread thread = mSDLThread;
        return "thread=" + (thread == null ? "null" : thread.getState().name()) +
                " alive=" + (thread != null && thread.isAlive()) +
                " activity_created=" + mActivityCreated +
                " main_finished=" + mSDLMainFinished;
    }

    private static String queryPreviousProcessExitReason() {
        if (Build.VERSION.SDK_INT < 30) {
            return "UNAVAILABLE_API_LT_30";
        }
        try {
            ActivityManager manager = (ActivityManager)diagnosticContext.getSystemService(ACTIVITY_SERVICE);
            List<ApplicationExitInfo> exits = manager.getHistoricalProcessExitReasons(
                    diagnosticContext.getPackageName(), 0, 1);
            if (exits == null || exits.isEmpty()) {
                return "NONE";
            }
            ApplicationExitInfo exit = exits.get(0);
            return "reason=" + exitReasonName(exit.getReason()) + "(" + exit.getReason() + ")" +
                    " status=" + exit.getStatus() + " timestamp=" + exit.getTimestamp() +
                    " description=" + String.valueOf(exit.getDescription());
        } catch (RuntimeException exception) {
            return "QUERY_FAILED " + exception.getClass().getName() + ": " + exception.getMessage();
        }
    }

    private static String exitReasonName(int reason) {
        switch (reason) {
            case ApplicationExitInfo.REASON_ANR: return "ANR";
            case ApplicationExitInfo.REASON_CRASH: return "CRASH";
            case ApplicationExitInfo.REASON_CRASH_NATIVE: return "CRASH_NATIVE";
            case ApplicationExitInfo.REASON_DEPENDENCY_DIED: return "DEPENDENCY_DIED";
            case ApplicationExitInfo.REASON_EXCESSIVE_RESOURCE_USAGE: return "EXCESSIVE_RESOURCE_USAGE";
            case ApplicationExitInfo.REASON_EXIT_SELF: return "EXIT_SELF";
            case ApplicationExitInfo.REASON_FREEZER: return "FREEZER";
            case ApplicationExitInfo.REASON_INITIALIZATION_FAILURE: return "INITIALIZATION_FAILURE";
            case ApplicationExitInfo.REASON_LOW_MEMORY: return "LOW_MEMORY";
            case ApplicationExitInfo.REASON_PACKAGE_STATE_CHANGE: return "PACKAGE_STATE_CHANGE";
            case ApplicationExitInfo.REASON_PACKAGE_UPDATED: return "PACKAGE_UPDATED";
            case ApplicationExitInfo.REASON_PERMISSION_CHANGE: return "PERMISSION_CHANGE";
            case ApplicationExitInfo.REASON_SIGNALED: return "SIGNALED";
            case ApplicationExitInfo.REASON_USER_REQUESTED: return "USER_REQUESTED";
            case ApplicationExitInfo.REASON_USER_STOPPED: return "USER_STOPPED";
            case ApplicationExitInfo.REASON_UNKNOWN: return "UNKNOWN";
            default: return "OTHER";
        }
    }
}
