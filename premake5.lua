workspace("predep")
configurations({ "release", "debug" })

project("predep")
kind("ConsoleApp")
language("C++")
cppdialect("C++23")
targetdir("bin")
files({ "src/**.cpp" })
includedirs({ "vendor", "src" })

filter("configurations:release")
optimize("Speed")
defines({ "NDEBUG" })
postbuildcommands({ "strip $(TARGET)" })

filter("system:linux")
links({ "curl", "ssl", "crypto", "pthread" })

filter("system:macosx")
links({ "curl", "ssl", "crypto" })

filter("system:windows")
links({ "libcurl", "ws2_32", "crypt32" })

-- Helpers

local function is_windows()
	return os.getenv("OS") == "Windows_NT"
end

local function exe()
	return is_windows() and "predep.exe" or "predep"
end

local function mkdir_p(path)
	if is_windows() then
		os.execute('cmd /c if not exist "' .. path .. '" mkdir "' .. path .. '"')
	else
		os.execute("mkdir -p " .. path)
	end
end

local function cp(src, dst)
	if is_windows() then
		os.execute('cmd /c copy /y "' .. src .. '" "' .. dst .. '"')
	else
		os.execute("cp " .. src .. " " .. dst)
	end
end

local function file_exists(path)
	local f = io.open(path, "r")
	if f then
		f:close()
		return true
	end
	return false
end

local function symlink(target, link)
	if is_windows() then
		return
	end
	os.execute("ln -sf " .. target .. " " .. link)
end

local function default_prefix()
	if is_windows() then
		local pf = os.getenv("PROGRAMFILES")
		if pf then
			return pf .. "/predep"
		end
		return "C:/Program Files/predep"
	end
	return "/usr/local"
end

newaction({
	trigger = "install",
	description = "Install predep to system",
	execute = function()
		local prefix = os.getenv("PREFIX") or default_prefix()
		local bindir = prefix .. "/bin"
		local bin_src = "bin/" .. exe()

		if not file_exists(bin_src) then
			print("  ERROR: " .. bin_src .. " not found")
			print("  Build first with: make")
			return
		end

		mkdir_p(bindir)

		local bin_dst = bindir .. "/" .. exe()
		cp(bin_src, bin_dst)

		-- Symlink into PATH when installed to a non-standard prefix
		if not is_windows() and prefix ~= "/usr/local" then
			mkdir_p("/usr/local/bin")
			symlink(bin_dst, "/usr/local/bin/predep")
		end

		print("  -> Installed to " .. bin_dst)
	end,
})

newaction({
	trigger = "uninstall",
	description = "Remove installed predep",
	execute = function()
		local prefix = os.getenv("PREFIX") or default_prefix()
		local bindir = prefix .. "/bin"

		os.execute("rm -f " .. bindir .. "/" .. exe())
		if not is_windows() then
			os.execute("rm -f /usr/local/bin/predep")
		end

		os.execute("rmdir " .. bindir .. " 2>/dev/null")
		os.execute("rmdir " .. prefix .. " 2>/dev/null")

		print("  -> Removed from " .. bindir)
	end,
})
