import os
import sys
from parse import *

file = sys.argv[1]
with open(file) as f, open(file+"-without-comments", "w") as out:
    for line in f.readlines():
        if not line.startswith("#####"):
            out.write(line)
"""

fmt_malloc = "##### [{_}] (frame_alloc) f={addr} is malloced. f->kpage={_}"
fmt_free = "##### [{_}] (frame_free) f={addr} is freed."

malloc = []
free = []

with open("./outputs/a") as f:
    for line in f.readlines():
        p = parse(fmt_malloc, line)
        if p is not None:
            malloc.append(p.named["addr"])

print(len(malloc), len(set(malloc)))

with open("./outputs/a") as f:
    for line in f.readlines():
        p = parse(fmt_free, line)
        if p is not None:
            free.append(p.named["addr"])

print(len(free), len(set(free)))

remain = set(malloc) - set(free)
print(len(remain))

for line in remain:
    print(line)
"""