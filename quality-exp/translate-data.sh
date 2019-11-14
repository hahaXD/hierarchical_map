#!/bin/bash
for i in {0..9}
do
    python translate-data.py ~/jason-aaai-quality/128_${i}_testing_routes.json ~/jason-aaai-quality/heuristic_map_${i}_variable_map.json ~/jason-aaai-quality/heuristic_128_${i}_testing_routes_sparse.json
    python translate-data.py ~/jason-aaai-quality/128_${i}_testing_routes.json ~/jason-aaai-quality/random_map_${i}_variable_map.json ~/jason-aaai-quality/random_128_${i}_testing_routes_sparse.json
    python translate-data.py ~/jason-aaai-quality/8196_${i}_training_routes.json ~/jason-aaai-quality/heuristic_map_${i}_variable_map.json ~/jason-aaai-quality/heuristic_8196_${i}_training_routes_sparse.json
    python translate-data.py ~/jason-aaai-quality/8196_${i}_training_routes.json ~/jason-aaai-quality/random_map_${i}_variable_map.json ~/jason-aaai-quality/random_8196_${i}_training_routes_sparse.json
done
