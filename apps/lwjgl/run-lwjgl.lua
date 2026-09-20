-- Run the LWJGL smoke client from the PlantOS Java disk.
-- Keep this command in Lua so it is easy to edit while debugging in psh.
local command = [[lwjgl-launcher.bin C:/java/bin/java -XX:ErrorFile=C:/java/lwjgl/hs_err.log -Xms16m -Xmx128m -Dorg.lwjgl.librarypath=C:/java/lwjgl/native -Djava.library.path=C:/java/lwjgl/native -Dorg.lwjgl.glfw.libname=libglfw.so -Dorg.lwjgl.opengl.libname=libGL.so -cp C:/java/lwjgl/classes;C:/java/lwjgl/jar/lwjgl-3.3.6.jar;C:/java/lwjgl/jar/lwjgl-glfw-3.3.6.jar;C:/java/lwjgl/jar/lwjgl-opengl-3.3.6.jar;C:/java/lwjgl/jar/lwjgl-stb-3.3.6.jar;C:/java/lwjgl/jar/lwjgl-openal-3.3.6.jar;C:/java/lwjgl/jar/jspecify-1.0.0.jar LwjglSmoke]]

assert(os.execute(command))
