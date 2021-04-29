#ifndef DIFF_PREDS_H
#define DIFF_PREDS_H

#include "utils/llvmUtils.h"
#include "utils/z3Utils.h"
#include "utils/options.h"
#include "daikon-inst/loopdata.h"
#include "irtoz3expr/irtoz3expr_index.h"

class diff_preds : public llvm::FunctionPass
{
  public:
  static char ID;
  std::unique_ptr<llvm::Module>& module;
  z3::context& z3_ctx;
  options& o;
  value_expr_map& def_map;
  std::map<llvm::Loop*, loopdata*>& ld_map;
  name_map& localNameMap;
  std::map<std::string, llvm::Value*>& exprValMap;
  irtoz3expr_index* ir2e;
  glb_model g_model;
  std::map<llvm::Function*, std::map<const llvm::Value*,
                                     const llvm::Value*>>& fn_v2v_map;
  int& unsupported;

  diff_preds(std::unique_ptr<llvm::Module>&, z3::context&, options&, value_expr_map&,
             std::map<llvm::Loop*, loopdata*>&, name_map&,
             std::map<std::string, llvm::Value*>&, glb_model&,
             std::map<llvm::Function*, std::map<const llvm::Value*,const llvm::Value*>>&,
             int&);
  ~diff_preds();

  llvm::Function* diff_f = NULL;
  llvm::BasicBlock* entry_bb = NULL;
  llvm::Value* N = NULL;
  llvm::Instruction *NS = NULL;
  llvm::Value *SingularNRef = NULL;

  std::list<llvm::Loop*> loopList;

  std::map<llvm::Value*, std::list<llvm::Value*>> diff_val_map;
  std::map<llvm::Value*, std::list<llvm::Value*>> const_aggr_map;
  std::map<llvm::Value*, std::list<z3::expr>> diff_expr_map;

  void peel_function(llvm::Function*);
  void peel_loop(llvm::Loop* L, loopdata* ld);
  void append_diff_invs(llvm::AllocaInst*, llvm::StoreInst*,
                        std::list<llvm::Value*>&, llvm::Value*,
                        llvm::Loop*, loopdata*);
  void additional_record(llvm::AllocaInst*, std::list<llvm::Value*>&, llvm::Value*);
  void peel_outer_block(llvm::BasicBlock*);
  void add_diff_val(llvm::AllocaInst*, llvm::Value*);
  void add_const_aggr_val(llvm::AllocaInst*, llvm::Value*);
  void add_diff_expr(llvm::AllocaInst*, z3::expr);
  void identify_interferences(llvm::Loop*, loopdata*);
  z3::expr compute_diff_expr(llvm::Value*);
  llvm::Value* get_val_for_diff(z3::expr, llvm::BasicBlock*, llvm::Instruction*);

  virtual bool runOnFunction(llvm::Function &f);
  llvm::StringRef getPassName() const;
  void getAnalysisUsage(llvm::AnalysisUsage &au) const;

};

#endif // DIFF_PREDS_H
