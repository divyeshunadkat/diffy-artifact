#ifndef EXTRACT_PEELS_H
#define EXTRACT_PEELS_H

#include "utils/llvmUtils.h"
#include "utils/z3Utils.h"
#include "utils/options.h"

#include "daikon-inst/loopdata.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
// pragam'ed to aviod warnings due to llvm included files
#include "llvm/IR/IRBuilder.h"
#pragma GCC diagnostic pop

class extract_peels : public llvm::FunctionPass
{
  public:
  static char ID;
  std::unique_ptr<llvm::Module>& module;
  options& o;
  std::map<llvm::Loop*, loopdata*>& ld_map;
  std::map<llvm::Function*, std::string>& fn_hash_map;
  std::map<std::string, llvm::ValueToValueMapTy>& fwd_fn_v2v_map;
  std::map<llvm::Function*, std::map<const llvm::Value*,
                                     const llvm::Value*>>& rev_fn_v2v_map;
  llvm::ValueToValueMapTy fn_vmap;
  llvm::Value* N_orig = NULL;
  int& unsupported;

  extract_peels(std::unique_ptr<llvm::Module>&, options&, std::map<llvm::Loop*, loopdata*>&,
                std::map<llvm::Function*, std::string>&,
                std::map<std::string, llvm::ValueToValueMapTy>&,
                std::map<llvm::Function*, std::map<const llvm::Value*, const llvm::Value*>>&,
                llvm::Value*, int&);
  ~extract_peels();

  llvm::Function* target_f = NULL;
  llvm::BasicBlock* entry_bb = NULL;
  llvm::Value* N = NULL;
  llvm::Instruction *NS = NULL;
  llvm::Value *SingularNRef = NULL;
  std::list<llvm::Loop*> loopList;

  void peel_function(llvm::Function*);
  void remove_loop_back_edge(llvm::Loop *, loopdata *);
  void peel_loop(llvm::Loop* L, loopdata* ld);
  void peel_outer_block(llvm::BasicBlock*);

  void peel_cloned_function(llvm::Function*);
  void remove_loop_back_edge_in_clone(llvm::Loop*, loopdata *);

  bool loop_cloning(llvm::Function &);

  llvm::Function* create_diff_function(llvm::Function &);

  void process(llvm::Loop*, loopdata*, llvm::Function*);
  void clone_loop_blocks(llvm::Loop*, loopdata*, llvm::Function*);
  void peel_and_remap_loop_inst(llvm::Loop*, loopdata*);
  void rewire_cloned_loop_blocks(llvm::Loop*, loopdata*);

  void setSingularNRef();
  void generateNSVal();

  virtual bool runOnFunction(llvm::Function &f);
  llvm::StringRef getPassName() const;
  void getAnalysisUsage(llvm::AnalysisUsage &au) const;

};

#endif // EXTRACT_PEELS_H
