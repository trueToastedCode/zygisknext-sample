package de.truetoastedcode.znmodsample;

import android.util.Log;

public final class EntryPoint {
    private static final String TAG = "znmodsample";

    public static void init() {
        nativeMethod();
    }

    private static native void nativeMethod();
}