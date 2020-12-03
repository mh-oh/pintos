import os
import sys
from parse import *

test = sys.argv[1]
stat = sys.argv[2]

with open(test+".result") as fin_res, open(test+".rsc") as fin_rsc, open(stat, "a") as fout:
    success = True
    if "PASS" not in fin_res.read():
        fout.write("Running {} failed.\n".format(test))
        success = False
    frame, page = fin_rsc.read().split("\n\n")
    if "PASS" not in frame:
        fout.write("Memory leaks: struct frame\n")
        success = False
    if "PASS" not in page:
        fout.write("Memory leaks: struct page\n")
        success = False

    if not success:
        fout.write("\n");
