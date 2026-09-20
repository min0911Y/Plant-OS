import java.lang.management.CompilationMXBean;
import java.lang.management.ManagementFactory;
import java.lang.management.RuntimeMXBean;
import java.nio.channels.FileChannel;
import java.nio.channels.FileLock;
import java.nio.channels.OverlappingFileLockException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardOpenOption;
import java.util.concurrent.atomic.AtomicLong;
import javax.management.ObjectName;
import jdk.jfr.Event;
import jdk.jfr.FlightRecorder;
import jdk.jfr.Label;
import jdk.jfr.Name;
import jdk.jfr.Recording;

public final class PlatformChecks {
    @Name("plant.PlatformProbe")
    @Label("Plant platform probe")
    static final class PlatformEvent extends Event {
        @Label("Message")
        String message;
    }

    private static void check(boolean condition, String message) {
        if (!condition) throw new AssertionError(message);
    }

    public static void main(String[] args) throws Exception {
        Path result = Path.of(args[0]);
        try {
            management();
            processId();
            fileLock(result.resolveSibling("platform-lock.bin"));
            flightRecorder(Path.of(args[1]));
            Files.writeString(result, "OPENJDK PLATFORM PASS\n");
            System.out.println("OPENJDK PLATFORM PASS");
        } catch (Throwable failure) {
            Files.writeString(result, failure.toString() + "\n");
            throw failure;
        }
    }

    private static void management() throws Exception {
        RuntimeMXBean runtime = ManagementFactory.getRuntimeMXBean();
        check(runtime.getStartTime() > 0, "runtime start time");
        check(runtime.getUptime() >= 0, "runtime uptime");
        check(runtime.getInputArguments() != null, "runtime arguments");
        check(ManagementFactory.getMemoryMXBean().getHeapMemoryUsage() != null,
              "heap memory usage");
        check(ManagementFactory.getThreadMXBean().getThreadCount() > 0,
              "thread count");
        check(ManagementFactory.getOperatingSystemMXBean().getAvailableProcessors() > 0,
              "processor count");
        check(!ManagementFactory.getGarbageCollectorMXBeans().isEmpty(),
              "garbage collector beans");
        CompilationMXBean compilation = ManagementFactory.getCompilationMXBean();
        check(compilation != null && !compilation.getName().isEmpty(),
              "compilation bean");
        check(ManagementFactory.getPlatformMBeanServer().isRegistered(
                  new ObjectName(ManagementFactory.RUNTIME_MXBEAN_NAME)),
              "platform MBean server");
    }

    private static void processId() throws InterruptedException {
        long pid = ProcessHandle.current().pid();
        AtomicLong threadPid = new AtomicLong();
        Thread thread = new Thread(() -> threadPid.set(ProcessHandle.current().pid()));
        thread.start();
        thread.join();
        check(pid > 0 && threadPid.get() == pid, "process id");
    }

    private static void fileLock(Path path) throws Exception {
        Files.deleteIfExists(path);
        try (FileChannel first = FileChannel.open(path, StandardOpenOption.CREATE,
                                                  StandardOpenOption.READ,
                                                  StandardOpenOption.WRITE);
             FileChannel second = FileChannel.open(path, StandardOpenOption.READ,
                                                   StandardOpenOption.WRITE)) {
            try (FileLock lock = first.tryLock()) {
                check(lock != null && lock.isValid(), "initial file lock");
                try {
                    second.tryLock();
                    throw new AssertionError("overlapping file lock accepted");
                } catch (OverlappingFileLockException expected) {
                    // The JVM must reject overlapping locks held by this process.
                }
            }
            try (FileLock lock = second.tryLock()) {
                check(lock != null && lock.isValid(), "released file lock");
            }
        } finally {
            Files.deleteIfExists(path);
        }
    }

    private static void flightRecorder(Path destination) throws Exception {
        check(FlightRecorder.isAvailable(), "flight recorder availability");
        Files.deleteIfExists(destination);
        try (Recording recording = new Recording()) {
            recording.enable(PlatformEvent.class);
            recording.setDestination(destination);
            recording.start();
            PlatformEvent event = new PlatformEvent();
            event.message = "minecraft-server-plan";
            event.commit();
            recording.stop();
        }
        check(Files.size(destination) > 0, "flight recording output");
    }
}
