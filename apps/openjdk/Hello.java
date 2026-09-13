import java.nio.file.Files;
import java.nio.file.Path;

public final class Hello {
    public static void main(String[] args) throws Exception {
        System.out.println("Hello, world!");
        if (args.length != 0) {
            Files.writeString(Path.of(args[0]), "Hello, world!\n");
        }
    }
}
