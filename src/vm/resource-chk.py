import os
import sys
from parse import *

def memchk(file, fmt_malloc, fmt_free):
    malloc = []
    free = []
    with open(file) as f:
        for line in f.readlines():
            p = parse(fmt_malloc, line)
            if p is not None:
                malloc.append(p.named["addr"])
    with open(file) as f:
        for line in f.readlines():
            p = parse(fmt_free, line)
            if p is not None:
                free.append(p.named["addr"])
    return malloc, free, set(malloc) - set(free)

file_in = sys.argv[1]
file_out = os.path.splitext(file_in)[0] + ".rsc"



f_malloc, f_free, f_remain = memchk(file_in,
                                    "##### [{_}] (frame_alloc) f={addr} is malloced and locked. f->kpage={_}",
                                    "##### [{_}] (frame_free) f={addr} is freed.")

p_malloc, p_free, p_remain = memchk(file_in,
                                    "##### [{_}] (page_make_entry) p={addr} is malloced to load upage={_}. spt size is {_}",
                                    "##### [{_}] (page_hash_free) p={addr} is freed.")

f_success = "PASS" if len(f_remain) == 0 else "FAIL"
p_success = "PASS" if len(p_remain) == 0 else "FAIL"

with open(file_out, "w") as out:
    out.write("{}\nstruct frame\n".format(f_success))
    out.write("    malloc: total={}\n".format(len(f_malloc)))
    out.write("    free  : total={}\n".format(len(f_free)))
    out.write("    remain: total={}\n".format(len(f_remain)))

    for line in f_remain:
        out.write(line+"\n")

    out.write("\n")

    out.write("{}\nstruct page\n".format(p_success))
    out.write("    malloc: total={}\n".format(len(p_malloc)))
    out.write("    free  : total={}\n".format(len(p_free)))
    out.write("    remain: total={}\n".format(len(p_remain)))

    for line in f_remain:
        out.write(line+"\n")

print("struct frame:{}, struct page:{}\n".format(f_success.lower(), p_success.lower()))