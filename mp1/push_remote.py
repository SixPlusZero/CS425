#!/usr/bin/python
import os
import sys
import subprocess

for i in range(1, 11):
    server = "jialin2@fa17-cs425-g59-%02d.cs.illinois.edu" % (i)
    cmd = "chmod 777 /usr/local/mp1/run.sh"
    os.system("scp dgrep.cpp worker.cpp run.sh %s:/usr/local/mp1/" % (server))
    ret = subprocess.Popen(["ssh", server, cmd])
