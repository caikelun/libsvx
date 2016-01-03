Main Page {#mainpage}
=========


Overview
--------

libsvx (service X library) is a pure C network library. It only supports
Linux, but it depend on very few libraries (only libc and libpthread).

libsvx use reactor pattern. It provides a mechanism to execute a callback
function when a specific event occurs on a file descriptor or after a
timeout has been reached. Based on this mechanism, libsvx provides the
following basic network modules: TCP server module, TCP client module,
UDP module and ICMP module. libsvx does *NOT* contain any application layer
network modules like HTTP server, HTTP client, DNS client, etc.

libsvx use the thread mode called: one I/O looper pre thread, with optional
thread pool.


Feature
-------

* supports IPv4 and IPv6
* supports epoll, poll and select
* TCP server module
* TCP client module
* UDP module (unicast and multicast)
* ICMP module (ICMPv4 and ICMPv6)
* asynchronous log module
* crash log module
* thread pool module
* process helper module (watchdog, daemon, singleton, user/group, signal, etc.)
* use BSD queue(3) and tree(3) data structure


Benchmark
---------

Here is a performance benchmark testing that libsvx vs. Nginx. We wrote a
very simple HTTP server (benchmarks/httpserver/main.c) using libsvx for test.
We use weighttp(https://github.com/lighttpd/weighttp) as the benchmarking tool,
and use matplotlib(http://matplotlib.org/) to create a image from test results.
libsvx/nginx and weighttp run in the same machine.

* Hardware: Intel Core i5-3470 @ 3.20GHz (Quad-core), 8GB RAM, 1000mbps Ethernet

* OS: Debian 8 (Linux kernel 3.16)

* libsvx version: 0.0.4

* libsvx test HTTP server start/stop:

        start: ./benchmarks/httpserver/httpserver
        stop:  ./benchmarks/httpserver/httpserver stop

* Nginx version: 1.9.6

* Nginx conf:

        ......
        worker_processes 1; #2;
        worker_rlimit_nofile 10240;
        events {
            worker_connections  10240;
        }
        http {
            ......
            keepalive_timeout  65;
            keepalive_requests 10000;
            server {
                ......
                listen 80;
                location /hello {
                    default_type text/plain;
                    return 200 'hello world!';
                }
            }
        }
        ......

* Weighttp version: 0.3

* Weighttp command:

        weighttp -k -n 100000 -c [1|2|5| ... |1000] -t [1|2] http://127.0.0.1:port/hello

* Automated test script: benchmarks/httpserver/test.py

![libsvx vs. Nginx (1 thread/worker)](doc/bm_httpsvr_1.png)

![libsvx vs. Nginx (2 threads/workers)](doc/bm_httpsvr_2.png)


Website, Clone and Download
---------------------------

* Github: https://github.com/caikelun/libsvx
* Coding: https://coding.net/u/caikelun/p/libsvx
* OSC: https://git.oschina.net/caikelun/libsvx


Compile
-------

libsvx is compiled by GCC, the source code compatible with C90, GNU90, C99, GNU99,
C11 and GNU11 standard. C11 standard is used by default in the current Makefile.

* Compiled using homemade script and Makefile:

        Configure : ./configure
        Compile   : make [prof=y|n]
                    make build=d [cover=y|n] [trapv=y|n] [asan=y|n] [tsan=y|n]
        Clean     : make clean
        Clean all : make distclean

        >>> MAKE OPTIONS <<<
        build = d (debug) | r (release, default) : Build for debug(-O0 -g3) or
                                                   release (-O3 -fvisibility=hidden, strip).
        prof  = y (yes)   | n (no, default)      : Build with -pg. (available when build=r)
        cover = y (yes)   | n (no, default)      : Build with --coverage. (available when build=d)
        trapv = y (yes)   | n (no, default)      : Build with -ftrapv. (available when build=d)
        asan  = y (yes)   | n (no, default)      : Build with -fsanitize=address, -fsanitize=leak
                                                   and -fsanitize=undefined. (available when build=d)
        tsan  = y (yes) | n (no, default)        : Build with -fsanitize=thread. (available when build=d)

* Or, Compiled using xmake (learn more about: https://github.com/waruqi/xmake):

        Compile : xmake config --mode=release; xmake -r
                  xmake config --mode=debug; xmake -r
        Clean   : xmake clean

        >>> NOTICE <<<
        The current ./xmake.lua place all output files to the ./build directory.


Test
----

libsvx have a unit test program in the ./test directory. You can use it in two ways:

* Run with valgrind. In this way, you can compile the test programe with
  build=r or build=d. But do *NOT* enable the prof, cover, trapv, asan and
  tsan options.
* Run directly. In this way, You *MUST* compile the test programe with
  build=d. Then you can enable one or more of above options.

        Run with valgrind : sudo ./test/test -g
        Run directly      : sudo ./test/test -d

        >>> NOTICE <<<
        The ICMP unit test need ROOT privilege.

PAY ATTENTION:

After testing with "tsan=y", your *SHOULD* review each warning very carefully.
Some of the "data race", in fact, will not cause any error or crash. If you
try to solve them with mutex or atomic, this will cause performance problems.


Samples
-------

* benchmarks/httpserver/main.c
* test/


Documents
---------

Build local documents from source code use doxygen:

    make doc

Visit online documents:

http://caikelun.github.io/proj/libsvx/doxy/index.html


ToDo
----
* svx_crash: Dump registers value for ARM architecture.
* svx_crash: Dump registers value for ARM64 architecture.
* More benchmarks.
* More samples.
* More documents.


License
-------

All of the code and documentation in libsvx has been dedicated to
the public domain by the authors. Anyone is free to copy, modify,
publish, use, compile, sell, or distribute the original libsvx code,
either in source code form or as a compiled binary, for any purpose,
commercial or non-commercial, and by any means.

The BSD queue(3) and tree(3) data structure header files
(src/svx_queue.h and src/svx_tree.h) are governed by a BSD license
that can be found at the head of each files.
