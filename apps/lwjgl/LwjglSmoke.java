import org.lwjgl.glfw.GLFWErrorCallback;
import org.lwjgl.glfw.GLFWKeyCallback;
import org.lwjgl.opengl.GLCapabilities;
import org.lwjgl.opengl.GL;
import org.lwjgl.stb.STBIWriteCallback;
import org.lwjgl.stb.STBImage;
import org.lwjgl.stb.STBImageWrite;
import org.lwjgl.stb.STBTTFontinfo;
import org.lwjgl.stb.STBTruetype;
import org.lwjgl.system.MemoryStack;
import org.lwjgl.openal.AL;
import org.lwjgl.openal.ALC;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.PrintWriter;
import java.io.StringWriter;
import java.nio.ByteBuffer;
import java.nio.FloatBuffer;
import java.nio.IntBuffer;
import java.nio.LongBuffer;
import java.nio.ShortBuffer;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;

import static org.lwjgl.glfw.GLFW.*;
import static org.lwjgl.opengl.GL11C.*;
import static org.lwjgl.opengl.GL15C.*;
import static org.lwjgl.opengl.GL20C.*;
import static org.lwjgl.opengl.GL30C.*;
import static org.lwjgl.stb.STBImage.*;
import static org.lwjgl.openal.AL10.*;
import static org.lwjgl.openal.ALC10.*;
import static org.lwjgl.openal.SOFTLoopback.*;

import static org.lwjgl.system.MemoryStack.*;
import static org.lwjgl.system.MemoryUtil.*;

public final class LwjglSmoke {
    private static final int WIDTH = 96;
    private static final int HEIGHT = 96;
    private static final Path RESULT = Path.of("C:/java/lwjgl/lwjgl-result.txt");
    private static final StringBuilder STAGES = new StringBuilder();

    private static final String VERTEX_SHADER =
        "#version 330 core\n" +
        "layout(location = 0) in vec2 position;\n" +
        "void main() { gl_Position = vec4(position, 0.0, 1.0); }\n";
    private static final String FRAGMENT_SHADER =
        "#version 330 core\n" +
        "uniform vec4 color;\n" +
        "out vec4 fragment;\n" +
        "void main() { fragment = color; }\n";

    private static void require(boolean condition, String message) {
        if (!condition) {
            throw new AssertionError(message);
        }
    }

    private static void mark(String stage) {
        STAGES.append(stage).append('\n');
    }

    private static void writeResult() throws IOException {
        Files.write(RESULT, STAGES.toString().getBytes("UTF-8"));
    }

    private static void coreTest() {
        ByteBuffer buffer = memAlloc(32);
        try {
            buffer.put(0, (byte)0x5a);
            buffer = memRealloc(buffer, 64);
            require(buffer != null && buffer.get(0) == (byte)0x5a,
                    "native realloc did not preserve data");
            mark("JNI");
        } finally {
            memFree(buffer);
        }

        try (MemoryStack outer = stackPush()) {
            IntBuffer values = outer.ints(11, 22, 33);
            require(values.get(1) == 22, "outer memory stack value");
            try (MemoryStack inner = stackPush()) {
                LongBuffer values64 = inner.mallocLong(1);
                values64.put(0, 0x1122334455667788L);
                require(values64.get(0) == 0x1122334455667788L,
                        "nested memory stack value");
            }
        }
        mark("CORE");
        System.out.println("LWJGL CORE PASS");
    }

    private static void stbTest() throws IOException {
        ByteBuffer rgba = memAlloc(16);
        ByteBuffer decoded = null;
        ByteBuffer input = null;
        STBIWriteCallback writer = null;
        try {
            rgba.put(new byte[] {
                (byte)0xff, 0x00, 0x00, (byte)0xff,
                0x00, (byte)0xff, 0x00, (byte)0xff,
                0x00, 0x00, (byte)0xff, (byte)0xff,
                (byte)0xff, (byte)0xff, 0x00, (byte)0xff
            }).flip();

            ByteArrayOutputStream encoded = new ByteArrayOutputStream();
            writer = STBIWriteCallback.create((context, data, size) -> {
                ByteBuffer chunk = STBIWriteCallback.getData(data, size);
                byte[] bytes = new byte[size];
                chunk.get(bytes);
                encoded.write(bytes, 0, bytes.length);
            });
            require(STBImageWrite.stbi_write_png_to_func(writer, 0L, 2, 2, 4,
                                                         rgba, 8),
                    "stb png callback writer");
            mark("CALLBACK");

            byte[] png = encoded.toByteArray();
            require(png.length > 64 && png[0] == (byte)0x89,
                    "stb png output");
            input = memAlloc(png.length);
            input.put(png).flip();
            try (MemoryStack stack = stackPush()) {
                IntBuffer width = stack.mallocInt(1);
                IntBuffer height = stack.mallocInt(1);
                IntBuffer channels = stack.mallocInt(1);
                decoded = STBImage.stbi_load_from_memory(input, width, height,
                                                         channels, STBI_rgb_alpha);
                require(decoded != null && width.get(0) == 2 && height.get(0) == 2,
                        "stb png decoder dimensions");
                require((decoded.get(0) & 0xff) == 0xff &&
                        (decoded.get(1) & 0xff) == 0x00 &&
                        (decoded.get(2) & 0xff) == 0x00 &&
                        (decoded.get(3) & 0xff) == 0xff,
                        "stb png decoder pixels");
            }
        } finally {
            if (decoded != null) {
                STBImage.stbi_image_free(decoded);
            }
            if (writer != null) {
                writer.free();
            }
            memFree(input);
            memFree(rgba);
        }

        byte[] fontBytes = Files.readAllBytes(Path.of("R:/data/fonts/mono.ttf"));
        ByteBuffer font = memAlloc(fontBytes.length);
        STBTTFontinfo info = STBTTFontinfo.malloc();
        try {
            font.put(fontBytes).flip();
            require(STBTruetype.stbtt_InitFont(info, font), "stb truetype init");
            require(STBTruetype.stbtt_ScaleForPixelHeight(info, 16.0f) > 0,
                    "stb truetype scale");
            try (MemoryStack stack = stackPush()) {
                IntBuffer advance = stack.mallocInt(1);
                STBTruetype.stbtt_GetCodepointHMetrics(info, 'A', advance, null);
                require(advance.get(0) > 0, "stb truetype metrics");
            }
        } finally {
            info.free();
            memFree(font);
        }
        mark("STB");
        System.out.println("LWJGL STB PASS");
    }

