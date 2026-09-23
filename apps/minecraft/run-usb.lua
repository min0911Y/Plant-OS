-- Invoke with an absolute Plant path, for example: lua.bin E:/plmc/run.lua
local root = assert(arg[0]:match("^([A-Za-z]:/.*)/[^/]+$"),
                    "Use an absolute path: lua.bin E:/plmc/run.lua")
assert(not root:find('["\r\n]'), "Invalid package path")
local command = '"' .. root .. '/lwjgl-launcher.bin" --directory "' ..
                root .. '/java/mc" "' .. root .. '/java/bin/java" @client.args'
assert(os.execute(command))
