import json

def average (l):
    return float(sum(l))/ len(l);

if __name__ == "__main__":
    import sys
    distance_prefix = sys.argv[1]
    numbers = []
    for i in range(1, 10):
        filename = "%s_%s_number_of_valid_routes.txt" % (distance_prefix, i)
        with open (filename, "r") as fp:
            distance = json.load(fp)
            numbers.append(distance["total_valid_routes"])
    print ("average valid %s "% (average(numbers)))

