import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Arrays;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.atomic.AtomicReference;
import java.util.stream.IntStream;

public final class RuntimeChecks {
    private static void check(boolean condition, String message) {
        if (!condition) throw new AssertionError(message);
    }

    private static int divide(int divisor) {
        return 42 / divisor;
    }

    public static void main(String[] args) throws Exception {
        Path result = Path.of(args[0]);
        try {
            check(System.getenv() != null, "environment enumeration");
            check(IntStream.range(0, 10000).map(i -> i * 2).sum() == 99990000,
                  "lambda and stream result");
            check((int) RuntimeChecks.class.getDeclaredMethod("divide", int.class)
                      .invoke(null, 2) == 21, "reflection result");
            int caught = 0;
            for (int i = 0; i < 20000; i++) {
                try {
                    divide(i & 1);
                } catch (ArithmeticException expected) {
                    caught++;
                }
            }
            check(caught == 10000, "compiled exception handling");

            String value = "Plant OS: 中文 / Java 17\n";
            Path file = result.resolveSibling("runtime 中文 file.txt");
            Files.writeString(file, value, StandardCharsets.UTF_8);
            check(Files.readString(Path.of(file.toUri()), StandardCharsets.UTF_8).equals(value),
                  "UTF-8 file round trip");
            Files.delete(file);

            CountDownLatch start = new CountDownLatch(1);
            AtomicReference<Throwable> error = new AtomicReference<>();
            Thread[] workers = new Thread[4];
            long[] sums = new long[workers.length];
            for (int worker = 0; worker < workers.length; worker++) {
                final int index = worker;
                workers[worker] = new Thread(() -> {
                    try {
                        start.await();
                        for (int i = 0; i < 2000; i++) {
                            int[] data = new int[4096];
                            Arrays.fill(data, i);
                            sums[index] += data[i % data.length];
                        }
                    } catch (Throwable failure) {
                        error.compareAndSet(null, failure);
                    }
                });
                workers[worker].start();
            }
            start.countDown();
            for (Thread worker : workers) worker.join();
            if (error.get() != null) throw new AssertionError(error.get());
            for (long sum : sums) check(sum == 1999000, "thread allocation result");
            System.gc();
            Files.writeString(result, "OPENJDK RUNTIME PASS\n");
        } catch (Throwable failure) {
            Files.writeString(result, failure.toString() + "\n");
            throw failure;
        }
    }
}
