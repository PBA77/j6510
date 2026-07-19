package com.pba77.j6510basic;

public final class MainActivity {
    private static native void nativeReset();
    private static native void nativeSend(String text);
    private static native String nativePump(int instructionBudget);
    private static native void nativeBreak();

    public static void main(String[] args) {
        if (args.length != 1) {
            throw new IllegalArgumentException("native library path required");
        }
        System.load(args[0]);
        nativeReset();

        StringBuilder output = new StringBuilder();
        pumpUntilQuiet(output, 80);
        if (output.indexOf("BASIC") < 0 || output.indexOf("OK") < 0) {
            throw new AssertionError("BASIC did not reach its prompt:\n" + output);
        }

        nativeSend("PRINT 2+2");
        pumpUntilQuiet(output, 40);
        if (output.indexOf(" 4") < 0) {
            throw new AssertionError("PRINT 2+2 did not return 4:\n" + output);
        }

        nativeSend("10 PRINT 7");
        pumpFixed(output, 100);
        nativeSend("LIST");
        pumpFixed(output, 100);
        nativeSend("RUN");
        pumpFixed(output, 100);
        if (output.indexOf("10 PRINT 7") < 0 || output.indexOf("\n 7 ") < 0) {
            throw new AssertionError("stored BASIC program did not run:\n" + output);
        }

        nativeSend("20 GOTO 20");
        pumpFixed(output, 100);
        nativeSend("RUN");
        pumpFixed(output, 20);
        nativeBreak();
        pumpFixed(output, 100);
        nativeSend("PRINT 3+3");
        pumpFixed(output, 100);
        if (output.indexOf("\n 6 ") < 0) {
            throw new AssertionError("STOP did not return control to BASIC:\n" + output);
        }

        System.out.print(output);
        System.out.println("[smoke test passed]");
    }

    private static void pumpUntilQuiet(StringBuilder output, int maximumPumps) {
        int quietPumps = 0;
        for (int i = 0; i < maximumPumps && quietPumps < 5; ++i) {
            String chunk = nativePump(150_000);
            output.append(chunk);
            quietPumps = chunk.isEmpty() ? quietPumps + 1 : 0;
        }
    }

    private static void pumpFixed(StringBuilder output, int pumps) {
        for (int i = 0; i < pumps; ++i) {
            output.append(nativePump(150_000));
        }
    }
}
