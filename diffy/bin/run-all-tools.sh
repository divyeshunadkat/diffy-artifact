#!/bin/bash

if [ "$#" -ne 2 ]; then
  echo -e "\nUsage: $0 PATH_TO_DIR_WITH_BENCHMARKS TIMEOUT_VALUE" >&2
  echo -e "\nRuns the tools diffy vajra veriabs and viap on all the benchmarks in the input directory" >&2
  exit 1
fi

dir="$1"
# Exit if the benchmarks directory does not exist
if [ ! -d $dir ]; then
	echo "Directory $dir DOES NOT exist" 
	exit 1 
fi

timeout=$2
if [ $timeout -le 0 ]; then
	echo "Timeout value must be greater than 0"
	exit 1
fi

cd $dir

echo -e "\nRunning experiments on the benchmarks in $dir with TIMEOUT=$timeout"
for tool in  "diffy" "vajra" "veriabs" "viap"
do
	echo -e "\nExecuting $tool on the benchmarks in $dir"
	echo -e -n "\nStart time : "
	date
	run-all-benchmarks.sh $tool $timeout >& ${tool}_results.log
	echo -e "\nFinished execution of $tool"
	echo -e -n "\nEnd time : "
	date
done

cd -
