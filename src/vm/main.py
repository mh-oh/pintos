import os

for file in os.listdir("./outputs"):
    file = os.path.join("./outputs", file)
    with open(file) as f, open(file+"-without-comments", "w") as out:
        for line in f.readlines():
            if not line.startswith("#####"):
                out.write(line)
