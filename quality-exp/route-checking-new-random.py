import sys
import json
import subprocess,shlex
import simple_graph
import logging
import math
from multiprocessing import Pool

psdd_binary = sys.argv[1]

def get_statistics (output):
    ll = output.split("\n")
    for line in ll:
        if "Consistent Data" in line:
            c = int(line.split(":")[1])
        if "Inconsistent Data" in line:
            i = int(line.split(":")[1])
    return c, i

def run_hmap(num):
    cmd = "%s --sparse_data_filename /space/jason/jason-aaai-quality/random_128_%s_testing_routes_sparse.json.sparse /space/jason/jason-aaai-quality/random_map_%s.psdd /space/jason/jason-aaai-quality/random_map_%s.vtree" % (psdd_binary, num, num, num)
    output = subprocess.check_output(shlex.split(cmd))
    c, i = get_statistics(output)
    print ("test c %s i %s" % (c, i))
    cmd = "%s --sparse_data_filename /space/jason/jason-aaai-quality/random_8196_%s_training_routes_sparse.json.sparse /space/jason/jason-aaai-quality/random_map_%s.psdd /space/jason/jason-aaai-quality/random_map_%s.vtree" % (psdd_binary, num, num, num)
    output = subprocess.check_output(shlex.split(cmd))
    c, i = get_statistics(output)
    print ("train c %s i %s" % (c, i))
    

    

if __name__ == "__main__":
    p = Pool(10)
    p.map(run_hmap, range(0, 10))

