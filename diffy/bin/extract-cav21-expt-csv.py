#!/usr/bin/env python
import re
import sys
import getopt
import os.path

timeout = 60
try:
    opts, args = getopt.getopt(sys.argv[1:],"ht:",["timeout="])
except getopt.GetoptError:
    print('extract-csv.py -t <timout value>')
    sys.exit(2)

for opt, arg in opts:
    if opt == '-h':
        print('extract-csv.py -t <timout value>')
        sys.exit()
    elif opt in ("-t", "--timeout"):
        timeout = arg

#Prepare tool list
tools = ["diffy", "vajra", "veriabs", "viap"]

#Initialize dictionaries
table_data = {}
C1_data = {}
C2C3_data = {}
for tool in tools:
    C1_data[tool] = {}
    C2C3_data[tool] = {}
    table_data[tool] = {}
    for cat in ["C1", "C2", "C3"]:
        table_data[tool][cat] = {"S" : 0, "U" : 0, "TO" : 0}

#Read the category-wise benchmark names from files
c1name_list = [line.rstrip() for line in open('C1_list.txt')]
c2name_list = [line.rstrip() for line in open('C2_list.txt')]
c3name_list = [line.rstrip() for line in open('C3_list.txt')]

state = 0
for tool in tools:
    f = open(tool+"_results.log", "r")
    #lines = f.readlines()
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
            if str(benchmark) in c1name_list:
                cat = "C1"
                C1_data[tool][benchmark] = time
            elif str(benchmark) in c2name_list:
                cat = "C2"
                C2C3_data[tool][benchmark] = time
            elif str(benchmark) in c3name_list:
                cat = "C3"
                C2C3_data[tool][benchmark] = time
            else:
                cat = "ERROR"
            if result == "Success" or result == "CounterExample":
                table_data[tool][cat]["S"] += 1
            elif result == "Inconclusive":
                table_data[tool][cat]["U"] += 1
            elif result == "Time Out":
                table_data[tool][cat]["TO"] += 1
            state = 0
            benchmark = ""
            time = ""
            result = ""
            cat = ""
    f.close()

# Output files to store the extracted data 
c1outfile = 'C1_'+str(timeout)+'.csv'
c2c3outfile = 'C2C3_'+str(timeout)+'.csv'
alloutfile = 'bench_'+str(timeout)+'.csv'

# Open csv files
c1of = open(c1outfile, 'w')
c2c3of = open(c2c3outfile, 'w')
allof = open(alloutfile, 'w')

# Write the extracted results in csv files for graph generation
c1of.write("Benchmark Diffy Vajra VeriAbs VIAP\n")
c2c3of.write("Benchmark Diffy Vajra VeriAbs VIAP\n")
allof.write("Benchmark Diffy Vajra VeriAbs VIAP\n")

for b in c1name_list:
    diffyt = C1_data["diffy"][b]
    vajrat= C1_data["vajra"][b]
    veriabst = C1_data["veriabs"][b]
    viapt = C1_data["viap"][b]
    txt = "{} {} {} {} {}{}".format(b, diffyt, vajrat, veriabst, viapt,'\n')
    c1of.write(txt)
    allof.write(txt)

for b in c2name_list + c3name_list:
    diffyt = C2C3_data["diffy"][b]
    vajrat= C2C3_data["vajra"][b]
    veriabst = C2C3_data["veriabs"][b]
    viapt = C2C3_data["viap"][b]
    txt = "{} {} {} {} {}{}".format(b, diffyt, vajrat, veriabst, viapt,'\n')
    c2c3of.write(txt)
    allof.write(txt)

# Close the files
c1of.close()
c2c3of.close()
allof.close()


#Generate that table from the paper
tabledata = 'table_data.csv'
td = open(tabledata, 'w')
td.write("Program Category,#Benchmarks,Diffy,Diffy,Diffy,Vajra,Vajra,VeriAbs,VeriAbs,VIAP,VIAP,VIAP\n")
td.write(",,Successful Result,Inconclusive Result,Timeout,Successful Result,Inconclusive Result,Successful Result,Timeout,Successful Result,Inconclusive Result,Timeout\n")

c1size=len(c1name_list)
c2size=len(c2name_list)
c3size=len(c3name_list)
totsize = c1size + c2size + c3size

t1s=0
t1u=0
t1to=0
t2s=0
t2u=0
t3s=0
t3to=0
t4s=0
t4u=0
t4to=0

for cat in ["C1", "C2", "C3"]:
    if cat == "C1":
        csize = c1size
    elif cat == "C2":
        csize = c2size
    elif cat == "C3":
        csize = c3size

    c1s=table_data["diffy"][cat]["S"]
    t1s+=c1s
    c1u=table_data["diffy"][cat]["U"]
    t1u+=c1u
    c1to=table_data["diffy"][cat]["TO"]
    t1to+=c1to
    c2s=table_data["vajra"][cat]["S"]
    t2s+=c2s
    c2u=table_data["vajra"][cat]["U"]
    t2u+=c2u
    c3s=table_data["veriabs"][cat]["S"]
    t3s+=c3s
    c3to=table_data["veriabs"][cat]["TO"]
    t3to+=c3to
    c4s=table_data["viap"][cat]["S"]
    t4s+=c4s
    c4u=table_data["viap"][cat]["U"]
    t4u+=c4u
    c4to=table_data["viap"][cat]["TO"]
    t4to+=c4to
    cresult = "{},{},{},{},{},{},{},{},{},{},{},{}{}".format(cat,csize,c1s,c1u,c1to,c2s,c2u,c3s,c3to,c4s,c4u,c4to,'\n')
    td.write(cresult)

totresult = "{},{},{},{},{},{},{},{},{},{},{},{}{}".format("Total",totsize,t1s,t1u,t1to,t2s,t2u,t3s,t3to,t4s,t4u,t4to,'\n')
td.write(totresult)

td.close()
