import json

def average (l):
    return float(sum(l))/ len(l);

if __name__ == "__main__":
    import sys
    distance_prefix = sys.argv[1]
    dsn = []
    trip_length_distance = []
    hausdorff_distance = []
    for i in range(1, 10):
        filename = "%s_%s_distance_result.json" % (distance_prefix, i)
        with open (filename, "r") as fp:
            distance = json.load(fp)
            dsn.extend(distance["dsn"])
            trip_length_distance.extend(distance["trip_length_distance"])
            hausdorff_distance.extend(distance["hausdorff_distance"])
    print ("dsn %s, trip length distance %s, hausdorff distance %s" % (average(dsn), average(trip_length_distance), average(hausdorff_distance) ))

