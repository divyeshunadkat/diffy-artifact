#ifndef OPTIONS_H
#define OPTIONS_H

#include<iostream>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
// pragam'ed to aviod warnings due to llvm included files
#include "llvm/IR/LLVMContext.h"
#pragma GCC diagnostic pop

#include "utils.h"
#include <boost/program_options.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <string>


namespace po = boost::program_options;

enum array_model_t{
  NONE,
  FULL,        // full model using store and select
  FIXED_LEN,   // array has fixed number of 'symbolic' cells
  PARTITION    // vaphor like model
};

enum tool{
  BMC,         // Build only the BMC tool
  TILER,       // Tiling implementation
  VAJRA,       // Full-Program implementation
  DIFFY,       // Difference Predicates implementation
  ALL          // Enable compilations of all tools
};

class options
{
public:
  //---------------------------------------
  //core options
  std::string fileName;
  std::string filePath;
  std::string funcName;
  std::string origFuncName;
  std::string outDirPath;
  bool dump_cfg = 0;
  //----------------------------------------

  //---------------------------------------
  //tiler options
  int mode = 0;
  int loopNum = 0;
  int totalLoops = 0;
  bool loop_aggr = false;
  //----------------------------------------

  //---------------------------------------
  //vajra options
  bool fpi = false;
  //----------------------------------------

  //---------------------------------------
  //vayuvajra options
  bool diff_preds = false;
  //----------------------------------------

  //----------------------------------------
  //bmc options
  int loop_unroll_count = 1;
  bool unwind=false;
  bool llvm_unroll=false; // selects between llvm_unroll or native
  bool includeOutOfBoundSpecs=false;
  bool includeOverUnderflowSpecs=false;
  array_model_t ar_model = FULL;
  int fixpointBound = 15; // Bound for weakest pre-computation
  //----------------------------------------

  //----------------------------------------
  //generic options
  int verbosity = 0;
  std::string version_string = "1.0";
  //----------------------------------------

  //----------------------------------------
  //Constants
  std::string SSA_SUFFIX = "_ssa";
  std::string DIFF_SUFFIX = "_diff";
  std::string PEEL_SUFFIX = "_peel";
  std::string PNM1_SUFFIX = "_pnm1";
  std::string QNM1_SUFFIX = "_qnm1";
  tool t = DIFFY;
  std::string toolName;
  std::string toolNameCaps;
  std::string toolDescription;
  const bool EXPLICIT_CONS = false;
  //----------------------------------------

  llvm::LLVMContext globalContext;

  options();
  void parse_config(boost::filesystem::path);
  bool parse_cmdline(int, char**);
  bool get_explcons();

private:
  void get_description_cmd(po::options_description&, po::options_description& ,
                           po::options_description&, po::positional_options_description&);
  void interpret_options(po::variables_map&);
  void show_help(po::options_description&);
  void print_z3_license();
  void print_boost_license();
  void print_diffy_license();
};

#endif
