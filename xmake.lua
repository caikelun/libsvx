set_warnings("all", "error")
set_languages("c11")

if modes("r") then
    set_symbols("hidden")
    set_optimize("fastest")
    set_strip("all")
end

if modes("d") then
    set_symbols("debug")
    set_optimize("none")
end

if modes("prof") then
    set_symbols("debug")
    set_optimize("fastest")
    add_cxflags("-pg")
    add_ldflags("-pg")
end

if modes("cover") then
    set_symbols("debug")
    set_optimize("none")
    add_cxflags("--coverage")
    add_ldflags("--coverage")
end

if modes("asan") then
    set_symbols("debug")
    set_optimize("none")
    add_cxflags("-fsanitize=address")
    add_ldflags("-fsanitize=address")
end

if modes("tsan") then
    set_symbols("debug")
    set_optimize("none")
    add_cxflags("-fsanitize=thread", "-fPIE")
    add_ldflags("-fsanitize=thread", "-fPIE", "-pie")
end

if modes("lsan") then
    set_symbols("debug")
    set_optimize("none")
    add_cxflags("-fsanitize=leak")
    add_ldflags("-fsanitize=leak")
end

if modes("usan") then
    set_symbols("debug")
    set_optimize("none")
    add_cxflags("-fsanitize=undefined")
    add_ldflags("-fsanitize=undefined")
end

-- libsvx.a & libsvx.so
add_target("svx")
set_kind("$(kind)")
set_headerdir("$(buildir)/include")
set_objectdir("$(buildir)/.objs")
add_headers("src/(*.h)")
add_files("src/*.c")
set_config_h("src/svx_auto_config.h")
set_config_h_prefix("SVX")
add_cfunc(nil, "POLL",         nil, {"sys/poll.h"},     "poll")
add_cfunc(nil, "SELECT",       nil, {"sys/select.h"},   "select")
add_cfunc(nil, "EPOLL",        nil, {"sys/epoll.h"},    "epoll_create")
add_cfunc(nil, "EVENTFD",      nil, {"sys/eventfd.h"},  "eventfd")
add_cfunc(nil, "TIMERFD",      nil, {"sys/timerfd.h"},  "timerfd_create")
add_cfunc(nil, "SIGNALFD",     nil, {"sys/signalfd.h"}, "signalfd")
add_cfunc(nil, "INOTIFY",      nil, {"sys/inotify.h"},  "inotify_init")
add_cfunc(nil, "GLIBC_ENDIAN", nil, {"endian.h"},       "htobe64")

-- test
add_target("test")
set_kind("binary")
add_deps("svx")
set_objectdir("$(buildir)/.objs")
add_includedirs("src")
add_linkdirs("$(buildir)")
add_links("svx", "pthread")
add_files("test/*.c")

-- benchmarks: httpserver
add_target("httpserver")
set_kind("binary")
add_deps("svx")
set_objectdir("$(buildir)/.objs")
add_includedirs("src")
add_linkdirs("$(buildir)")
add_links("svx", "pthread")
add_files("benchmarks/httpserver/*.c")
