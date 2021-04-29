#ifndef SIMPLIFY_NL_H
#define SIMPLIFY_NL_H

#include "utils/llvmUtils.h"
#include "utils/z3Utils.h"
#include "utils/options.h"
#include "daikon-inst/loopdata.h"
#include "irtoz3expr/irtoz3expr_index.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
// pragam'ed to aviod warnings due to llvm included files
#include "llvm/Transforms/Utils/LoopUtils.h"
#pragma GCC diagnostic pop

class simplify_nl : public llvm::FunctionPass
{
  public:
  static char ID;

  z3::context& z3_ctx;
  options& o;
  std::map<llvm::Loop*, loopdata*>& ld_map;
  std::unique_ptr<llvm::Module>& module;
  glb_model g_model;
  llvm::Value* N = NULL;
  llvm::Value *SingularNRef = NULL;
  llvm::Function* target_f = NULL;
  llvm::BasicBlock* entry_bb = NULL;

  //irtoz3expr_index* ir2e;

  simplify_nl( z3::context& ,
               options&,
               value_expr_map&,
               std::map<llvm::Loop*, loopdata*>& ,
               name_map& ,
               std::map<std::string, llvm::Value*>&,
               std::unique_ptr<llvm::Module>&,
               glb_model&,
               llvm::Value*);
  ~simplify_nl();

  bool process_nl();
  bool check_rt_scope(llvm::Value*, llvm::Loop*);

  virtual bool runOnFunction(llvm::Function &f);
  llvm::StringRef getPassName() const;
  void getAnalysisUsage(llvm::AnalysisUsage &au) const;

};

#endif // SIMPLIFY_NL_H
