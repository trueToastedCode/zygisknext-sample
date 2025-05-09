package de.truetoastedcode.znmodsample;

import android.util.Log;

public final class EntryPoint {
    private static final String TAG = "znmodsample";

    public static void init() {
        Thread worker = new Thread(() -> {
            long startTime = System.currentTimeMillis();
            long duration = 2 * 60 * 1000; // 2 minutes in milliseconds

            while (System.currentTimeMillis() - startTime < duration) {
                try {
                    nativeMethod();
                    Thread.sleep(5000);  // Wait 5 seconds
                } catch (InterruptedException e) {
                    Log.w(TAG, "Worker thread interrupted", e);
                    Thread.currentThread().interrupt();
                    break;
                } catch (Exception e) {
                    Log.e(TAG, "Error calling native method", e);
                }
            }

            Log.i(TAG, "Finished periodic native calls after 2 minutes");
        });

        worker.setDaemon(true); // Optional
        worker.start();
    }

    private static native void nativeMethod();
}
