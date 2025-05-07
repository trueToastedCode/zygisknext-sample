package de.truetoastedcode.znmodsample;

import android.util.Log;
import android.view.View;
import android.view.ViewGroup;
import android.widget.TextView;
import android.os.Looper;
import android.os.Handler;
import android.graphics.Color;

import java.lang.reflect.Field;
import java.lang.reflect.Method;

public final class EntryPoint {
    private static final String TAG = "znmodsample";

    public static void init() {
        Log.d(TAG, "EntryPoint initialization started");
        
        try {
            // Warten Sie etwas, damit die UI vollständig geladen ist
            scheduleOneTimeUIModification(30000);
            Log.d(TAG, "Scheduled one-time UI modification");
        } catch (Exception e) {
            Log.e(TAG, "Error in initialization: " + e.getMessage());
            e.printStackTrace();
        }
    }
    
    private static void scheduleOneTimeUIModification(long delayMillis) {
        new Thread(() -> {
            try {
                Log.d(TAG, "Starting sleep for " + delayMillis + "ms");
                Thread.sleep(delayMillis);
                Log.d(TAG, "Woke up, starting UI modifications");
                
                // Post UI modifications to main thread
                new Handler(Looper.getMainLooper()).post(() -> {
                    try {
                        modifySystemUI();
                    } catch (Exception e) {
                        Log.e(TAG, "Error in UI modification: " + e.getMessage());
                    }
                });
                
            } catch (Exception e) {
                Log.e(TAG, "Error in delayed thread: " + e.getMessage());
                e.printStackTrace();
            }
        }).start();
    }

    private static void modifySystemUI() {
        Log.d(TAG, "Attempting to modify SystemUI");
        try {
            // Die Hauptmethode, die den permanenten Hack anwendet
            applyPermanentClockHack();
            
            Log.d(TAG, "SystemUI modification completed");
        } catch (Exception e) {
            Log.e(TAG, "Error modifying SystemUI: " + e.getMessage());
            e.printStackTrace();
        }
    }
    
    private static void applyPermanentClockHack() {
        try {
            Log.d(TAG, "Applying permanent clock hack");
            
            modifyTimeFormatForAllTextViews();
            
            Log.d(TAG, "Permanent clock hack applied");
        } catch (Exception e) {
            Log.e(TAG, "Error applying permanent clock hack: " + e.getMessage());
            e.printStackTrace();
        }
    }
    
    private static void modifyTimeFormatForAllTextViews() {
        try {
            View rootView = getRootView();
            if (rootView == null) {
                Log.e(TAG, "Could not get root view");
                return;
            }
            
            // Suchen Sie nach allen TextViews, die wie eine Uhr aussehen
            findAndModifyClockLikeTextViews(rootView);
        } catch (Exception e) {
            Log.e(TAG, "Error modifying time format: " + e.getMessage());
        }
    }
    
    private static void findAndModifyClockLikeTextViews(View view) {
        try {
            if (view instanceof TextView) {
                TextView textView = (TextView) view;
                String text = textView.getText().toString();
                
                // Prüfen Sie, ob der Text wie eine Uhrzeit aussieht
                if ((text.contains(":") && text.matches(".*\\d+.*")) || 
                    (text.length() <= 8 && text.matches("\\d{1,2}[:.]\\d{2}"))) {
                    
                    Log.d(TAG, "Found clock-like TextView with text: " + text);
                    
                    modifyTextView(textView);
                }
            }
            
            if (view instanceof ViewGroup) {
                ViewGroup viewGroup = (ViewGroup) view;
                for (int i = 0; i < viewGroup.getChildCount(); i++) {
                    findAndModifyClockLikeTextViews(viewGroup.getChildAt(i));
                }
            }
        } catch (Exception e) {
            Log.e(TAG, "Error finding clock-like TextViews: " + e.getMessage());
        }
    }
    
    private static void modifyTextView(TextView textView) {
        try {
            Log.d(TAG, "Modifying TextView with current text: " + textView.getText());
            textView.setTextColor(Color.RED);
        } catch (Exception e) {
            Log.e(TAG, "Error modifying TextView: " + e.getMessage());
        }
    }
    
    private static View getRootView() {
        try {
            // Die Root-View über WindowManagerGlobal bekommen
            Class<?> windowManagerGlobalClass = Class.forName("android.view.WindowManagerGlobal");
            Method getInstance = windowManagerGlobalClass.getDeclaredMethod("getInstance");
            Object windowManagerGlobal = getInstance.invoke(null);
            
            try {
                // Für neuere Android-Versionen
                Method getViewRootNames = windowManagerGlobalClass.getDeclaredMethod("getViewRootNames");
                String[] rootViewNames = (String[]) getViewRootNames.invoke(windowManagerGlobal);
                Method getRootView = windowManagerGlobalClass.getDeclaredMethod("getRootView", String.class);
                
                // Versuchen Sie, die StatusBar-Root-View zu finden
                for (String rootViewName : rootViewNames) {
                    if (rootViewName.contains("StatusBar")) {
                        View view = (View) getRootView.invoke(windowManagerGlobal, rootViewName);
                        return view;
                    }
                }
                
                // Falls keine spezifische StatusBar-View gefunden wurde, nehmen Sie die erste
                if (rootViewNames.length > 0) {
                    return (View) getRootView.invoke(windowManagerGlobal, rootViewNames[0]);
                }
            } catch (NoSuchMethodException e) {
                // Für ältere Android-Versionen
                Field viewsField = windowManagerGlobalClass.getDeclaredField("mViews");
                viewsField.setAccessible(true);
                Object views = viewsField.get(windowManagerGlobal);
                
                if (views instanceof View[]) {
                    View[] viewArray = (View[]) views;
                    if (viewArray.length > 0) {
                        return viewArray[0];
                    }
                } else if (views instanceof java.util.ArrayList) {
                    java.util.ArrayList viewList = (java.util.ArrayList) views;
                    if (!viewList.isEmpty()) {
                        return (View) viewList.get(0);
                    }
                }
            }
            
            Log.e(TAG, "Could not find any root view");
            return null;
        } catch (Exception e) {
            Log.e(TAG, "Error getting root view: " + e.getMessage());
            return null;
        }
    }
}