import subprocess, shlex
from multiprocessing import Pool
import sys

def run_eval (arg):
    hmap_name, network_file_prefix, psdd_binary = arg
    cmd = "python quality-exp.py %s %s %s" % (hmap_name, network_file_prefix, psdd_binary)
    subprocess.call(shlex.split(cmd))


if __name__ == "__main__":
    pool_size = int(sys.argv[1])
    network_filename_prefix = sys.argv[2]
    network_compilation_filename_prefix = sys.argv[3]
    psdd_binary = sys.argv[4]
    p = Pool (pool_size)
    args = []
    for network_id in range(1, 10):
        args.append(("%s_%s.json"%(network_filename_prefix, network_id), "%s_%s" % (network_compilation_filename_prefix, network_id), psdd_binary))
    p.map(run_eval, args)
