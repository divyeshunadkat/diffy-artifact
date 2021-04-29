#include<string>
#include "options.h"

namespace po = boost::program_options;

options::options() {
  if(t == TILER) {
    toolName = "tiler";
    toolNameCaps = "TILER";
    toolDescription = "\n\t" + toolNameCaps + " - Verifying Array Manipulating Programs by Tiling";
  } else if (t == VAJRA) {
    toolName = "vajra";
    toolNameCaps = "VAJRA";
    toolDescription = "\n\t" + toolNameCaps + " - Verifying Array Manipulating Programs with Full-program Induction";
  }  else if (t == DIFFY) {
    toolName = "diffy";
    toolNameCaps = "DIFFY";
    toolDescription = "\n\t" + toolNameCaps + " - Inductive Reasoning of Array Programs using Difference Invariants";
  }  else if (t == ALL) {
    toolName = "tvd"; // TILER VAJRA DIFFY
    toolNameCaps = "TVD";
    toolDescription = "\n\t" + toolName + " - Cluster of Tools to Verify Array Programs";
  } else {
    std::cout << "\n\tERROR: Unrecognized tool build!";
    exit(1);
  }
}

void options::show_help(po::options_description& desc) {
  std::cout << toolDescription;
  std::cout << "\n\tSupratik Chakraborty, Ashutosh Gupta, Divyesh Unadkat";
  std::cout << "\n\tIndian Institute of Technology, Computer Science Department";
  std::cout << "\n\t{supratik,akg,divyesh}@cse.iitb.ac.in\n\n";
  std::cout << "Usage:\t\t\t\t    Purpose:\n\n";
  std::cout << toolName << " [-h] [--help]\t\t    Show help \n";
  std::cout << toolName << " [Options] file.c ...\n";
  std::cout << desc << "\n";
}

bool options::get_explcons() { return EXPLICIT_CONS; }

void options::get_description_cmd(po::options_description& config,
                                  po::options_description& cmdline,
                                  po::options_description& hlp,
                                  po::positional_options_description& pd){

  po::options_description generic("Generic Options");
  po::options_description core("Core Options");
  po::options_description tiler("Tiler Options");
  po::options_description exp("Experimental Options");
  po::options_description vajra("Vajra Options");
  po::options_description vv("Diffy Options");
  po::options_description bmc("BMC Options");
  po::options_description hidden("Hidden Options");
  po::options_description license("LICENSE");

  core.add_options()
    ("function,f", po::value(&funcName)->default_value("main"), "Set main function")
    ("output-dir,o", po::value(&outDirPath)->default_value("/tmp/"), "Set output directory")
    ("dump-cfg,d", po::bool_switch(&dump_cfg), "Dump the llvm control flow graph")
    ("config,c", po::value<std::string>(), "Set config file")
    ;
  tiler.add_options()
    ("mode,m", po::value<int>(&mode), "Set mode")
    ("loop,l", po::value<int>(&loopNum), "Set loop")
    ("total-loops,t", po::value<int>(&totalLoops), "Set total number of loops")
    ;
  exp.add_options()
    ("loop-peeling,a", po::bool_switch(&loop_aggr), "Run verification with loop peeling")
    ;
  vajra.add_options()
    ("full-program-induction,p", po::bool_switch(&fpi), "Run full-program induction algorithm")
    ;
  vv.add_options()
    ("difference-invs,x", po::bool_switch(&diff_preds)->default_value(true), "Verification using difference invariants")
    ;
  bmc.add_options()
    ("unwind,u", po::value<int>(&loop_unroll_count)->default_value(1), "Set loop unroll count")
    ("fix-point-bound,b", po::value<int>(&fixpointBound)->default_value(50), "Bound on pre-condition inference cycle")
    //    ("bounds-check", po::bool_switch(&includeOutOfBoundSpecs), "Enable array-index-out-of-bounds check")
    //    ("overflow-check", po::bool_switch(&includeOverUnderflowSpecs), "Enable over/underflow check")
    ;
  license.add_options()
    ("diffy-license", "Print Diffy License")
    ("boost-license", "Print Boost License")
    ("z3-license", "Print Z3 License")
    ;
  hidden.add_options()
    ("input,i", po::value(&filePath), "Set source files")
    ;
  generic.add_options()
    ("version", "Print version string")
    ("verbose,v", po::value<int>(&verbosity)->default_value(0), "Set verbosity level")
    ("help,h", "Print help")
    ;
  pd.add("input", -1);

  po::options_description toolopt;
  if(t == TILER) {
    toolopt.add(core).add(tiler).add(bmc);
  } else if(t == VAJRA) {
    toolopt.add(core).add(vajra).add(bmc);
  } else if(t == DIFFY) {
    toolopt.add(core).add(vv).add(bmc);
  } else if(t == ALL) {
    toolopt.add(core).add(tiler).add(vajra).add(vv).add(bmc);
  } else {
    std::cout << "\n\tERROR: Unrecognized tool build!";
    exit(1);
  }
  config.add(toolopt).add(hidden).add(license);
  cmdline.add(config).add(generic);
  hlp.add(toolopt).add(generic).add(license);
}

