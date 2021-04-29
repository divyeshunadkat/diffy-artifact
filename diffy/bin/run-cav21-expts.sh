#!/bin/bash

if [ "$#" -ne 2 ]; then
  echo -e "\nUsage: $0 {safe | unsafe} TIMEOUT_VALUE" >&2
  echo -e "\n$0 must be executed from the directory that contains cav21-bench" >&2
  echo -e "\nRun the experiments reported in our CAV 2021 paper in two parts - safe and unsafe" >&2
  exit 1
fi

wd=`pwd`
dir=""
if [ "$1" = "safe" ]; then
	dir="$wd/cav21-bench/safe"
elif [ "$1" = "unsafe" ]; then
	dir="$wd/cav21-bench/unsafe"
else
	echo "Incorrect option"
	exit 1
fi

# Exit if the benchmarks directory does not exist
if [ ! -d $dir ]; then
	echo "Directory $dir DOES NOT EXIST" 
	exit 1 
fi

# Timeout value must be greater than 0 
if [ $2 -le 0 ]; then
	echo "Timeout value must be greater than 0"
	exit 1
fi

timeout=$2

mkplotdir=/diffy/bin/mkplot-master

extract_py=/diffy/bin/extract-cav21-expt-csv.py

#Running experiments on the benchmarks in $dir
run-all-tools.sh $dir $timeout

cd $dir

echo -e "\nExtracting data from log files for generating the plots"
$extract_py -t $timeout

mkdir -p plots

mv table_data.csv "plots/${1}_table_data_${timeout}.csv"
mv C1_${timeout}.csv "plots/${1}_C1_${timeout}.csv"
mv C2C3_${timeout}.csv "plots/${1}_C2C3_${timeout}.csv"
mv bench_${timeout}.csv "plots/${1}_bench_${timeout}.csv"

cp plots/*.csv $mkplotdir

cd -

echo -e "\nGenerating the plots from the extracted data"

cd $mkplotdir

if [ "$1" = "safe" ]; then
	python3 $mkplotdir/mkplot.py -l -t $timeout -b pdf --xmax 160 --save-to "$dir/plots/${1}_bench_${timeout}.pdf" ${1}_bench_${timeout}.csv
	python3 $mkplotdir/mkplot.py -l -t $timeout -b pdf --xmax 110 --save-to "$dir/plots/${1}_C1_${timeout}.pdf" ${1}_C1_${timeout}.csv
	python3 $mkplotdir/mkplot.py -l -t $timeout -b pdf --xmax 50 --save-to "$dir/plots/${1}_C2C3_${timeout}.pdf" ${1}_C2C3_${timeout}.csv
elif [ "$1" = "unsafe" ]; then
	python3 $mkplotdir/mkplot.py -l -t $timeout -b pdf --xmax 150 --save-to "$dir/plots/${1}_bench_${timeout}.pdf" ${1}_bench_${timeout}.csv
	python3 $mkplotdir/mkplot.py -l -t $timeout -b pdf --xmax 100 --save-to "$dir/plots/${1}_C1_${timeout}.pdf" ${1}_C1_${timeout}.csv
	python3 $mkplotdir/mkplot.py -l -t $timeout -b pdf --xmax 50 --save-to "$dir/plots/${1}_C2C3_${timeout}.pdf" ${1}_C2C3_${timeout}.csv
else
	echo "Incorrect option"
	exit 1
fi

rm -f *.csv

cd -

echo -e "\nGenerated plots and tables are saved at $dir/plots"

# Copying to permanent storage
mkdir -p $HOME/${1}_plots_${timeout}
cp $dir/plots/* $HOME/${1}_plots_${timeout}

echo -e "\nCopied the generated plots and tables to $HOME/${1}_plots_${timeout}"

echo -e "\nThe plots and table can be viewed using pdf and csv readers on the host machine in the \"diffy-artifact/storage\" directory"
