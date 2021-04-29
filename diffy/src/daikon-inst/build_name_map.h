#ifndef BUILD_NAME_MAP_H
#define BUILD_NAME_MAP_H

#include "utils/options.h"
#include "utils/llvmUtils.h"

class build_name_map : public llvm::FunctionPass
{
public:
  static char ID;


  build_name_map( options&, std::string, value_expr_map&, name_map&,
                  std::map<std::string, llvm::Value*>&,
                  std::map<const bb*, rev_name_map >&,
                  std::map<const bb*, rev_name_map >&
                 );
  ~build_name_map();

  options& o;
  std::string name_suffix;
  std::map< const bb*, rev_name_map >& revStartLocalNameMap;//todo: likely useless
  std::map< const bb*, rev_name_map >& revEndLocalNameMap;
  std::map<std::string, llvm::Value*>& exprValMap;
  name_map& localNameMap;
  value_expr_map& def_map;

  void buildRevNameMap( llvm::Function &f );
  void buildParamExpr( llvm::Function &f );

  virtual bool runOnFunction(llvm::Function &f);
  llvm::StringRef getPassName() const;
  void getAnalysisUsage(llvm::AnalysisUsage &au) const;
};

#endif // BUILD_NAME_MAP_H
