package com.pba77.j6510benchmark;

import android.app.Activity;
import android.graphics.Color;
import android.graphics.Typeface;
import android.os.Bundle;
import android.text.method.ScrollingMovementMethod;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.ScrollView;
import android.widget.TextView;

public final class MainActivity extends Activity {
    static {
        System.loadLibrary("j6510_benchmark");
    }

    private static native String runBenchmark(long iterations);

    private TextView output;
    private TextView status;
    private ProgressBar progress;
    private Button runButton;
    private long selectedIterations = 1_000_000L;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(createContent());
    }

    private View createContent() {
        final int padding = dp(20);

        LinearLayout page = new LinearLayout(this);
        page.setOrientation(LinearLayout.VERTICAL);
        page.setPadding(padding, padding, padding, padding);
        page.setBackgroundColor(Color.rgb(245, 247, 250));

        TextView title = text("j6510 Benchmark", 28, Color.rgb(20, 27, 39), Typeface.BOLD);
        page.addView(title);

        TextView subtitle = text(
                "Natywny benchmark rdzenia MOS 6510 / NMOS 6502",
                14,
                Color.rgb(80, 91, 109),
                Typeface.NORMAL);
        LinearLayout.LayoutParams subtitleParams = wrap();
        subtitleParams.setMargins(0, dp(4), 0, dp(20));
        page.addView(subtitle, subtitleParams);

        TextView presetLabel = text("LICZBA ITERACJI", 12, Color.rgb(80, 91, 109), Typeface.BOLD);
        page.addView(presetLabel);

        LinearLayout presets = new LinearLayout(this);
        presets.setOrientation(LinearLayout.HORIZONTAL);
        presets.setGravity(Gravity.CENTER_VERTICAL);
        LinearLayout.LayoutParams presetsParams = matchWrap();
        presetsParams.setMargins(0, dp(8), 0, dp(12));
        page.addView(presets, presetsParams);

        Button quick = preset("100 tys.", 100_000L);
        Button standard = preset("1 mln", 1_000_000L);
        Button longRun = preset("5 mln", 5_000_000L);
        presets.addView(quick, weighted());
        presets.addView(standard, weighted());
        presets.addView(longRun, weighted());

        runButton = new Button(this);
        runButton.setText("URUCHOM BENCHMARK");
        runButton.setTextSize(15);
        runButton.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        runButton.setTextColor(Color.WHITE);
        runButton.setBackgroundColor(Color.rgb(28, 89, 214));
        runButton.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {
                startBenchmark();
            }
        });
        page.addView(runButton, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, dp(54)));

        LinearLayout statusRow = new LinearLayout(this);
        statusRow.setOrientation(LinearLayout.HORIZONTAL);
        statusRow.setGravity(Gravity.CENTER_VERTICAL);
        LinearLayout.LayoutParams rowParams = matchWrap();
        rowParams.setMargins(0, dp(14), 0, dp(10));
        page.addView(statusRow, rowParams);

        progress = new ProgressBar(this, null, android.R.attr.progressBarStyleSmall);
        progress.setVisibility(View.GONE);
        statusRow.addView(progress, new LinearLayout.LayoutParams(dp(24), dp(24)));

        status = text("Gotowy", 13, Color.rgb(80, 91, 109), Typeface.NORMAL);
        LinearLayout.LayoutParams statusParams = wrap();
        statusParams.setMargins(dp(8), 0, 0, 0);
        statusRow.addView(status, statusParams);

        ScrollView scroll = new ScrollView(this);
        scroll.setFillViewport(true);
        scroll.setBackgroundColor(Color.rgb(15, 20, 30));

        output = text(
                "Wybierz długość testu i naciśnij URUCHOM.\n\n" +
                "Test porównuje:\n" +
                "  • step()\n" +
                "  • run()\n" +
                "  • run_block()\n" +
                "  • run_cached()",
                13,
                Color.rgb(220, 228, 240),
                Typeface.NORMAL);
        output.setTypeface(Typeface.MONOSPACE);
        output.setPadding(dp(16), dp(16), dp(16), dp(16));
        output.setTextIsSelectable(true);
        output.setMovementMethod(new ScrollingMovementMethod());
        scroll.addView(output, new ScrollView.LayoutParams(
                ScrollView.LayoutParams.MATCH_PARENT,
                ScrollView.LayoutParams.WRAP_CONTENT));

        LinearLayout.LayoutParams scrollParams = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f);
        page.addView(scroll, scrollParams);
        return page;
    }

    private Button preset(String label, long iterations) {
        Button button = new Button(this);
        button.setText(label);
        button.setTextSize(12);
        button.setAllCaps(false);
        button.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {
                selectedIterations = iterations;
                status.setText("Wybrano: " + label + " iteracji");
            }
        });
        return button;
    }

    private void startBenchmark() {
        runButton.setEnabled(false);
        progress.setVisibility(View.VISIBLE);
        status.setText("Benchmark działa natywnie na CPU…");
        output.setText("Trwa pomiar. Nie wygaszaj ekranu.\n");

        final long iterations = selectedIterations;
        new Thread(new Runnable() {
            @Override
            public void run() {
                try {
                    final String result = runBenchmark(iterations);
                    runOnUiThread(new Runnable() {
                        @Override
                        public void run() {
                            output.setText(result);
                            status.setText("Zakończono");
                            progress.setVisibility(View.GONE);
                            runButton.setEnabled(true);
                        }
                    });
                } catch (final Throwable error) {
                    runOnUiThread(new Runnable() {
                        @Override
                        public void run() {
                            output.setText("Błąd benchmarku:\n" + error);
                            status.setText("Błąd");
                            progress.setVisibility(View.GONE);
                            runButton.setEnabled(true);
                        }
                    });
                }
            }
        }, "j6510-benchmark").start();
    }

    private TextView text(String value, int sizeSp, int color, int style) {
        TextView view = new TextView(this);
        view.setText(value);
        view.setTextSize(sizeSp);
        view.setTextColor(color);
        view.setTypeface(Typeface.DEFAULT, style);
        return view;
    }

    private LinearLayout.LayoutParams wrap() {
        return new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT,
                LinearLayout.LayoutParams.WRAP_CONTENT);
    }

    private LinearLayout.LayoutParams matchWrap() {
        return new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT);
    }

    private LinearLayout.LayoutParams weighted() {
        LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(0, dp(48), 1f);
        params.setMargins(dp(2), 0, dp(2), 0);
        return params;
    }

    private int dp(int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }
}
