#
# This source code has been dedicated to the public domain by the author.
# Anyone is free to copy, modify, publish, use, compile, sell, or distribute
# this source code, either in source code form or as a compiled binary, 
# for any purpose, commercial or non-commercial, and by any means.
#

import sys
import os
import re
import time
import threading
import subprocess
import psutil
import argparse
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

class Benchmark:
    def __init__(self, proc_name, port, concurrency):
        self.proc_name = proc_name
        self.proc = []
        self.port = port
        self.concurrency = concurrency
        self.req = []
        self.server_cpu = []
        self.client_cpu = []
        self.find_worker_proc()
    #end of __init__()

    def find_worker_proc(self):
        pids = []
        ppids = []
        for p in psutil.process_iter():
            if p.name() == self.proc_name:
                self.proc.append(p)
        if len(self.proc) > 1:
            for i in range(len(self.proc)):
                for j in range(len(self.proc)):
                    if self.proc[i].pid == self.proc[j].ppid():
                        del self.proc[i]
                        break
                else:
                    continue
                break
    # end of find_worker_proc()

    def run_client_thread(self, c):
        cmd = "weighttp -k -n 1000 -c " + str(c) + " -t " + str(1 if c == 1 else 2) + \
              " http://127.0.0.1:" + self.port + "/hello"
        sys.stdout.write(cmd + ' >>>')
        sys.stdout.flush()
        popen = subprocess.Popen(cmd, shell=True, stdout=subprocess.PIPE)
        for line in popen.stdout:
            match = re.search('\d+ req/s', str(line))
            if match:
                self.req.append(int(match.group(0)[0:-len(' req/s')]))
                break
    # end of run_client_thread()
    
    def run(self):
        if len(self.proc) == 0:
            print('No process named ' + self.proc_name)
            return False
        
        print('*** run benchmark for ' + self.proc_name + ' ***')
        for c in self.concurrency:
            t = threading.Thread(target=self.run_client_thread, args=(c,))
            t.start()

            # find the weighttp process
            weighttp_proc = None
            while weighttp_proc is None:
                for p in psutil.process_iter():
                    if p.name() == 'weighttp':
                        weighttp_proc = p
                        break

            # recode the server/client CPU usage information
            server_cpu = []
            client_cpu = []
            while weighttp_proc.is_running():
                try:
                    percent = weighttp_proc.cpu_percent()
                except Exception as e:
                    break #the weighttp process no longer exists
                client_cpu.append(percent)
                for p in self.proc:
                    server_cpu.append(p.cpu_percent())
                time.sleep(0.02)

            if len(server_cpu) == 0 or len(client_cpu) == 0:
                sys.stdout.write('\nCPU usage information not collected.\n')
                sys.stdout.flush()
                sys.exit()
                
            self.server_cpu.append(round((sum(server_cpu) / len(server_cpu)) * len(self.proc), 1))
            self.client_cpu.append(round(sum(client_cpu) / len(client_cpu), 1))
        
            t.join()
            
            sys.stdout.write(' (req/s:' + str(self.req[len(self.req) - 1]) +
                             ', server %CPU:' + str(self.server_cpu[len(self.req) - 1]) +
                             ', client %CPU:' + str(self.client_cpu[len(self.req) - 1]) +
                             ')\n')
            sys.stdout.flush()

            #give the server time to handle pending works
            time.sleep(0.5)
        print('')
        return True
    # end of run()
# end of class Benchmark

concurrency = [1, 2, 5, 10, 20, 50, 100, 200, 500, 1000]

svx = Benchmark('httpserver', '8080', concurrency)
if not svx.run():
    sys.exit()
    
ngx = Benchmark('nginx', '80', concurrency)
if not ngx.run():
    sys.exit()

img_name  = "bm_httpsvr_" + str(len(ngx.proc)) + ".png"
img_title = "libsvx vs. Nginx (" + str(len(ngx.proc)) + \
            (" thread/worker)" if len(ngx.proc) == 1 else " threads/workers)")

svx_color = '#B82A2A'
ngx_color = '#2150A6'

table_row_labels = ('Concurrency',
                    'libsvx req/s',
                    'libsvx %CPU',
                    'weighttp %CPU',
                    'nginx req/s',
                    'nginx %CPU',
                    'weighttp %CPU')

table_data = [concurrency,
              svx.req, svx.server_cpu, svx.client_cpu,
              ngx.req, ngx.server_cpu, ngx.client_cpu]

plt.axes([0.18, 0.34, 0.79, 0.58])
plt.plot(range(10), svx.req, linewidth=2, marker='s', color=svx_color, label="libsvx")
plt.plot(range(10), ngx.req, linewidth=2, marker='o', color=ngx_color, label="nginx")
tbl = plt.table(cellText=table_data, rowLabels=table_row_labels, loc='bottom', bbox=[0,-0.53,1,0.5])
tbl.auto_set_font_size(False)
tbl.set_fontsize(8)
for i in range(-1,10):
    tbl._cells[(1, i)]._text.set_color(svx_color)
    tbl._cells[(4, i)]._text.set_color(ngx_color)
plt.legend(loc='upper left', fontsize=10)
plt.grid(True)
plt.xlim(-0.5, 9.5)
plt.xticks([])
plt.yticks(fontsize=10)
plt.ylabel('requests per second', fontsize=10)
plt.title(img_title, fontsize=15)
plt.gcf().set_size_inches(6.5, 5)
plt.savefig(img_name, dpi=100)
print("save result to : " + img_name)
print('')
