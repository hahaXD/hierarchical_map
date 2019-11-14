# hierarchical\_map

To run the code, first compile your code using cmake with the following command.
mkdir build;
cd build;
cmake ..;
make -j4;

Then, run
`./hmc_main  ../hm_benchmarks/sf_0_0.json ../script/compile_graph.py /tmp/ 4 test.sdd test.vtree`

This command will compile the map specified by the file ../hm\_benchmarks/sf\_0\_0.json. The compiled circuit is saved as test.sdd and test.vtree.

