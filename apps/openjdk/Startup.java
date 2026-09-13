import java.io.File;
import java.nio.file.Path;

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
        Path path = Path.of(home);
        Path root = Path.of(home.substring(0, 2) + "/");
        Path otherRoot = Path.of((home.charAt(0) == 'C' ? "D" : "C") + ":/");
        if (!path.isAbsolute() || !path.toAbsolutePath().equals(path) ||
                !Path.of(path.toUri()).equals(path) ||
                !Path.of(directory.toURI()).equals(path) ||
                !path.getRoot().equals(root) || root.getNameCount() != 0 ||
                root.getFileName() != null || root.getParent() != null ||
                !root.resolve("one").getParent().equals(root) ||
                !root.resolve("one").getFileName().equals(Path.of("one")) ||
                !root.resolve("one/../two").normalize().equals(root.resolve("two")) ||
                !root.resolve("one/..").normalize().equals(root) ||
                !root.resolve("one").relativize(root.resolve("two")).equals(Path.of("../two")) ||
                !path.startsWith(root) || path.startsWith(otherRoot) ||
                !path.endsWith(path.getFileName()) || root.endsWith(otherRoot) ||
                !root.resolve(otherRoot).equals(otherRoot)) {
            throw new AssertionError("NIO drive path semantics: " + path);
        }
        try {
            root.relativize(otherRoot);
            throw new AssertionError("NIO relativize accepted different roots");
        } catch (IllegalArgumentException expected) {
        }
        System.out.println("OPENJDK STARTUP PASS: " + home);
    }
}