void options::interpret_options(po::variables_map& vm) {
  if (vm.count("input")) {
    boost::filesystem::path cf( filePath);
    fileName = cf.filename().string();
  }
  if (vm.count("unwind")) {
    unwind = true;
  }
  if (mode > 0 && mode <= 3) {
    // Modes for Tiling, Only Print Dot files and BMC
    if(totalLoops <= 0)
      tiler_error("Command-line Options", "Total Loops cannot be less equal to zero");
    if(loopNum <= 0)
      tiler_error("Command-line Options", "Selected Loop cannot be less equal to zero");
  } else if( fpi && !loop_aggr && !diff_preds) {
    mode = 4;
  }
  else if( loop_aggr && !fpi && !diff_preds ) { // Experimental
    mode = 5;
  }
  else if( diff_preds && !fpi && !loop_aggr ) {
    mode = 6;
  } else {
    tiler_error("Command-line Options", "Unrecognized option passed");
  }
  origFuncName = funcName;
}

void options::parse_config(boost::filesystem::path filename) {
  boost::filesystem::ifstream cfg_file(filename);
  po::variables_map vm;
  po::options_description config;
  po::options_description cmdline;
  po::options_description hlp;
  po::positional_options_description pd;

  get_description_cmd(config, cmdline, hlp, pd);
  po::notify(vm);
  try {
    po::store(po::parse_config_file(cfg_file, config, false), vm);
    po::notify(vm);
    interpret_options(vm);
  } catch ( const boost::program_options::error& e ) {
    tiler_error("Command-line Options", e.what());
  }
}

bool options::parse_cmdline(int argc, char** argv) {
  po::variables_map vm;
  po::options_description config;
  po::options_description cmdline;
  po::options_description hlp;
  po::positional_options_description pd;

  get_description_cmd(config, cmdline, hlp, pd);
  try {
    po::store(po::command_line_parser(argc, argv).options(cmdline).positional(pd).run(), vm);
    po::notify(vm);
    if (vm.count("version")) {
      std::cout << "\nVersion : " << version_string << "\n\n";
      return false;
    }
    if (vm.count("help")) {
      show_help(hlp);
      return false;
    }
    if (vm.count("boost-license")) {
      print_boost_license();
      return false;
    }
    if (vm.count("z3-license")) {
      print_z3_license();
      return false;
    }
    if (vm.count("diffy-license")) {
      print_diffy_license();
      return false;
    }
    if (!vm.count("input")) {
      std::cout << "\nNo input file specified!\n\n";
      std::cout << "Usage:\t";
      std::cout << toolName << " [Options] file.c \n\n";
      exit(1);
    }
    if (vm.count("config")) {
      boost::filesystem::path path(vm["config"].as<std::string>());
      parse_config(path);
    }
    interpret_options(vm);
  } catch ( const boost::program_options::error& e ) {
    tiler_error("Command-line Options", e.what());
    return false;
  }
  return true;
}

void options::print_z3_license() {
  std::cout << "Z3\n";
  std::cout << "Copyright (c) Microsoft Corporation\n";
  std::cout << "All rights reserved.\n";
  std::cout << "MIT License\n\n";
  std::cout << "Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the \"Software\"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:\n\n";
  std::cout << "The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.\n\n";
  std::cout << "THE SOFTWARE IS PROVIDED *AS IS*, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.\n";
}

void options::print_boost_license() {
  std::cout << "Boost Software License - Version 1.0 - August 17th, 2003\n\n";
  std::cout << "Permission is hereby granted, free of charge, to any person or organization obtaining a copy of the software and accompanying documentation covered by this license (the \"Software\") to use, reproduce, display, distribute, execute, and transmit the Software, and to prepare derivative works of the Software, and to permit third-parties to whom the Software is furnished to do so, all subject to the following:\n\n";
  std::cout << "The copyright notices in the Software and this entire statement, including the above license grant, this restriction and the following disclaimer, must be included in all copies of the Software, in whole or in part, and all derivative works of the Software, unless such copies or derivative works are solely in the form of machine-executable object code generated by a source language processor.\n\n";
  std::cout << "THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.\n";
}

void options::print_diffy_license() {
  std::cout <<"Diffy\n";
  std::cout <<" Copyright 2021-2022, Supratik Chakraborty, Ashutosh Gupta, Divyesh Unadkat, Computer Science Department, Indian Institute of Technology Bombay, Mumbai, India.\n";
  std::cout << "All rights reserved.\n";
  std::cout << "MIT License\n\n";
  std::cout << "Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the \"Software\"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:\n\n";
  std::cout << "The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.\n\n";
  std::cout << "THE SOFTWARE IS PROVIDED *AS IS*, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.\n";
}
