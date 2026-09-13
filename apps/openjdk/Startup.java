import java.io.File;

public final class Startup {
    public static void main(String[] args) throws Exception {
        String home = System.getProperty("java.home");
        if (!";".equals(System.getProperty("path.separator"))) {
            throw new AssertionError("incorrect path.separator");
        }
        File directory = new File(home);
        if (!directory.isAbsolute() || !new File(directory, "lib/modules").isFile()) {
            throw new AssertionError("incorrect java.home: " + home);
        }
        if (!new File(directory.toURI()).equals(directory)) {
            throw new AssertionError("drive path URI round trip failed");
        }
        if (!new File(home.substring(0, 2) + "/").isAbsolute()) {
            throw new AssertionError("drive root is not absolute");
        }
        System.out.println("OPENJDK STARTUP PASS: " + home);
    }
}
