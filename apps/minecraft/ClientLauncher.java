package org.plantos.launcher;

import java.io.FileOutputStream;
import java.io.PrintStream;
import java.lang.reflect.InvocationTargetException;
import java.util.Arrays;

/** Preserve startup failures even before Minecraft configures Log4j. */
public final class ClientLauncher {
    public static void main(String[] args) throws Throwable {
        PrintStream log = new PrintStream(new FileOutputStream("client.log"), true, "UTF-8");
        System.setOut(log);
        System.setErr(log);
        try {
            Class.forName(args[0]).getMethod("main", String[].class)
                .invoke(null, (Object) Arrays.copyOfRange(args, 1, args.length));
        } catch (InvocationTargetException exception) {
            throw exception.getCause();
        }
    }
}
