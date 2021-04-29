#include "extract_peels.h"
#include "llvm/Transforms/Utils/ValueMapper.h"
#include "llvm/Transforms/Utils/Cloning.h"

char extract_peels::ID = 0;

extract_peels::extract_peels(std::unique_ptr<llvm::Module>& m,
                             options& o_,
                             std::map<llvm::Loop*, loopdata*>& ldm,
                             std::map<llvm::Function*, std::string>& fn_h_map,
                             std::map<std::string, llvm::ValueToValueMapTy>& fwd_v2v_map,
                             std::map<llvm::Function*, std::map<const llvm::Value*,
                             const llvm::Value*>>& rev_v2v_map,
                             llvm::Value* N_o, int& unsup)
  : llvm::FunctionPass(ID)
  , module(m)
  , o(o_)
  , ld_map(ldm)
  , fn_hash_map(fn_h_map)
  , fwd_fn_v2v_map(fwd_v2v_map)
  , rev_fn_v2v_map(rev_v2v_map)
  , unsupported(unsup)
{
  N_orig = N_o;
}

extract_peels::~extract_peels() {}

bool extract_peels::runOnFunction( llvm::Function &f ) {
  if(f.getName() != o.funcName) {
    return false;
  }
  // We assume that arrays are of size N or 1
  // for array of size N, all entries are updated in the loop and not outside
  // for arrays of size 1, its entry can be updated in a block within or outside the loop
  // peel_cloned_function(&f);
  peel_function(&f);
  return true;
}

void extract_peels::
peel_function(llvm::Function* peel_fun) {

  target_f = peel_fun;
  auto ld = ld_map.at(NULL);
  if(!ld) tiler_error("Peeling", "Unpopulated loop data map");
  std::vector< loopdata* >& sub_loops = ld->childHeads;

  // Peel the function only if loops are present
  if(sub_loops.empty()) return;

  // Get entry block
  entry_bb = &peel_fun->getEntryBlock();

  // Fetch from fwd_fn_v2v_map the clone of N in the original function
  if(llvm::isa<llvm::GlobalVariable>(N_orig))
    N = N_orig;
  else
    N = fwd_fn_v2v_map[fn_hash_map[target_f]][N_orig];

  if(N == NULL) N = find_ind_param_N(module, target_f, entry_bb);
  assert(N && "N is NULL while creating P'_{N-1}");

  setSingularNRef();
  generateNSVal();
  assert(NS && "NS is NULL while creating P'_{N-1}");

  // Check if induction parameter is present in the index expression
  check_indu_param_in_index(peel_fun, N, unsupported);

  unsigned gap_num = 0;
  for( auto b : ld->getCurrentBlocks() ) {
    if( b == NULL ) {
      //processing sub loops
      auto sub_loop = sub_loops[gap_num++];
      peel_loop( sub_loop->loop, sub_loop );
    }else{
      peel_outer_block(b);
    }
  }
  assert( gap_num == ld->childHeads.size() );
}

// Peel an iteration of the loop
void extract_peels::
peel_loop(llvm::Loop* L, loopdata* ld) {
  loopList.push_back(L);
  // Remove the back edge
  remove_loop_back_edge(L, ld);
  // Replace i with N-1 and remap instructions
  ld->VMap[ld->ctr] = NS;
  llvm::SmallVector<llvm::BasicBlock*, 40> bb_sv;
  for(llvm::BasicBlock* b : L->getBlocks())
    bb_sv.push_back(b);
  llvm::remapInstructionsInBlocks(bb_sv, ld->VMap);
  ld->VMap.clear();
  // Remove phi inst of i
  if(llvm::Instruction *I = llvm::dyn_cast<llvm::Instruction>(ld->ctr)) {
    I->eraseFromParent();
  } else {
    tiler_error("Peeling", "Unable to remove the phi node from the peel");
  }
}

