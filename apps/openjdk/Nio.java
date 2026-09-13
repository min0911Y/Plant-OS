import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.net.StandardProtocolFamily;
import java.nio.ByteBuffer;
import java.nio.channels.*;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.concurrent.atomic.AtomicReference;

public final class Nio {
    private static void check(boolean result, String message) {
        if (!result) throw new AssertionError(message);
    }

    public static void main(String[] args) throws Exception {
        Path result = Path.of(args[0]);
        try {
            Startup.main(new String[0]);
            pipe();
            sockets();
            Files.writeString(result, "OPENJDK NIO PASS\n");
            System.out.println("OPENJDK NIO PASS");
        } catch (Throwable failure) {
            Files.writeString(result, failure.toString() + "\n");
            throw failure;
        }
    }

    private static void pipe() throws Exception {
        AtomicReference<Throwable> error = new AtomicReference<>();
        Pipe pipe = Pipe.open();
        try (Selector selector = Selector.open();
             Pipe.SourceChannel source = pipe.source();
             Pipe.SinkChannel sink = pipe.sink()) {
            source.configureBlocking(false);
            source.register(selector, SelectionKey.OP_READ);
            check(selector.selectNow() == 0, "empty pipe readiness");
            long before = System.nanoTime();
            check(selector.select(40) == 0 && System.nanoTime() - before >= 40_000_000,
                  "selector timeout");
            Thread writer = new Thread(() -> {
                try {
                    Thread.sleep(50);
                    sink.write(ByteBuffer.wrap(new byte[] {42}));
                } catch (Throwable failure) { error.set(failure); }
            });
            writer.start();
            check(selector.select(5000) == 1, "pipe event wakes selector");
            writer.join();
            check(error.get() == null, "writer failed: " + error.get());
            ByteBuffer byteBuffer = ByteBuffer.allocate(1);
            check(source.read(byteBuffer) == 1 && byteBuffer.get(0) == 42, "pipe contents");
            selector.selectedKeys().clear();
            Thread waker = new Thread(() -> {
                try { Thread.sleep(50); } catch (InterruptedException e) { throw new AssertionError(e); }
                selector.wakeup();
            });
            waker.start();
            check(selector.select() == 0, "selector explicit wakeup");
            waker.join();
            sink.close();
            check(selector.select(5000) == 1 && source.read(byteBuffer.clear()) == -1,
                  "pipe EOF readiness");
        }
    }

    private static void sockets() throws Exception {
        try (Selector selector = Selector.open();
             ServerSocketChannel server = ServerSocketChannel.open(StandardProtocolFamily.INET);
             SocketChannel client = SocketChannel.open(StandardProtocolFamily.INET)) {
            server.bind(new InetSocketAddress(InetAddress.getByName("127.0.0.1"), 0));
            server.configureBlocking(false);
            server.register(selector, SelectionKey.OP_ACCEPT);
            client.configureBlocking(false);
            boolean connected = client.connect(server.getLocalAddress());
            check(selector.select(5000) == 1, "accept readiness");
            try (SocketChannel accepted = server.accept()) {
                check(accepted != null, "accept connection");
                if (!connected) {
                    try (Selector connection = Selector.open()) {
                        client.register(connection, SelectionKey.OP_CONNECT);
                        check(connection.select(5000) == 1 && client.finishConnect(),
                              "nonblocking connect completion");
                    }
                }
                accepted.write(ByteBuffer.wrap(new byte[] {73}));
                server.keyFor(selector).cancel();
                selector.selectNow();
                selector.selectedKeys().clear();
                client.register(selector, SelectionKey.OP_READ);
                check(selector.select(5000) == 1, "socket read readiness");
                ByteBuffer data = ByteBuffer.allocateDirect(1);
                check(client.read(data) == 1 && data.get(0) == 73, "socket data");
            }
        }
    }
}
