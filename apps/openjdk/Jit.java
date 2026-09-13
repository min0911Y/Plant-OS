import java.nio.file.Files;
import java.nio.file.Path;

public final class Jit {
    private static long kernel(int seed) {
        long result = 0;
        for (int i = 0; i < 256; i++) {
            result += (long) i * seed + (long) i * i;
        }
        return result;
    }

    public static void main(String[] args) throws Exception {
        Path result = Path.of(args[0]);
        try {
            for (int seed = -10000; seed < 10000; seed++) {
                long expected = 32640L * seed + 5559680L;
                long actual = kernel(seed);
                if (actual != expected) {
                    throw new AssertionError("kernel(" + seed + "): " + actual + " != " + expected);
                }
            }
            Files.writeString(result, "OPENJDK JIT WORKLOAD PASS\n");
        } catch (Throwable failure) {
            Files.writeString(result, failure.toString() + "\n");
            throw failure;
        }
    }
}
