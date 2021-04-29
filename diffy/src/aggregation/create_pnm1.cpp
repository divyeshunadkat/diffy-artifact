#include "create_pnm1.h"

char create_pnm1::ID = 0;

create_pnm1::create_pnm1(std::unique_ptr<llvm::Module>& m, options& o_,
                         std::map<llvm::Function*, std::string>& fn_h_map,
                         std::map<std::string, llvm::ValueToValueMapTy>& fwd_v2v_map,
                         std::map<llvm::Function*, std::map<const llvm::Value*,
                         const llvm::Value*>>& rev_v2v_map,
                         llvm::Value* N_o
                         )
  : llvm::FunctionPass(ID)
  , module(m)
  , o(o_)
  , fn_hash_map(fn_h_map)
  , fwd_fn_v2v_map(fwd_v2v_map)
  , rev_fn_v2v_map(rev_v2v_map)
{
  N_orig = N_o;
}

create_pnm1::~create_pnm1() {}

bool create_pnm1::runOnFunction( llvm::Function &f ) {
  if(f.getName() != o.funcName + o.PNM1_SUFFIX)
    return false;

  target_f = &f;

  // Get entry block
  entry_bb = &target_f->getEntryBlock();

  // N = find_ind_param_N(module, target_f, entry_bb);
  // Fetch from fwd_fn_v2v_map the clone of N in the original function
  if(llvm::isa<llvm::GlobalVariable>(N_orig))
    N = N_orig;
  else
    N = fwd_fn_v2v_map[fn_hash_map[target_f]][N_orig];

  if(N == NULL) N = find_ind_param_N(module, target_f, entry_bb);
  assert(N && "N is NULL while creating P_{N-1}");

  SingularNRef = getSingularNRef(N, entry_bb);
  if(SingularNRef == NULL) tiler_error("Create P{N-1}", "SingularNRef is NULL");
  NS = generateNSVal(N, SingularNRef, entry_bb);
  if(NS==NULL) tiler_error("Create P_{N-1}", "NS is NULL");

  transform_N_uses();
  return true;
}

void create_pnm1::transform_N_uses() {
  // Replace N with N-1 and remap instructions that refer to N except the
  // instruction for creating N-1 that we added above in the entry block
  llvm::ValueToValueMapTy VMap;
  VMap[SingularNRef] = NS;
  for (llvm::Instruction &I : *entry_bb)
    if(&I != NS && &I != SingularNRef)
      llvm::RemapInstruction(&I, VMap, llvm::RF_IgnoreMissingLocals |
                             llvm::RF_NoModuleLevelChanges);
  llvm::SmallVector<llvm::BasicBlock*, 50> blocks;
  for( llvm::BasicBlock& b : target_f->getBasicBlockList() )
    if(&b != entry_bb)
      blocks.push_back(&b);
  llvm::remapInstructionsInBlocks(blocks, VMap);
}

llvm::StringRef create_pnm1::getPassName() const {
  return "Generates the CFG of P_{N-1}";
}

void create_pnm1::getAnalysisUsage(llvm::AnalysisUsage &au) const {}