    private static void openalTest() {
        long device = alcLoopbackOpenDeviceSOFT((ByteBuffer)null);
        require(device != NULL, "OpenAL loopback device");
        long context = NULL;
        try (MemoryStack stack = stackPush()) {
            org.lwjgl.openal.ALCCapabilities caps = ALC.createCapabilities(device);
            context = alcCreateContext(device, stack.ints(ALC_FREQUENCY, 48000,
                ALC_FORMAT_CHANNELS_SOFT, ALC_STEREO_SOFT,
                ALC_FORMAT_TYPE_SOFT, ALC_SHORT_SOFT, 0));
            require(context != NULL && alcMakeContextCurrent(context), "OpenAL context");
            AL.createCapabilities(caps);
            ShortBuffer samples = stack.mallocShort(1024);
            for (int i = 0; i < samples.capacity(); i++) {
                samples.put(i, (short)(Math.sin(i * Math.PI / 32) * 16000));
            }
            int buffer = alGenBuffers();
            int source = alGenSources();
            try {
                alBufferData(buffer, AL_FORMAT_MONO16, samples, 48000);
                alSourcei(source, AL_BUFFER, buffer);
                alSourcei(source, AL_LOOPING, AL_TRUE);
                alSourcePlay(source);
                ShortBuffer output = stack.mallocShort(4096);
                alcRenderSamplesSOFT(device, output, 2048);
                long energy = 0;
                for (int i = 0; i < output.capacity(); i++) {
                    energy += Math.abs((int)output.get(i));
                }
                require(energy > 1000000 && alGetError() == AL_NO_ERROR &&
                        alcGetError(device) == ALC_NO_ERROR, "OpenAL actual PCM mixing");
            } finally {
                alDeleteSources(source);
                alDeleteBuffers(buffer);
            }
        } finally {
            alcMakeContextCurrent(NULL);
            if (context != NULL) alcDestroyContext(context);
            alcCloseDevice(device);
            AL.setCurrentThread(null);
        }
        mark("OPENAL");
    }

    private static int shader(int type, String source) {
        int shader = glCreateShader(type);
        glShaderSource(shader, source);
        glCompileShader(shader);
        require(glGetShaderi(shader, GL_COMPILE_STATUS) == GL_TRUE,
                "shader compile: " + glGetShaderInfoLog(shader));
        return shader;
    }

    private static int createProgram() {
        int vertex = shader(GL_VERTEX_SHADER, VERTEX_SHADER);
        int fragment = shader(GL_FRAGMENT_SHADER, FRAGMENT_SHADER);
        int program = glCreateProgram();
        glAttachShader(program, vertex);
        glAttachShader(program, fragment);
        glLinkProgram(program);
        glDeleteShader(vertex);
        glDeleteShader(fragment);
        require(glGetProgrami(program, GL_LINK_STATUS) == GL_TRUE,
                "program link: " + glGetProgramInfoLog(program));
        return program;
    }

