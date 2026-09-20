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

    private static double constants(float value) {
        // Mix four- and eight-byte constants in the same compiled code buffer.
        return Math.abs(value * -1.25f) + 2.5d / (value + 0.5d);
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
                double floatingExpected = Math.abs(seed * 5.0 / 4.0) + 5.0 / (2 * seed + 1);
                double floatingActual = constants(seed);
                if (floatingActual != floatingExpected) {
                    throw new AssertionError("constants(" + seed + "): " + floatingActual
                            + " != " + floatingExpected);
                }
            }
            Files.writeString(result, "OPENJDK JIT WORKLOAD PASS\n");
        } catch (Throwable failure) {
            Files.writeString(result, failure.toString() + "\n");
            throw failure;
        }
    }
}
