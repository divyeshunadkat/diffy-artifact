FROM ubuntu:18.04

#LABEL tool="diffy"
#LABEL version="CAV2021"
#LABEL description="Artifacts for CAV 2021."
#LABEL maintainer="divyesh@cse.iitb.ac.in"

#Set a timezone and non-interactive terminal mode. Required to avoid user input during build. 
ARG DEBIAN_FRONTEND=noninteractive
ENV TZ=America/New_York

#Install dependencies for verification tools
RUN apt clean && apt update && apt install -qy wget unzip git g++ vim python3 make cmake flex bison libboost-system-dev libboost-filesystem-dev libboost-program-options-dev clang-6.0 clang-6.0-doc clang-6.0-examples clang-format-6.0 clang-tidy-6.0 clang-tools-6.0 libclang-6.0-dev libclang-common-6.0-dev libclang1-6.0 libllvm6.0 llvm-6.0 llvm-6.0-dev llvm-6.0-runtime python-clang-6.0 clang++-6.0 openjdk-8-jre libc6-dev-i386

#Install dependencies for creation of plots
RUN DEBIAN_FRONTEND="noninteractive" TZ="America/New_York" apt install -y texlive texlive-latex-extra dvipng python3-matplotlib 

#Remove the fetched packages to reduce the space taken by the container
#RUN apt -qy autoremove

#Fix for the bug in LLVM-6.0
RUN ln -s /usr/share/llvm-6.0/cmake/ /usr/lib/llvm-6.0/lib/cmake/clang ; \
    ln -s /usr/lib/llvm-6.0/bin/clang /usr/lib/llvm-6.0/bin/clang-6.0

#Fetch the tool used to create plots in the paper
RUN wget https://github.com/alexeyignatiev/mkplot/archive/refs/heads/master.zip

#Fetch the verification tool - VIAP
RUN wget https://gitlab.com/sosy-lab/sv-comp/archives-2019/raw/svcomp19/2019/viap.zip

#Fetch the verification tool - VeriAbs
RUN wget https://gitlab.com/sosy-lab/sv-comp/archives-2021/raw/svcomp21/2021/veriabs.zip

#Copy the verification tool - Diffy, source code, benchmarks and libraries
COPY diffy /diffy

#Symbolic link as required by pre-built tool binaries
RUN ln -s /diffy/bin/libz3.so.4.8.7.0 /diffy/bin/libz3.so.4.8

#Extract the verification tool - VeriAbs 
RUN unzip /veriabs.zip -d /diffy/bin

#Extract the verification tool - VIAP 
RUN unzip /viap.zip -d /diffy/bin

#Extract the library to generate plots
RUN unzip /master.zip -d /diffy/bin

#Remove the fetched packages to reduce the space taken by the container
#RUN rm -f /viap.zip /veriabs.zip /master.zip 

#Set the libaray path for the pre-built tool binaries
ENV LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/diffy/bin

#Add the path to binaries of all the tools 
ENV PATH=$PATH:/diffy/bin:/diffy/bin/mkplot-master/:/diffy/bin/viap/:/diffy/bin/VeriAbs/scripts

#Set up a volume for persistant storage that will be available across container runs.
#Data in this directory will be available on the host machine.
VOLUME ["/root/"]

#Set the working directory
WORKDIR /diffy

