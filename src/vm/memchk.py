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

file = sys.argv[1]

f_malloc, f_free, f_remain = memchk(file,
                                    "##### [{_}] (frame_alloc) f={addr} is malloced. f->kpage={_}",
                                    "##### [{_}] (frame_free) f={addr} is freed.")

with open(file+"-memchk", "a") as out:
    out.write("f: malloc: total={}\n".format(len(f_malloc)))
    out.write("f: free  : total={}\n".format(len(f_free)))
    out.write("f: remain: total={}\n".format(len(f_remain)))

    for line in f_remain:
        out.write(line+"\n")

p_malloc, p_free, p_remain = memchk(file,
                                    "##### [{_}] (page_make_entry) p={addr} is malloced to load upage={_}. spt size is {_}",
                                    "##### [{_}] (page_hash_free) p={addr} is freed.")

with open(file+"-memchk", "a") as out:
    out.write("p: malloc: total={}\n".format(len(p_malloc)))
    out.write("p: free  : total={}\n".format(len(p_free)))
    out.write("p: remain: total={}\n".format(len(p_remain)))

    for line in f_remain:
        out.write(line+"\n")
