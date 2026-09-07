package com.cyberlym.project8probe;

import android.app.Activity;
import android.content.ClipData;
import android.content.Intent;
import android.graphics.Color;
import android.os.Bundle;
import android.os.Handler;
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

/** SDLActivity plus a small, phone-only probe report overlay. */
public final class ProbeActivity extends SDLActivity {
    private static final int CREATE_LOG = 41;
    private final Handler handler = new Handler();
    private TextView report;
    private final Runnable refresh = new Runnable() {
        @Override public void run() { if (report != null) report.setText(nativeGetReport()); handler.postDelayed(this, 500); }
    };
    private static native String nativeGetReport();
    private static native String nativeGetLog();
    private static native void nativeActivityCreated();
    private static native void nativeActivityDestroyed();
    private static native void nativeAndroidSurfaceCreated(int identity);
    private static native void nativeAndroidSurfaceDestroyed(int identity);
    private static native boolean nativePrepareActivityRecreate();

    @Override protected void onCreate(Bundle state) {
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
                nativeAndroidSurfaceDestroyed(surface != null ? System.identityHashCode(surface) : 0);
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
    }
    private void recordSurface(SurfaceHolder holder) {
        Surface surface = holder.getSurface();
        if (surface != null && surface.isValid()) {
            nativeAndroidSurfaceCreated(System.identityHashCode(surface));
        }
    }
    private void recreateActivityForSurfaceTest() {
        if (nativePrepareActivityRecreate()) {
            recreate();
        }
    }
    private void copyReport() {
        android.content.ClipboardManager clipboard = (android.content.ClipboardManager) getSystemService(CLIPBOARD_SERVICE);
        clipboard.setPrimaryClip(ClipData.newPlainText("Project 8 probe report", nativeGetReport()));
    }
    private void exportLog() {
        Intent intent = new Intent(Intent.ACTION_CREATE_DOCUMENT); intent.setType("text/plain");
        intent.putExtra(Intent.EXTRA_TITLE, "probe.log"); startActivityForResult(intent, CREATE_LOG);
    }
    @Override protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (request == CREATE_LOG && result == Activity.RESULT_OK && data != null) {
            try (OutputStream out = getContentResolver().openOutputStream(data.getData())) {
                out.write(nativeGetLog().getBytes(StandardCharsets.UTF_8));
            } catch (Exception ignored) { }
        }
    }
    @Override protected void onDestroy() {
        handler.removeCallbacks(refresh);
        nativeActivityDestroyed();
        super.onDestroy();
    }
}