// Remove stores that are not needed in the peel
void extract_peels::
peel_outer_block(llvm::BasicBlock* bb) {
  // loopdata pointer for all blocks outside any loops
  auto ld = ld_map.at(NULL);
  populate_arr_rd_wr(bb, ld->arrRead, ld->arrWrite);
  if(!ld->arrWrite.empty()) {
    std::map<llvm::Value*,std::list<llvm::Value*>>::iterator wbit = ld->arrWrite.begin();
    std::map<llvm::Value*,std::list<llvm::Value*>>::iterator weit = ld->arrWrite.end();
    for (; wbit!=weit; wbit++) {
      // auto arry = wbit->first;
      // llvm::AllocaInst *arry_alloca = llvm::dyn_cast<llvm::AllocaInst>(arry);
      std::list<llvm::Value*> vl = wbit->second;
      for(auto v : vl) {
        llvm::StoreInst* store = llvm::dyn_cast<llvm::StoreInst>(v);
        if (llvm::dyn_cast<llvm::ConstantInt>(store->getOperand(0))) {
          auto addr = store->getOperand(1);
          llvm::BitCastInst* addr_bc = NULL;
          if( (addr_bc = llvm::dyn_cast<llvm::BitCastInst>(addr)) )
            addr = addr_bc->getOperand(0);
          auto gep = llvm::dyn_cast<llvm::GetElementPtrInst>(addr);
          store->eraseFromParent();
          if(addr_bc != NULL) addr_bc->eraseFromParent();
          gep->eraseFromParent();
        } else if(auto load = llvm::dyn_cast<llvm::LoadInst>(store->getOperand(0))) {
          auto load_addr = load->getOperand(0);
          llvm::BitCastInst* load_addr_bc = NULL;
          if( (load_addr_bc = llvm::dyn_cast<llvm::BitCastInst>(load_addr)) )
            load_addr = load_addr_bc->getOperand(0);
          auto load_elemPtr = llvm::dyn_cast<llvm::GetElementPtrInst>(load_addr);
          auto st_addr = store->getOperand(1);
          llvm::BitCastInst* st_addr_bc = NULL;
          if( (st_addr_bc = llvm::dyn_cast<llvm::BitCastInst>(st_addr)) )
            st_addr = st_addr_bc->getOperand(0);
          auto st_elemPtr = llvm::dyn_cast<llvm::GetElementPtrInst>(st_addr);
          store->eraseFromParent();
          if(st_addr_bc != NULL) st_addr_bc->eraseFromParent();
          st_elemPtr->eraseFromParent();
          if(load != SingularNRef) {
            load->eraseFromParent();
            if(load_addr_bc != NULL) load_addr_bc->eraseFromParent();
            load_elemPtr->eraseFromParent();
          }
        } else {
          store->eraseFromParent();
        }
      }
    }
  } else {}
}

void extract_peels::
remove_loop_back_edge (llvm::Loop* L, loopdata* ld) {
  bb* exit_block = NULL;
  bb* header = L->getHeader();
  bb* latch = L->getLoopLatch();
  llvm::Instruction* term = latch->getTerminator();
  if (llvm::BranchInst* bi = llvm::dyn_cast<llvm::BranchInst>(term)) {
    const int NumS = bi->getNumSuccessors();
    for(int i=0; i<NumS; i++) {
      bb* s = bi->getSuccessor(i);
      if(s != header) {
        exit_block = s;
      }
    }

    // Remove the conditional branch and add a unconditional branch
    llvm::IRBuilder<> b(term);
    b.CreateBr(exit_block);
    llvm::Value *loopCond = bi->getCondition();
    bi->eraseFromParent();
    if( auto icmp = llvm::dyn_cast<llvm::ICmpInst>(loopCond) ) {
      icmp->eraseFromParent();
    }
/*
// Doing this generates bmc formula where the
// needed path gets sliced off
    for(int i=0; i<NumS; i++) {
      bb* s = bi->getSuccessor(i);
      if(s == header) {
        bi->setSuccessor(i, exit_block);
      }
    }
*/
  }
}

void extract_peels::setSingularNRef() {
  if(llvm::isa<llvm::GlobalVariable>(N)) {
    for(auto it = entry_bb->begin(), e = entry_bb->end(); it != e; ++it) {
      llvm::Instruction *I = &(*it);
      if(llvm::LoadInst *load = llvm::dyn_cast<llvm::LoadInst>(I) ) {
        if(N == load->getOperand(0)) {
          SingularNRef = load;
          break;
        } else {}
      } else {}
    }
  } else {
    SingularNRef = N;
  }
  if(SingularNRef == NULL)
    tiler_error("Create P'_{N-1}","Program Parameter is NULL");
}

void extract_peels::generateNSVal() {
  if(llvm::isa<llvm::GlobalVariable>(N)) {
    llvm::Type *oneTy = N->getType();
    oneTy = oneTy->getPointerElementType();
    llvm::Value *one = llvm::ConstantInt::get(oneTy, 1);
    llvm::LoadInst *Nload = llvm::dyn_cast<llvm::LoadInst>(SingularNRef);
    NS = llvm::BinaryOperator::Create(llvm::Instruction::Sub, Nload, one, "NS");
    NS->insertAfter(Nload);
  } else {
    llvm::Type *oneTy = N->getType();
    llvm::Value *one = llvm::ConstantInt::get(oneTy, 1);
    NS = llvm::BinaryOperator::Create(llvm::Instruction::Sub, N, one, "NS");
    NS->insertBefore(entry_bb->getFirstNonPHI());
  }
}

llvm::StringRef extract_peels::getPassName() const {
  return "Gives the CFG with only the peeled iterations of each loop";
}

void extract_peels::getAnalysisUsage(llvm::AnalysisUsage &au) const {
  au.addRequired<llvm::LoopInfoWrapperPass>();
}
