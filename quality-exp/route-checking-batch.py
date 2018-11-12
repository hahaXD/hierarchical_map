import subprocess, shlex
from multiprocessing import Pool
import sys

def run_eval (arg):
    network_file_prefix, psdd_binary, routes_filename = arg
    cmd = "python route-checking.py %s %s %s" % (network_file_prefix, psdd_binary, routes_filename)
    subprocess.call(shlex.split(cmd))


if __name__ == "__main__":
    pool_size = int(sys.argv[1])
    network_compilation_filename_prefix = sys.argv[2]
    psdd_binary = sys.argv[3]
    routes_filename = sys.argv[4]
    p = Pool (pool_size)
    args = []
    for network_id in range(1, 10):
        args.append(("%s_%s" % (network_compilation_filename_prefix, network_id), psdd_binary, routes_filename))
    p.map(run_eval, args)
