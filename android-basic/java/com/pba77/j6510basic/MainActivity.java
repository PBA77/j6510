package com.pba77.j6510basic;

import android.app.Activity;
import android.content.Context;
import android.graphics.Color;
import android.graphics.Typeface;
import android.os.Bundle;
import android.text.Editable;
import android.text.InputType;
import android.text.TextWatcher;
import android.view.Gravity;
import android.view.View;
import android.view.inputmethod.InputMethodManager;
import android.widget.Button;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.HorizontalScrollView;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

public final class MainActivity extends Activity {
    static {
        System.loadLibrary("j6510_basic");
    }

    private static native void nativeReset();
    private static native void nativeSend(String text);
    private static native void nativeType(String text);
    private static native String nativePump(int instructionBudget);
    private static native void nativeBreak();

    private final StringBuilder transcript = new StringBuilder();
    private volatile boolean pumpRunning;
    private Thread pumpThread;
    private TextView terminal;
    private TextView state;
    private ScrollView terminalScroll;
    private EditText keyboardInput;
    private boolean resettingKeyboardInput;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(createContent());
        nativeReset();
    }

    @Override
    protected void onStart() {
        super.onStart();
        startPump();
    }

    @Override
    protected void onStop() {
        pumpRunning = false;
        super.onStop();
    }

    private View createContent() {
        LinearLayout page = new LinearLayout(this);
        page.setOrientation(LinearLayout.VERTICAL);
        page.setBackgroundColor(Color.rgb(8, 12, 18));

        LinearLayout toolbar = new LinearLayout(this);
        toolbar.setOrientation(LinearLayout.HORIZONTAL);
        toolbar.setGravity(Gravity.CENTER_VERTICAL);
        toolbar.setPadding(dp(16), dp(10), dp(10), dp(10));
        toolbar.setBackgroundColor(Color.rgb(18, 31, 52));

        LinearLayout titles = new LinearLayout(this);
        titles.setOrientation(LinearLayout.VERTICAL);
        TextView title = text("j6510 BASIC", 20, Color.WHITE, Typeface.BOLD);
        state = text("MICROSOFT 6502 BASIC • CPU ACTIVE", 11, Color.rgb(96, 220, 170), Typeface.BOLD);
        titles.addView(title);
        titles.addView(state);
        toolbar.addView(titles, new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f));

        Button reset = smallButton("RESET");
        reset.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {
                nativeReset();
                transcript.setLength(0);
                renderTerminal();
                state.setText("RESET • TAP SCREEN TO TYPE");
                showKeyboard();
            }
        });
        toolbar.addView(reset);
        page.addView(toolbar, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT));

        FrameLayout terminalLayer = new FrameLayout(this);

        terminalScroll = new ScrollView(this);
        terminalScroll.setFillViewport(true);
        terminalScroll.setBackgroundColor(Color.rgb(8, 12, 18));
        terminal = text("█", 15, Color.rgb(184, 255, 196), Typeface.NORMAL);
        terminal.setTypeface(Typeface.MONOSPACE);
        terminal.setTextIsSelectable(true);
        terminal.setPadding(dp(14), dp(14), dp(14), dp(14));
        terminal.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {
                showKeyboard();
            }
        });
        terminalScroll.addView(terminal, new ScrollView.LayoutParams(
                ScrollView.LayoutParams.MATCH_PARENT,
                ScrollView.LayoutParams.WRAP_CONTENT));
        terminalLayer.addView(terminalScroll, new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT,
                FrameLayout.LayoutParams.MATCH_PARENT));

        keyboardInput = new EditText(this);
        keyboardInput.setBackgroundColor(Color.TRANSPARENT);
        keyboardInput.setTextColor(Color.TRANSPARENT);
        keyboardInput.setCursorVisible(false);
        keyboardInput.setSingleLine(false);
        keyboardInput.setInputType(InputType.TYPE_CLASS_TEXT
                | InputType.TYPE_TEXT_FLAG_CAP_CHARACTERS
                | InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS
                | InputType.TYPE_TEXT_FLAG_MULTI_LINE);
        keyboardInput.setText(" ");
        keyboardInput.setSelection(1);
        keyboardInput.addTextChangedListener(new TextWatcher() {
            @Override
            public void beforeTextChanged(CharSequence text, int start, int count, int after) {
            }

            @Override
            public void onTextChanged(CharSequence text, int start, int before, int count) {
            }

            @Override
            public void afterTextChanged(Editable editable) {
                if (resettingKeyboardInput) {
                    return;
                }
                String value = editable.toString();
                if (value.isEmpty()) {
                    nativeType("\b");
                } else if (!" ".equals(value)) {
                    String typed = value.charAt(0) == ' ' ? value.substring(1) : value;
                    if (!typed.isEmpty()) {
                        nativeType(typed);
                        state.setText("CPU ACTIVE");
                    }
                }
                resettingKeyboardInput = true;
                keyboardInput.setText(" ");
                keyboardInput.setSelection(1);
                resettingKeyboardInput = false;
            }
        });
        FrameLayout.LayoutParams hiddenInputParams = new FrameLayout.LayoutParams(dp(2), dp(2));
        hiddenInputParams.gravity = Gravity.BOTTOM | Gravity.START;
        terminalLayer.addView(keyboardInput, hiddenInputParams);
        terminalLayer.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {
                showKeyboard();
            }
        });
        page.addView(terminalLayer, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f));

        HorizontalScrollView shortcutsScroll = new HorizontalScrollView(this);
        shortcutsScroll.setHorizontalScrollBarEnabled(false);
        shortcutsScroll.setBackgroundColor(Color.rgb(13, 20, 30));
        LinearLayout shortcuts = new LinearLayout(this);
        shortcuts.setOrientation(LinearLayout.HORIZONTAL);
        shortcuts.setPadding(dp(8), dp(6), dp(8), dp(6));
        shortcuts.addView(commandButton("RUN", "RUN"));
        shortcuts.addView(commandButton("LIST", "LIST"));
        shortcuts.addView(commandButton("NEW", "NEW"));

        Button clear = smallButton("CLEAR");
        clear.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {
                transcript.setLength(0);
                renderTerminal();
                showKeyboard();
            }
        });
        shortcuts.addView(clear);

        Button stop = smallButton("STOP");
        stop.setTextColor(Color.rgb(255, 150, 150));
        stop.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {
                nativeBreak();
                state.setText("BREAK REQUESTED");
            }
        });
        shortcuts.addView(stop);
        shortcutsScroll.addView(shortcuts);
        page.addView(shortcutsScroll, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT));

        return page;
    }

    private Button commandButton(String label, final String basicCommand) {
        Button button = smallButton(label);
        button.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {
                nativeSend(basicCommand);
                state.setText("CPU ACTIVE");
                showKeyboard();
            }
        });
        return button;
    }

    private void showKeyboard() {
        keyboardInput.requestFocus();
        keyboardInput.setSelection(keyboardInput.length());
        InputMethodManager keyboard = (InputMethodManager) getSystemService(Context.INPUT_METHOD_SERVICE);
        if (keyboard != null) {
            keyboard.showSoftInput(keyboardInput, InputMethodManager.SHOW_IMPLICIT);
        }
    }

    private void startPump() {
        if (pumpThread != null && pumpThread.isAlive()) {
            pumpRunning = true;
            return;
        }
        pumpRunning = true;
        pumpThread = new Thread(new Runnable() {
            @Override
            public void run() {
                while (pumpRunning) {
                    final String output = nativePump(150_000);
                    if (!output.isEmpty()) {
                        runOnUiThread(new Runnable() {
                            @Override
                            public void run() {
                                appendTerminal(output);
                                state.setText("READY");
                            }
                        });
                    }
                    try {
                        Thread.sleep(output.isEmpty() ? 25L : 2L);
                    } catch (InterruptedException ignored) {
                        Thread.currentThread().interrupt();
                        return;
                    }
                }
            }
        }, "j6510-basic-cpu");
        pumpThread.start();
    }

    private void appendTerminal(String value) {
        transcript.append(value);
        if (transcript.length() > 100_000) {
            transcript.delete(0, transcript.length() - 80_000);
        }
        renderTerminal();
        terminalScroll.post(new Runnable() {
            @Override
            public void run() {
                terminalScroll.fullScroll(View.FOCUS_DOWN);
            }
        });
    }

    private void renderTerminal() {
        terminal.setText(transcript.toString() + "█");
    }

    private Button smallButton(String label) {
        Button button = new Button(this);
        button.setText(label);
        button.setTextSize(12);
        button.setTextColor(Color.rgb(210, 222, 238));
        button.setBackgroundColor(Color.TRANSPARENT);
        button.setMinHeight(0);
        button.setMinWidth(0);
        button.setPadding(dp(12), dp(8), dp(12), dp(8));
        return button;
    }

    private TextView text(String value, int sizeSp, int color, int style) {
        TextView view = new TextView(this);
        view.setText(value);
        view.setTextSize(sizeSp);
        view.setTextColor(color);
        view.setTypeface(Typeface.DEFAULT, style);
        return view;
    }

    private int dp(int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }
}
