#!/usr/bin/env python
import re
import sys
import getopt
import os.path

inputfile = ''
outputfile = ''
timeout = 0
try:
    opts, args = getopt.getopt(sys.argv[1:],"hi:o:t:",["ifile=","ofile=","timeout="])
except getopt.GetoptError:
    print('extract-csv.py -i <input log file> -o <output csv file> -t <timeout value>')
    sys.exit(2)

for opt, arg in opts:
    if opt == '-h':
        print('extract-csv.py -i <input log file> -o <output csv file> -t <timeout value>')
        sys.exit()
    elif opt in ("-i", "--ifile"):
        inputfile = arg
    elif opt in ("-o", "--ofile"):
        outputfile = arg
    elif opt in ("-t", "--timeout"):
        timeout = arg

with open(inputfile) as f:
    f = f.readlines()

state = 0
with open(outputfile, 'w') as file:
    file.write("Benchmark,Time,Result\n")
    for line in f:
        benchFind = re.search(r'RUNNING ON BENCHMARK\b',line)
        if benchFind is not None:
            benchmarkArr = benchFind.string.split();
            benchmark = benchmarkArr[3]
            if state == 0:
                state = 1
        userFind = re.search(r'user\b',line)
        if userFind is not None:
            timeArr = userFind.string.split();
            time = timeArr[1]
            if state == 1 :
                state = 2
        successFind = re.search('\AVERIFICATION_SUCCESSFUL',line)
        if successFind is not None:
            result = "Success"
            if state == 2:
                state = 3
        failFind = re.search('\AVERIFICATION_FAILED',line)
        if failFind is not None:
            result = "CounterExample"
            if state == 2:
                state = 3
        unknownFind= re.search('\AVERIFICATION_UNKNOWN',line)
        if unknownFind is not None:
            result = "Inconclusive"
            time = timeout 
            if state == 2:
                state = 3
        timeoutFind = re.search('\ATIMEOUT',line)
        if timeoutFind is not None:
            result = "Time Out" 
            time = timeout 
            if state == 2:
                state = 3
        if state == 3:
            file.write(benchmark+','+time+','+result+'\n')
            state = 0
            benchmark = ""
            time = ""
            result = ""
