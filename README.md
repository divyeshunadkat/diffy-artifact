# Diffy
=======

Diffy is the prototype tool that implements the verification technique
reported in the paper "Diffy: Inductive Reasoning of Array Program
using Difference Invariants". Supratik Chakraborty, Ashutosh Gupta,
Divyesh Unadkat. In Proc of CAV 2021.

The artifact associated with the paper can be obtained from the
[Online Archives](#online-archives).

## Executing Diffy
==================

Diffy accepts as input a C program in the format similar to SV-COMP.
It verifies if the given post-condition of the program holds or not.
Note that arrays in the program need to be stack allocated and not
malloc'd.

```
	diffy [options] `filename`
```

Example:

```
	diffy cav21-bench/safe/nested-as1.c
```

Help:

```
	diffy --help
```

Output
------

*DIFFY_VERIFICATION_SUCCESSFUL* - when the given post-condition is verified.

*DIFFY_VERIFICATION_FAILED* - when the given post-condition is violated.

*DIFFY_VERIFICATION_UNKNOWN* - when the result cannot be determined.


## Kick-the-Tyres Instructions
==============================

The recommended way to install this artifact is by compiling the
Docker image based on Ubuntu 18.04. Instructions to install Docker can
be found [here](https://docs.docker.com/install/). Subsequently, run
the steps (1 through 6) listed below. Use a bash terminal to execute
all commands on a linux system.

Step 4 builds the docker image and fetches packages and tools
(approximately 1GB) over the internet. It may take more than an hour
to build the entire docker image, depending on the network speed and
machine configuration. Note that the "." at the end of the command in
step 4 is important and needs to be typed in.

The built image includes all the tools and the required dependencies
to reproduce the experiments reported in the paper and works without
an active internet connection. The built image will occupy about 4GB
of disk space. Step 5 runs the docker image in a container and opens a
terminal into the container.

```
1. Download the artifact file diffy-artifact.tar.gz
2. tar -xvf diffy-artifact.tar.gz
3. cd diffy-artifact/
4. sudo docker build -t diffy .
5. sudo docker run --mount type=bind,source="$(pwd)/storage",target=/root --name diffy-artifact -it diffy:latest
6. diffy cav21-bench/safe/nested-as1.c
```

If the output of step 6 is *DIFFY_VERIFICATION_SUCCESSFUL* then
`diffy` is functional and we are ready to run all the experiments.

At this point one may choose to exit the container (using the `exit`
command) or continue running all the experiments as described in the
next section.

Note: If docker is used on Windows OS then the word `pwd` needs to be
surrounded with braces instead of parentheses as
`--mount type=bind,source="${pwd}/storage",target=/root` for the
command to function correctly.

Instead of steps 1 and 2, the repository can be cloned from
[GitHub](https://github.com/divyeshunadkat/diffy-artifact).


## Instructions for Running All Experiments
===========================================

If the container was exited, execute the first two commands below to
restart and run the same container. Otherwise, skip the first two
instructions below.

```
1. sudo docker start diffy-artifact
2. sudo docker attach diffy-artifact
3. run-cav21-expts.sh safe 60
4. run-cav21-expts.sh unsafe 60
```

Steps 3 and 4 above execute the tools `diffy` [[1]](#1), `vajra`
[[2]](#2), `veriabs` [[3]](#3) and `viap` [[4]](#4) on the benchmarks
in `cav21-bench/safe` and `cav21-bench/unsafe` directories. `vajra`,
`veriabs` and `viap` are the tools against which `diffy` is compared
in the paper. Programs that satisfy the given post-condition are
categorized as "Safe" as in Table 1 of the paper, and **157** such
benchmarks are included in the `cav21-bench/safe` directory. Programs
that violate the given post-condition are categorized as "Unsafe" as
in Table 1 of the paper, and **146** such benchmarks are included in
the `cav21-bench/unsafe` directory. A timeout of 60 seconds is set for
each tool to verify a benchmark. We run `diffy`, `vajra`, `veriabs`
and `viap` sequentially on all benchmarks in a directory. Collecting
the verification results and generating the csv files and plots takes
a small amount of additional time. Running step 3 to completion takes
approximately 4 hours, and running step 4 to completion takes
approximately 3 hours on a machine with Intel i7-6500U CPU, 16GB RAM,
running at 2.5 GHz, and Ubuntu 18.04.5 LTS operating system. The
execution time may vary based on machine configuration of the host,
docker performance overhead, and CPU utilization by other applications
running on the host machine.

Step 3 above creates the sub-directory `cav21-bench/safe/plots` that
stores the plots as well as the summary of experimental results in the
following files:

* safe_table_data_60.csv - Summary of experimental results on safe benchmarks  in Table 1 (first four lines) of the paper
* safe_bench_60.pdf - Plot in figure 3(a) of the paper
* safe_bench_60.csv - Data for generating the plot in figure 3(a) of the paper
* safe_C1_60.pdf - Plot in figure 4(a) of the paper
* safe_C1_60.csv - Data for generating the plot in figure 4(a) of the paper
* safe_C2C3_60.pdf - Plot in figure 5(a) of the paper
* safe_C2C3_60.csv - Data for generating the plot in figure 5(a) of the paper

Step 4 creates the sub-directory `cav21-bench/unsafe/plots` that
stores the plots as well as the summary of experimental results in the
following files:

* unsafe_table_data_60.csv - Summary of experimental results on unsafe benchmarks in Table 1 (last four lines) of the paper
* unsafe_bench_60.pdf - Plot in figure 3(b) of the paper
* unsafe_bench_60.csv - Data for generating the plot in figure 3(b) of the paper
* unsafe_C1_60.pdf - Plot in figure 4(b) of the paper
* unsafe_C1_60.csv - Data for generating the plot in figure 4(b) of the paper
* unsafe_C2C3_60.pdf - Plot in figure 5(b) of the paper
* unsafe_C2C3_60.csv - Data for generating the plot in figure 5(b) of the paper

The directories `cav21-bench/safe/plots` and
`cav21-bench/unsafe/plots` are copied to the host machine at
`diffy-artifact/storage/safe_plots_60` and
`diffy-artifact/storage/unsafe_plots_60` respectively. The pdf and csv
readers on the host machine can now be used to view the plots and
summary of experimental results from the csv files.

To generate the plots in figure 6(a) and figure 6(b) of the paper,
execute the steps shown below with a timeout of 100 seconds. Due to
the timeout value of 100 seconds, these steps take more time to
execute than they required when the timeout was of 60 seconds.

```
1. run-cav21-expts.sh safe 100
2. run-cav21-expts.sh unsafe 100
```

## Experiments on Additional Benchmarks
=======================================

Since the submission of our paper to CAV, we have been able to run our
tool, `diffy`, on additional benchmarks.  These further demonstrate
the advantages of `diffy` over the tools `vajra`, `veriabs` and
`viap`. While data from these additional experiments do not appear in
the paper (because these experiments were not completed before the
submission of the paper), we are including these as part of the
artifact evaluation, in case the artifact reviewers want to check out
the performance of `diffy` on these additional benchmarks. These are
included in `additional-bench` directory.

Tools `diffy`, `vajra`, `veriabs`, and `viap` can be tried on all the
benchmarks in `additional-bench/safe` and `additional-bench/unsafe`
directories with a timeout of 60 seconds using the following commands:

```
	run-all-tools.sh additional-bench/safe 60
	run-all-tools.sh additional-bench/unsafe 60
```

To execute only `diffy` on the benchmarks in `additional-bench/safe`
directory with a timeout of 60 seconds, run the following commands:

```
        cd additional-bench/safe
        run-all-benchmarks.sh diffy 60
        cd -
```

`diffy` can be directly executed on individual benchmarks as follows:

```
	diffy additional-bench/safe/nested-ssn2.c
```


## Claims Supported by the Artifact
===================================

* Experiments submitted as part of this artifact generate all plots &
  the table with summary of results as described in the experiments
  section of the paper. Run times may slightly differ depending on
  machine configuration, docker performance overhead, and CPU
  utilization by other applications running on the host machine.

* `diffy` successfully verifies additional challenging benchmarks
  beyond those reported in the paper (given in the `additional-bench`
  directory).


## Clean Up
===========

Following commands can be used to remove the docker container and the
compiled image. Be careful as this will permanently delete the docker
image for this artifact as well as the generated plots and data in the
shared folder `diffy-artifact/storage`.

```
	sudo docker container rm diffy-artifact
	sudo docker image rm diffy
	sudo docker image prune
	rm -rf storage/*
```


## Build and Run the Docker Image
=================================

Docker version 20.10.2 was used to test the image creation and its
execution in a container.

Building the Docker Image
-------------------------

In the folder that contains the Dockerfile, run the following command:

```
	sudo docker build -t diffy .
```


This command fetches the required packages/tools and downloads
approximately 1GB of data from the internet. Depending on the network
speed and machine configuration, the command may take more than an
hour to run till completion. After the image is built, it will include
all required tools and required dependencies. The built container will
occupy about 4GB of disk space. Please ensure that there is enough
disk-space for its storage and execution.

Running the Docker Image
------------------------

```
	sudo docker run --mount type=bind,source="$(pwd)/storage",target=/root --name diffy-artifact -it diffy:latest
```

`--mount type=bind,source="$(pwd)/storage",target=/root` specifies a
persistent volume (i.e. the folder `storage` in the `diffy-artifact`
directory) so that the generated plots (pdf,png files) can be saved in
the `diffy-artifact/storage` folder and can be viewed using the pdf
and csv readers from the host machine. Any data that needs to be
transferred to the host machine or needs to be persistent across
containers must be copied/created in this folder.

Note: If docker is used on Windows OS then the word `pwd` needs to be
surrounded with braces instead of parentheses as
`--mount type=bind,source="${pwd}/storage",target=/root` for the
command to function correctly.

Running this command again will start a fresh container which does not
retain any data from the previous runs. To create a fresh container
using this command, ensure to use a different name in `--name`
parameter, else an error will be encountered. To retain the
data/computation and generated files from a container run, it needs to
be re-started using the commands described in the next section.

Re-starting the Docker Container:
---------------------------------

To restart the container that you were working with, such that the
files and data generated from that run are retained, use the following
commands:

```
	sudo docker start diffy-artifact

	sudo docker attach diffy-artifact
```


Other Useful Docker Commands:
-----------------------------

View the docker containers running on the system:

```
	sudo docker ps -a
```

Remove the docker container named `diffy-artifact` running on the system:

```
	sudo docker container rm diffy-artifact
```

Remove any dangling docker images from the system to free-up space:

```
	sudo docker image prune
```

View docker images installed on the system:

```
	sudo docker images
```

Remove the installed docker image named `diffy` from the system:

```
	sudo docker image rm diffy
```

Create a docker image with all packages pre-installed:

```
	sudo docker save diffy:latest | gzip > diffy_latest.tar.gz
```

## Using Pre-compiled Diffy
===========================

The `diffy/bin` directory contains a pre-compiled `diffy` executable
file as well as the libraries that it depends on. It requires LLVM
version 6.0, BOOST version 1.68 and Z3 version 4.8.7 to be installed
on the system where it will be used.  The following libraries are
bundled with the binary:

BOOST version 1.68
------------------
* libboost_filesystem.so.1.68.0
* libboost_program_options.so.1.68.0
* libboost_system.so.1.68.0

Z3 version 4.8.7
----------------
* libz3.so.4.8.7.0

The source code and compilation instructions of the above library
files can be found at the links below:

Boost version 1.68 can be found
[here](https://www.boost.org/users/history/version_1_68_0.html)

Z3 version 4.8.7 can be found
[here](https://github.com/Z3Prover/z3/releases/tag/z3-4.8.7)

Installing the Pre-compiled Files
---------------------------------

Please follow the below instructions to use the pre-compiled binaries
on a linux system:

```
	cd diffy/bin

	ln -s libz3.so.4.8.7.0 libz3.so.4.8

	export LD_LIBRARY_PATH=.:$LD_LIBRARY_PATH

	export PATH=.:$PATH
```

Note: Install LLVM 6.0 using the `apt install` command below if it is
not already installed on the system.


## Building from Source Code
============================

Installing Dependencies
-----------------------

```
	sudo apt install git g++-7 cmake libboost-program-options-dev libboost-filesystem-dev libboost-system-dev flex bison

	sudo update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-7 100 --slave /usr/bin/g++ g++ /usr/bin/g++-7 --slave /usr/bin/gcov gcov /usr/bin/gcov-7
```

Some package names may not be exact and will depend on the OS in use.

Install LLVM 6.0 and its patch
------------------------------

Run the following command to install LLVM 6.0 on the system:

```
	sudo apt install clang-6.0 clang-format-6.0 clang-tidy-6.0 libclang-6.0-dev
```

Run the following commands to patch the installation of LLVM 6.0 on the system:

```
	sudo ln -s /usr/share/llvm-6.0/cmake/ /usr/lib/llvm-6.0/lib/cmake/clang
	sudo ln -s /usr/lib/llvm-6.0/bin/clang /usr/lib/llvm-6.0/bin/clang-6.0
```

The second command in the patch may or may not be required depending
on the OS in use. The patch for LLVM 6.0 is as suggested in this
[webpage](https://github.com/flang-compiler/f18/issues/190).

Compilation
-----------

To compile, navigate to the diffy directory containing the Makefile
and execute the following command:

```
	make
```

This will result in the executable binary file `diffy` generated in
the folder.

Note: Compilation requires an active internet connection for the first
run as it fetches Z3 sources. This step may take an hour or more
depending on the machine configuration as it compiles `diffy` along
with the Z3 source code.

Debug Mode Compilation
----------------------

Navigate to the diffy directory containing the Makefile and execute
the following command:

```
	make debug
```

This will result in the executable binary file `diffy` generated in
the folder. The executable file takes nearly 2GB of disk-space.

Note: Requires an active internet connection for the first time as it
fetches LLVM 6.0 sources before compiling them in debug mode. For the
first run, this step may take more than 24 hours depending on the
machine configuration as it compiles `diffy`, `z3` and `llvm` in debug
mode.

## Project Structure and Content
================================

```
	|- diffy/
	|	* Top level folder with source code, benchmarks, binaries and logs.
	|- diffy/bin/
	|	* Contains pre-compiled binaries of various tools, libraries and scripts.
	|	* run-cav21-expts.sh - script to reproduce CAV21 experiments and generate plots.
	|	* run-all-tools.sh - script for running all tools on all benchmarks in a directory.
	|	* run-all-benchmarks.sh - script for running a tool on all benchmarks in a directory.
	|	* extract-cav21-expt-csv.py - script to extract execution time and other data in csv format from the logs.
	|- diffy/Makefile
	|	* Make file for compiling the tool.
	|- diffy/src
	|	* Directory that contains the entire source code of the tool.
	|	* CMakeLists.txt - specification file for the cmake compilation utility.
	|	* main.cpp - file with the main function where the execution begins.
	|- diffy/src/aggregation
	|	* Compiler passes for code transformation based on LLVM IR.
	|	* array_ssa.cpp - generate a new ssa name for arrays in each loop of the program.
	|	* simplify_nl.cpp - simplify the input program for subsequent analysis.
	|	* create_pnm1.cpp - generation of $P_{N-1}$ from the input program.
	|	* create_qnm1.cpp - generation of $Q_{N-1}$ from the input program.
	|	* extract_peels.cpp - generation of $peel(P_N)$ from the input program.
	|	* diff_preds.cpp - incorporate the computed difference invariants in the analysis.
	|- diffy/src/bmc
	|	* Translation of LLVM IR to SMT formulas and inductive verification APIs.
	|	* array_model.cpp - modeling the arrays in llvm ir for creating the bmc formula.
	|	* glb_model.cpp - modeling global variables in llvm ir for creating the bmc formula.
	|	* witness.cpp - generation of the witness when the property can be violated.
	|	* bmc_ds.cpp - data structure classes for storing information while translating to bmc. 
	|	* bmc_pass.cpp - base class for translating the input to a bmc formula.
	|	* bmc_fun_pass.cpp - sub-class of bmc_pass, runs initialization for each function.
	|	* bmc_loop_pass.cpp - sub-class of bmc_pass, runs initialization for each loop a function.
	|	* bmc.cpp - class that defines APIs to execute the bmc pass and access varied information. 
	|	* bmc_llvm_aggr.cpp - implements the inductive verification using smt solving, it checks
	|	* the base-case, inductive case and computes Dijkstra's weakest pre-condition.
	|- diffy/src/irtoz3expr
	|	* Create smt expressions corresponding to LLVM IR expressions and store them for later use.
	|	* irtoz3expr.cpp - defines APIs for converting ir to smt before bmc translation is invoked.
	|- diffy/src/utils
	|	* Utilities for manipulating the LLVM IR, Z3 expressions and parsing parameter options.
	|	* llvmUtils.cpp - APIs and classes for analyzing and manipulating the llvm ir of the input program.
	|	* z3Utils.cpp - APIs for manipulating the z3 expressions created while analyzing the input program.
	|	* options.cpp - parse the parameters passed to the tool and store them appropriately.
	|- diffy/cav21-bench/
	|	* Safe & unsafe benchmarks used during the experimentation for CAV 2021.
	|- diffy/additional-bench/
	|	* Additional benchmark programs for experimentation beyond those in the paper.
	|- diffy/logs-cav21-expts/
	|	* Log files generated during the experiments for CAV 2021.
	|	* File and directory names are indicative of the content.
	|- storage/
	|	* Shared directory to copy content between host machine and docker container.
	|- DIFFY_LICENSE
	|	* License file for the tool.
	|- Dockerfile
	|	* Docker script for building the container.
	|- README.md
	|	* Readme file describing the contents of the artifact and the evaluation procedure.
```

## Online Archives
==================

The source code, benchmarks and compiled executable files are
permanently available in the following archives:

[figshare](https://doi.org/10.6084/m9.figshare.14509467)

[GitHub](https://github.com/divyeshunadkat/diffy-artifact)


## LICENSE
==========

This work is licensed under the MIT License - see the
[LICENSE](diffy/DIFFY_LICENSE) file for details.


## References
===========

<a id="1">[1]</a>
Supratik Chakraborty, Ashutosh Gupta, and Divyesh Unadkat.
"Diffy: Inductive Reasoning of Array Programs using Difference Invariants."
International Conference on Computer-Aided Verification (CAV). 2021.

<a id="2">[2]</a>
Supratik Chakraborty, Ashutosh Gupta, and Divyesh Unadkat.
"Verifying Array Manipulating Programs with Full-Program Induction."
International Conference on Tools and Algorithms for the
Construction and Analysis of Systems (TACAS). 2020.

<a id="3">[3]</a>
Mohammad Afzal, et al.
"Veriabs: Verification by abstraction and test generation
(competition contribution)."
International Conference on Tools and Algorithms for the
Construction and Analysis of Systems (TACAS). 2020.

<a id="4">[4]</a>
Pritom Rajkhowa, and Fangzhen Lin.
"VIAP 1.1."
International Conference on Tools and Algorithms for the
Construction and Analysis of Systems(TACAS). 2019.

## Contact
===========

[Write to us](divyesh@cse.iitb.ac.in) for any issues, queries,
suggestions and feedback. We are continuously working to make our
algorithms and implementation better, faster and more robust.


## A Thank You Note
===================

We thank all the reviewers for taking time to evaluate our
artifact. We appreciate your effort and feedback.

