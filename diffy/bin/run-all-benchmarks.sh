#!/bin/bash

if [ "$#" -ne 2 ]; then
  echo -e "\nUsage: $0 TOOLNAME={diffy, vajra, veriabs, viap} TIMEOUT_VALUE" >&2
  echo -e "\nRuns the tool on all the benchmarks in the current directory" >&2
  exit 1
fi

tool=""
success_res=""
fail_res=""
opt=""
if [ "$1" = "diffy" ]; then
	tool="diffy"
	success_res="DIFFY_VERIFICATION_SUCCESSFUL"
	fail_res="DIFFY_VERIFICATION_FAILED"
elif [ "$1" = "vajra" ]; then
	tool="vajra"
	success_res="VAJRA_VERIFICATION_SUCCESSFUL"
	fail_res="VAJRA_VERIFICATION_FAILED"
elif [ "$1" = "veriabs" ]; then
	tool="veriabs"
	success_res="VERIABS_VERIFICATION_SUCCESSFUL"
	fail_res="VERIABS_VERIFICATION_FAILED"
	opt="--property-file unreach-call.prp"
elif [ "$1" = "viap" ]; then
	tool="viap_tool.py"
	success_res="VIAP_STANDARD_OUTPUT_True"
	fail_res="VIAP_STANDARD_OUTPUT_False"
	opt="--spec=unreach-call.prp"
else
	echo "Incorrect option"
	exit 1
fi

# Timeout value must be greater than 0 
if [ $2 -le 0 ]; then
	echo "Timeout value must be greater than 0"
	exit 1
fi
timeout=$2
timeout_res="TIMEOUT"

results_dir=$1"_results"
rm -rf $results_dir 
mkdir $results_dir 


# Run the tool on all the C programs in the current directory 
rm -f temp.c
for file in *.c
do
	echo "RUNNING ON BENCHMARK $file"
	
	base=`basename $file .c`

	cat $file > temp.c

	time -p memlimit -t $timeout $tool $opt temp.c >& $results_dir/$base.log 2>&1

	if `grep -q -w $success_res $results_dir/$base.log`; then
		echo "VERIFICATION_SUCCESSFUL"
	elif `grep -q -w $fail_res $results_dir/$base.log`; then
		echo "VERIFICATION_FAILED"
	elif `grep -q $timeout_res $results_dir/$base.log`; then
		echo "TIMEOUT" 
	else
		echo "VERIFICATION_UNKNOWN" 
	fi

	rm -f temp.c
done