    private static int render(int program, int location, float red, float green,
                              ByteBuffer pixels) {
        glViewport(0, 0, WIDTH, HEIGHT);
        glClearColor(0.05f, 0.10f, 0.20f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glUseProgram(program);
        glUniform4f(location, red, green, 0.05f, 1.0f);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glFinish();
        glReadPixels(0, 0, WIDTH, HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        int colored = 0;
        for (int index = 0; index < WIDTH * HEIGHT; index++) {
            int offset = index * 4;
            int r = pixels.get(offset) & 0xff;
            int g = pixels.get(offset + 1) & 0xff;
            if (r > 180 && g < 80 && red > green ||
                g > 180 && r < 80 && green > red) {
                colored++;
            }
        }
        require(colored > 100, "OpenGL triangle pixel readback");
        return colored;
    }

    private static void glfwOpenGLTest() throws IOException, InterruptedException {
        AtomicInteger callbackErrors = new AtomicInteger();
        AtomicReference<String> lastError = new AtomicReference<>();
        GLFWErrorCallback errors = GLFWErrorCallback.create((code, description) -> {
            callbackErrors.incrementAndGet();
            lastError.set(code + ": " + memUTF8Safe(description));
            System.err.println("LWJGL GLFW error " + code + ": " +
                               memUTF8Safe(description));
        });
        GLFWKeyCallback keys = null;
        long window = NULL;
        int vao = 0;
        int vbo = 0;
        int program = 0;
        ByteBuffer pixels = memAlloc(WIDTH * HEIGHT * 4);
        AtomicBoolean keySeen = new AtomicBoolean();
        try {
            require(glfwInit(), "glfwInit");
            glfwSetErrorCallback(errors);
            mark("GLFW");
            glfwDefaultWindowHints();
            glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
            glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
            glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
            glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);
            window = glfwCreateWindow(WIDTH, HEIGHT, "Plant LWJGL 3", NULL, NULL);
            require(window != NULL, "glfwCreateWindow: " + lastError.get());
            glfwSetWindowSize(window, 0, HEIGHT);
            keys = GLFWKeyCallback.create((handle, key, scancode, action, mods) -> {
                if (key == GLFW_KEY_A && action == GLFW_PRESS) {
                    keySeen.set(true);
                }
                if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
                    glfwSetWindowShouldClose(handle, true);
                }
            });
            glfwSetKeyCallback(window, keys);
            glfwMakeContextCurrent(window);
            glfwSwapInterval(0);
            GLCapabilities capabilities = GL.createCapabilities();
            require(capabilities.OpenGL33, "OpenGL 3.3 capabilities");

            vao = glGenVertexArrays();
            vbo = glGenBuffers();
            glBindVertexArray(vao);
            glBindBuffer(GL_ARRAY_BUFFER, vbo);
            try (MemoryStack stack = stackPush()) {
                FloatBuffer vertices = stack.floats(-0.75f, -0.75f, 0.75f, -0.75f,
                                                    0.0f, 0.75f);
                glBufferData(GL_ARRAY_BUFFER, vertices, GL_STATIC_DRAW);
            }
            glVertexAttribPointer(0, 2, GL_FLOAT, false, 0, 0L);
            glEnableVertexAttribArray(0);
            program = createProgram();
            int color = glGetUniformLocation(program, "color");
            require(color >= 0, "shader color uniform");
            render(program, color, 1.0f, 0.0f, pixels);
            glfwSwapBuffers(window);
            System.out.println("LWJGL WINDOW READY");

            Thread wake = new Thread(() -> {
                try {
                    Thread.sleep(120);
                    glfwPostEmptyEvent();
                } catch (InterruptedException exception) {
                    Thread.currentThread().interrupt();
                }
            });
            wake.start();
            long deadline = System.nanoTime() + 8_000_000_000L;
            while (!glfwWindowShouldClose(window) && !keySeen.get() &&
                   System.nanoTime() < deadline) {
                glfwWaitEventsTimeout(0.1);
            }
            wake.join();
            require(keySeen.get(), "GLFW key callback (send A)");
            render(program, color, 0.0f, 1.0f, pixels);
            glfwSwapBuffers(window);
            mark("OPENGL");
            System.out.println("LWJGL KEY CALLBACK PASS");
            System.out.println("LWJGL OPENGL PASS");
            Thread.sleep(1000);
        } finally {
            memFree(pixels);
            if (program != 0) {
                glDeleteProgram(program);
            }
            if (vbo != 0) {
                glDeleteBuffers(vbo);
            }
            if (vao != 0) {
                glDeleteVertexArrays(vao);
            }
            if (window != NULL) {
                glfwSetKeyCallback(window, null);
                glfwDestroyWindow(window);
            }
            GL.setCapabilities(null);
            GL.destroy();
            if (keys != null) {
                keys.free();
            }
            glfwTerminate();
            glfwSetErrorCallback(null);
            errors.free();
        }
        require(callbackErrors.get() > 0, "GLFW error callback was not invoked");
        System.out.println("LWJGL GLFW ERROR CALLBACK PASS");
    }

    public static void main(String[] args) throws Exception {
        try {
            coreTest();
            stbTest();
            openalTest();
            glfwOpenGLTest();
            writeResult();
            System.out.println("LWJGL JNI CORE CALLBACK GLFW OPENGL STB OPENAL PASS");
        } catch (Throwable failure) {
            StringWriter trace = new StringWriter();
            failure.printStackTrace(new PrintWriter(trace));
            try {
                writeResult();
                Files.write(Path.of("C:/java/lwjgl/lwjgl-failure.txt"),
                            trace.toString().getBytes("UTF-8"));
            } catch (IOException ignored) {
            }
            throw failure;
        }
    }

}
