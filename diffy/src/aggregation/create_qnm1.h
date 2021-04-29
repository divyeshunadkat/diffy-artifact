#ifndef CREATE_QNM1_H
#define CREATE_QNM1_H

#include "utils/llvmUtils.h"
#include "utils/z3Utils.h"
#include "utils/options.h"
#include "daikon-inst/loopdata.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/LoopSimplify.h"
#pragma GCC diagnostic pop

class create_qnm1 : public llvm::FunctionPass
{
  public:
  static char ID;
  std::unique_ptr<llvm::Module>& module;
  options& o;
  value_expr_map& def_map;
  std::map<llvm::Loop*, loopdata*>& ld_map;
  std::map<llvm::Function*, std::string>& fn_hash_map;
  std::map<std::string, llvm::ValueToValueMapTy>& fwd_fn_v2v_map;
  std::map<llvm::Function*, std::map<const llvm::Value*,
                                     const llvm::Value*>>& rev_fn_v2v_map;
  llvm::Value* N_orig = NULL;

  create_qnm1(std::unique_ptr<llvm::Module>&,
               options&,
               value_expr_map&,
               std::map<llvm::Loop*, loopdata*>&,
               std::map<llvm::Function*, std::string>&,
               std::map<std::string, llvm::ValueToValueMapTy>&,
               std::map<llvm::Function*, std::map<const llvm::Value*, const llvm::Value*>>&,
               llvm::Value*
               );
  ~create_qnm1();

  llvm::Function* target_f = NULL;
  llvm::BasicBlock* entry_bb = NULL;
  llvm::Value* N = NULL;
  llvm::Instruction *NS = NULL;
  llvm::Value *SingularNRef = NULL;

  std::vector<llvm::BasicBlock*> bb_vec;

  std::map<llvm::BasicBlock*, std::set<llvm::BasicBlock*>> block_preds_map;

  std::map<llvm::Loop*, llvm::ValueToValueMapTy> loop_vmap;
  std::map<llvm::Loop*, std::vector<llvm::BasicBlock*>> peeled_blocks_map;
  std::vector<llvm::BasicBlock*> all_peeled_blocks;

  std::set<llvm::BasicBlock*> red_blocks;
  std::set<llvm::Value*> affectedValues;
  std::map<llvm::Value*, std::set<llvm::Value*>> affectedFrom;

  std::set<llvm::Instruction*> all_loop_cond_inst;
  std::set<llvm::Instruction*> skip_inst;

  void peelLoops();
  void runPeelingForLoop(llvm::Loop *L, llvm::LoopInfo &LI,
                         llvm::ScalarEvolution &SE, llvm::DominatorTree &DT,
                         llvm::AssumptionCache &AC);
  void populateSkipInsts();
  void addLoopCondInst(llvm::Loop *L);
  void addBrCondsUses(llvm::BranchInst *BR);
  void skipAssumesAsserts();
  void skipSubLoads(llvm::Value* expr);

  void populateBlockAncestors();
  void markRedBlocks();
  void markDependents(llvm::Instruction* I);
  void markAllStoresAffected(llvm::BasicBlock*, std::set<llvm::Value*>&);
  std::set<llvm::Value*> getAffectingConds(llvm::BasicBlock* b);
  void computeAffected();
  void processLoad(llvm::LoadInst *load, llvm::BasicBlock *b);
  bool checkAffected(llvm::Value* expr);

  void propagatePeelEffects();
  void pushPeels();
  llvm::BasicBlock* runPeelShifting(llvm::Loop*, llvm::BasicBlock*,
                                    llvm::BasicBlock*);
  void extractPeels();
  void removeNonLoopBlockStores();

  virtual bool runOnFunction(llvm::Function &f);
  llvm::StringRef getPassName() const;
  void getAnalysisUsage(llvm::AnalysisUsage &au) const;

};

#endif // CREATE_QNM1_H
