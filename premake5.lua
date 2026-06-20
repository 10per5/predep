workspace("predep")
configurations({ "release", "debug" })

newoption {
    trigger = "no-elevation",
    description = "Disable privilege elevation (sudo/UAC) for install/uninstall stages"
}

project("predep")
kind("ConsoleApp")
language("C++")
cppdialect("C++23")
targetdir("bin")
files({ "src/**.cpp" })
includedirs({ "vendor", "src" })

if not _OPTIONS["no-elevation"] then
    defines({ "ALLOW_ELEVATION" })
end

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
