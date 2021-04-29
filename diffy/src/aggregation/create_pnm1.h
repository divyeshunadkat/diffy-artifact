#ifndef CREATE_PNM1_H
#define CREATE_PNM1_H

#include "utils/llvmUtils.h"
#include "utils/z3Utils.h"
#include "utils/options.h"

class create_pnm1 : public llvm::FunctionPass
{
  public:
  static char ID;
  std::unique_ptr<llvm::Module>& module;
  options& o;
  std::map<llvm::Function*, std::string>& fn_hash_map;
  std::map<std::string, llvm::ValueToValueMapTy>& fwd_fn_v2v_map;
  std::map<llvm::Function*, std::map<const llvm::Value*,
                                     const llvm::Value*>>& rev_fn_v2v_map;
  llvm::Value* N_orig = NULL;

  create_pnm1(std::unique_ptr<llvm::Module>&,
              options&,
              std::map<llvm::Function*, std::string>&,
              std::map<std::string, llvm::ValueToValueMapTy>&,
              std::map<llvm::Function*, std::map<const llvm::Value*, const llvm::Value*>>&,
              llvm::Value*
              );
  ~create_pnm1();

  llvm::Function* target_f = NULL;
  llvm::BasicBlock* entry_bb = NULL;
  llvm::Value* N = NULL;
  llvm::Instruction *NS = NULL;
  llvm::Value *SingularNRef = NULL;

  void transform_N_uses();

  virtual bool runOnFunction(llvm::Function &f);
  llvm::StringRef getPassName() const;
  void getAnalysisUsage(llvm::AnalysisUsage &au) const;

};

#endif // CREATE_PNM1_H
