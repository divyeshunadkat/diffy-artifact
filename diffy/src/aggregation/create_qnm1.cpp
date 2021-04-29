#include "create_qnm1.h"

char create_qnm1::ID = 0;

create_qnm1::create_qnm1(std::unique_ptr<llvm::Module>& m, options& o_,
                           value_expr_map& def_map_,
                           std::map<llvm::Loop*, loopdata*>& ldm,
                           std::map<llvm::Function*, std::string>& fn_h_map,
                           std::map<std::string, llvm::ValueToValueMapTy>& fwd_v2v_map,
                           std::map<llvm::Function*, std::map<const llvm::Value*,
                           const llvm::Value*>>& rev_v2v_map,
                           llvm::Value* N_o
                           )
  : llvm::FunctionPass(ID)
  , module(m)
  , o(o_)
  , def_map(def_map_)
  , ld_map(ldm)
  , fn_hash_map(fn_h_map)
  , fwd_fn_v2v_map(fwd_v2v_map)
  , rev_fn_v2v_map(rev_v2v_map)
{
  N_orig = N_o;
}

create_qnm1::~create_qnm1() {}

bool create_qnm1::runOnFunction( llvm::Function &f ) {
  // if(f.getName() != o.funcName + o.QNM1_SUFFIX)
  if(f.getName() != o.funcName)
   return false;

  target_f = &f;

  // Get entry block
  entry_bb = &target_f->getEntryBlock();

  // Fetch from fwd_fn_v2v_map the clone of N in the original function
  if(llvm::isa<llvm::GlobalVariable>(N_orig))
    N = N_orig;
  else
    N = fwd_fn_v2v_map[fn_hash_map[target_f]][N_orig];

  if(N == NULL) N = find_ind_param_N(module, target_f, entry_bb);
  assert(N && "N is NULL while creating Q_{N-1}");

  SingularNRef = getSingularNRef(N, entry_bb);
  if(SingularNRef == NULL) tiler_error("Create Q{N-1}", "SingularNRef is NULL");
  NS = generateNSVal(N, SingularNRef, entry_bb);
  if(NS==NULL) tiler_error("Create Q_{N-1}", "NS is NULL");

  peelLoops();
  get_topologically_sorted_bb(target_f, bb_vec);
  populateBlockAncestors();
  populateSkipInsts();
  // populateBlockCondHist(); // TODO
  computeAffected();
  // markRedBlocks();
  propagatePeelEffects();
  removeNonLoopBlockStores();
  pushPeels();
  extractPeels();
  return true;
}

void create_qnm1::peelLoops() {
  llvm::DominatorTree &DT = getAnalysis<llvm::DominatorTreeWrapperPass>().getDomTree();
  llvm::LoopInfo &LI = getAnalysis<llvm::LoopInfoWrapperPass>().getLoopInfo();
  llvm::ScalarEvolution &SE = getAnalysis<llvm::ScalarEvolutionWrapperPass>().getSE();
  llvm::AssumptionCache &AC = getAnalysis<llvm::AssumptionCacheTracker>().getAssumptionCache(*target_f);
  for (auto I = LI.rbegin(), E = LI.rend(); I != E; ++I)
    runPeelingForLoop(*I, LI, SE, DT, AC);
}

void create_qnm1::
runPeelingForLoop(llvm::Loop *L, llvm::LoopInfo &LI,
                  llvm::ScalarEvolution &SE, llvm::DominatorTree &DT,
                  llvm::AssumptionCache &AC) {
  bool PreserveLCSSA = true;
  int PeelCount = 1;
  std::vector<llvm::BasicBlock*>& peeledBlocks = peeled_blocks_map[L];
  llvm::Value* exitVal = ld_map.at(L)->exitValue;
  int stepCnt = ld_map.at(L)->stepCnt;
  bool Peeled = myPeelLoop(L, PeelCount, &LI, &SE, &DT, &AC, PreserveLCSSA,
                           N, NS, exitVal, stepCnt, loop_vmap[L], peeledBlocks); // Peel last iteration
  // bool Peeled = peelLoop(L, 1, LI, &SE, &DT, &AC, PreserveLCSSA); // Peel first iteration
  if(!Peeled) tiler_error("Create Q_{N-1}","Unable to peel a loop");
  // Collect all the newly created blocks for future use
  for (llvm::BasicBlock *b : peeledBlocks)
    all_peeled_blocks.push_back(b);
  for (llvm::Loop *SL : L->getSubLoops())
    runPeelingForLoop(SL, LI, SE, DT, AC);
}

void create_qnm1::populateSkipInsts() {
  if(llvm::isa<const llvm::GlobalVariable>(N)) {
    for(auto it = entry_bb->begin(), e = entry_bb->end(); it != e; ++it) {
      llvm::Instruction *I = &(*it);
      if(llvm::ICmpInst* icmp = llvm::dyn_cast<llvm::ICmpInst>(I))
        skip_inst.insert(icmp); // Skip instructions in the entry block
    }
  }
  skipAssumesAsserts();
  llvm::LoopInfo *LI = &getAnalysis<llvm::LoopInfoWrapperPass>().getLoopInfo();
  for (auto I = LI->rbegin(), E = LI->rend(); I != E; ++I)
    addLoopCondInst(*I);
}

void create_qnm1::addLoopCondInst(llvm::Loop *L) {
  llvm::BasicBlock *Latch = L->getLoopLatch();
  llvm::BranchInst *LatchBR = llvm::cast<llvm::BranchInst>(Latch->getTerminator());
  if(LatchBR) addBrCondsUses(LatchBR);
  llvm::BranchInst *GuardBR = getLoopGuardBR(L, true);
  if(GuardBR) addBrCondsUses(GuardBR);
  for (llvm::Loop *SL : L->getSubLoops())
    addLoopCondInst(SL);
}

void create_qnm1::addBrCondsUses(llvm::BranchInst *BR) {
  llvm::Value *loopCond = BR->getCondition();
  if( llvm::ICmpInst *icmp = llvm::dyn_cast<llvm::ICmpInst>(loopCond) ) {
    all_loop_cond_inst.insert(icmp);
    for(auto U = icmp->use_begin(); U != icmp->use_end(); U++)
      if( llvm::Instruction *I = llvm::dyn_cast<llvm::Instruction>(*U) )
        skip_inst.insert(I); // Skip immediate uses of terminators and guards
  } else { tiler_error("Create Q_{N-1}", "Loop condition is not a ICmp inst");}
}

void create_qnm1::skipAssumesAsserts() {
  for(bb* b: bb_vec) {
    for( auto it = b->begin(), e = b->end(); it != e; ++it) {
      llvm::Instruction *I = &(*it);
      if(llvm::CallInst* call = llvm::dyn_cast<llvm::CallInst>(I)) {
        if( llvm::isa<llvm::IntrinsicInst>(call) ) continue;
        if( call->getNumArgOperands() <= 0 ) continue;
        auto op = llvm::dyn_cast<llvm::Instruction>(call->getArgOperand(0));
        if( auto cast = llvm::dyn_cast<llvm::CastInst>(op) )
          op = cast;
        if(llvm::ICmpInst* icmp = llvm::dyn_cast<llvm::ICmpInst>(op->getOperand(0))) {
          if( is_assume_call(call) ) {
            skip_inst.insert(icmp); // Skip compares in assumes
          } else if ( is_assert_call(call) ) {
            skip_inst.insert(icmp); // Skip compares in asserts
            skipSubLoads(icmp->getOperand(0));
            skipSubLoads(icmp->getOperand(1));
          } else {}
        } else {}
      } else {}
    }
  }
}

void create_qnm1::skipSubLoads(llvm::Value* expr) {
  if(auto bop = llvm::dyn_cast<llvm::BinaryOperator>(expr) ) {
    skipSubLoads(bop->getOperand(0));
    skipSubLoads(bop->getOperand(1));
  } else if(auto load = llvm::dyn_cast<llvm::LoadInst>(expr) ) {
    skip_inst.insert(load);
  } else if( auto cast = llvm::dyn_cast<llvm::CastInst>(expr) ) {
    skipSubLoads(cast->getOperand(0));
  } else {}
}

void create_qnm1::populateBlockAncestors() {
  for( auto b : bb_vec ) {
    if(o.verbosity > 20) {
      llvm::errs () << "\nComputing Ancestors for Block\n";
      b->print(llvm::errs());
      llvm::errs() << "\n\n";
    }
    collect_block_ancestors(b, block_preds_map);
    if(o.verbosity > 20) {
      llvm::errs () << "\nAncestors are\n";
      for( auto ab : block_preds_map.at(b)) {
        ab->print(llvm::errs());
        llvm::errs() << "\n\n";
      }
    }
  }
}

void create_qnm1::markRedBlocks() {
  for( auto b : bb_vec ) {
    bool markAffected = false;
    for( auto it = b->begin(), e = b->end(); it != e; ++it) {
      llvm::Instruction *I = &(*it);
      if(llvm::StoreInst* store = llvm::dyn_cast<llvm::StoreInst>(I)) {
        if(affectedValues.find(store) != affectedValues.end()) {
          markAffected = true;
          break;
        }
      } else if(llvm::LoadInst* load = llvm::dyn_cast<llvm::LoadInst>(I)) {
        if(affectedValues.find(load) != affectedValues.end()) {
          markAffected = true;
          break;
        }
      } else if(llvm::ICmpInst* icmp = llvm::dyn_cast<llvm::ICmpInst>(I)) {
        if(affectedValues.find(icmp) != affectedValues.end()) {
          markAffected = true;
          break;
        }
      } else {}
    }
    if(markAffected)
      red_blocks.insert(b);
  }
}

// Only load inst, store inst and icmp inst are marked as affected
void create_qnm1::markDependents(llvm::Instruction* I) {
  if(llvm::LoadInst* load = llvm::dyn_cast<llvm::LoadInst>(I)) {
    if(checkDependence(load, SingularNRef)) {
      if(o.verbosity > 10) {
        llvm::errs() << "\nLoad stmt directly affected:\n";
        load->print(llvm::errs());
        llvm::errs() << "\n";
      }
      affectedValues.insert(load);
      affectedFrom[load].insert(SingularNRef);
    }
  } else if(llvm::StoreInst* store = llvm::dyn_cast<llvm::StoreInst>(I)) {
    if(checkDependence(store->getOperand(0), SingularNRef)) {
      if(o.verbosity > 10) {
        llvm::errs() << "\nStore stmt directly affected:\n";
        store->print(llvm::errs());
        llvm::errs() << "\n";
      }
      affectedValues.insert(store);
      affectedFrom[store].insert(SingularNRef);
    }
  } else if(llvm::ICmpInst* icmp = llvm::dyn_cast<llvm::ICmpInst>(I)) {
    if(checkDependence(icmp, SingularNRef)) {
      if(o.verbosity > 10) {
        llvm::errs() << "\nICMP stmt directly affected:\n";
        icmp->print(llvm::errs());
        llvm::errs() << "\n";
      }
      affectedValues.insert(icmp);
      affectedFrom[icmp].insert(SingularNRef);
    }
  } else {}
}

void create_qnm1::markAllStoresAffected(llvm::BasicBlock* b, std::set<llvm::Value*>& affectingConds) {
  for( auto it = b->begin(), e = b->end(); it != e; ++it) {
    llvm::Instruction *I = &(*it);
    if(llvm::StoreInst* store = llvm::dyn_cast<llvm::StoreInst>(I)) {
      affectedValues.insert(store);
      for(auto c : affectingConds)
        affectedFrom[store].insert(c);
      if(o.verbosity > 10) {
        llvm::errs() << "\nStore covered by affected conditional stmt:\n";
        store->print(llvm::errs());
        llvm::errs() << "\n";
      }
    } else {}
  }
}

// Currently just looks at predecessors
// TODO: Design a data-structure to store the branching history of each block
// Mark affected variables based on the branching history of each block
std::set<llvm::Value*> create_qnm1::getAffectingConds(llvm::BasicBlock* b) {
  std::set<llvm::Value*> affectingConds;
  for (llvm::BasicBlock *pb : llvm::predecessors(b)) {
    for( auto it = pb->begin(), e = pb->end(); it != e; ++it) {
      llvm::Instruction *I = &(*it);
      if(llvm::ICmpInst* icmp = llvm::dyn_cast<llvm::ICmpInst>(I)) {
        if(affectedValues.find(icmp) != affectedValues.end()) {
          if(o.verbosity > 10) {
            llvm::errs() << "\nFound affected conditional\n";
            icmp->print(llvm::errs());
            llvm::errs() << "\n";
          }
          affectingConds.insert(icmp);
        }
      } else {}
    }
  }
  return affectingConds;
}

void create_qnm1::computeAffected() {
  for( auto b : bb_vec ) {
    if(o.verbosity > 10) {
      llvm::errs () << "\nProcessing Block\n";
      b->print(llvm::errs());
      llvm::errs() << "\n\n";
    }
    // Values in the entry block are not marked as affected when N is a global variable
    if(b == entry_bb && llvm::isa<const llvm::GlobalVariable>(N)) continue;
    // Values in the peeled blocks are not marked
    if(find(all_peeled_blocks.begin(), all_peeled_blocks.end(), b) != all_peeled_blocks.end())
      continue;
    for( auto it = b->begin(), e = b->end(); it != e; ++it) {
      llvm::Instruction *I = &(*it);
      if(exists(all_loop_cond_inst, I)) {
        continue;  // Do not consider the loop guards and terminators for dependence
      } else if(exists(skip_inst, I)) {
        continue;  // Do not consider instructions marked for skipping
      } else {}
      // Mark any load, store and icmp inst that directly depend on N that are not to be skipped
      markDependents(I);
      // If history has a affected conditional then stores in this block need to be marked
      std::set<llvm::Value*> affectingConds = getAffectingConds(b);
      if(!affectingConds.empty())
        markAllStoresAffected(b, affectingConds);
      // Transitively mark all uses defined in peeled blocks, defs and icmp with affected uses
      if(llvm::LoadInst* load = llvm::dyn_cast<llvm::LoadInst>(I)) {
        processLoad(load, b);
      } else if(llvm::StoreInst* store = llvm::dyn_cast<llvm::StoreInst>(I)) {
        if(checkAffected(store->getOperand(0))) {
          if(o.verbosity > 10) {
            llvm::errs() << "\nStore affected after its load was affected:\n";
            store->print(llvm::errs());
            llvm::errs() << "\n";
          }
          affectedValues.insert(store);
        }
      } else if(llvm::ICmpInst* icmp = llvm::dyn_cast<llvm::ICmpInst>(I)) {
        if(checkAffected(icmp)) {
          if(o.verbosity > 10) {
            llvm::errs() << "\nCompare affected after its load was affected:\n";
            icmp->print(llvm::errs());
            llvm::errs() << "\n";
          }
          affectedValues.insert(icmp);
        }
      } else {}
    }
  }
  if(o.verbosity > 2) {
    llvm::errs() << "\n\nAffected values\n\n";
    for(auto I : affectedValues) {
      llvm::errs() << "\nAffected value\n";
      I->print(llvm::errs());
      llvm::errs () << "\n";
      for(auto v : affectedFrom[I]) {
        llvm::errs() << "\nAffected from value\n";
        v->print(llvm::errs());
        llvm::errs () << "\n";
      }
      llvm::errs () << "\n\n";
    }
    llvm::errs() << "\n\nEnd Affected values\n\n";
  }
}

void create_qnm1::processLoad(llvm::LoadInst *load, llvm::BasicBlock *b) {
  if(llvm::isa<const llvm::GlobalVariable>(load->getOperand(0)))
    return;
  auto load_arr = getAlloca(load);
  for( auto pb : block_preds_map[b] ) {
    if(find(all_peeled_blocks.begin(), all_peeled_blocks.end(), pb) ==
       all_peeled_blocks.end()) {
      if(o.verbosity > 10) {
        llvm::errs () << "\nProcessing non peeled ancestor block\n";
        pb->print(llvm::errs());
        llvm::errs() << "\n\n";
      }
      for( auto pit = pb->begin(), pe = pb->end(); pit != pe; ++pit) {
        llvm::Instruction *PI = &(*pit);
        if(llvm::StoreInst* pred_store = llvm::dyn_cast<llvm::StoreInst>(PI)) {
          auto p_st_arr = getAlloca(pred_store);
          if(p_st_arr == NULL) continue; // Store to global variable
          if(p_st_arr->isStaticAlloca()) continue; // Effect of scalars comes from peels
          if(load_arr != p_st_arr) continue;
          if(affectedValues.find(pred_store) != affectedValues.end()) {
            if(o.verbosity > 10) {
              llvm::errs() << "\nLoad affected as its store is already affected:\n";
              load->print(llvm::errs());
              llvm::errs() << "\n";
            }
            affectedValues.insert(load);
            affectedFrom[load].insert(pred_store);
          }
        } else {}
      }
    } else {
      if(o.verbosity > 10) {
        llvm::errs () << "\nProcessing peeled ancestor block\n";
        pb->print(llvm::errs());
        llvm::errs() << "\n\n";
      }
      for( auto pit = pb->begin(), pe = pb->end(); pit != pe; ++pit) {
        llvm::Instruction *PI = &(*pit);
        if(llvm::StoreInst* pred_store = llvm::dyn_cast<llvm::StoreInst>(PI)) {
          auto p_st_arr = getAlloca(pred_store);
          if(p_st_arr == NULL) continue; // Store to global variable
          if(load_arr != p_st_arr) continue;
          // Static allocas like sum[0] can be definitely marked
          if(p_st_arr->isStaticAlloca()) {
            if(o.verbosity > 10) {
              llvm::errs() << "\nLoad affected as the scalar was last written in a peeled block:\n";
              load->print(llvm::errs());
              llvm::errs() << "\n";
            }
            affectedValues.insert(load);
            affectedFrom[load].insert(pred_store);
          }
          // TODO:
          // if the load stmt is in a loop block and store is in a peeled
          // block then check if the range for idx has the value stidx in it
          // auto load_idx = getIdx(load);
          // auto store_idx = getIdx(pred_st);
          // z3::expr lidx_var = def_map.get_term(load_idx);
          // z3::expr stidx_var = def_map.get_term(store_idx);
        } else {}
      }
    }
  }
}

bool create_qnm1::checkAffected(llvm::Value* expr) {
  if(affectedValues.find(expr) != affectedValues.end()) return true;
  if(auto bop = llvm::dyn_cast<llvm::BinaryOperator>(expr) ) {
    bool res = checkAffected(bop->getOperand(0));
    if(res) return true;
    res = checkAffected(bop->getOperand(1));
    return res;
  } else if(llvm::ICmpInst* icmp = llvm::dyn_cast<llvm::ICmpInst>(expr)) {
    bool res = checkAffected(icmp->getOperand(0));
    if(res) return true;
    res = checkAffected(icmp->getOperand(1));
    return res;
  } else if(auto store = llvm::dyn_cast<llvm::StoreInst>(expr) ) {
    // This case should ideally not occur
    return checkAffected(store->getOperand(0));
  } else if( auto cast = llvm::dyn_cast<llvm::CastInst>(expr) ) {
    return checkAffected(cast->getOperand(0));
  } else {
    return false;
  }
}

void create_qnm1::propagatePeelEffects() {
  std::map<llvm::Value*, std::set<llvm::Value*>>::iterator it;
  for(it=affectedFrom.begin(); it!=affectedFrom.end(); it++) {
    if(llvm::LoadInst* load = llvm::dyn_cast<llvm::LoadInst>(it->first)) {
      if(llvm::isa<const llvm::GlobalVariable>(load->getOperand(0)))
        tiler_error("Create Q_{N-1}", "This case cannot happen");
      if(it->second.size()!=1)
        tiler_error("Create Q_{N-1}", "Multiple affecting stores from peels or None!");
      llvm::Instruction* phi = NULL;
      auto load_arr = getAlloca(load);
      if(!load_arr->isStaticAlloca()) {
        auto load_idx = getIdx(load);
        auto v = getPhiNode(load_idx);
        if(v != NULL) phi = llvm::dyn_cast<llvm::Instruction>(v);
      } else {}
      std::set<llvm::Value*>::iterator v = it->second.begin();
      if(auto store = llvm::dyn_cast<llvm::StoreInst>(*v) ) {
        // Create the clone of the value stored in the store; handle indices for nested loops
        llvm::ValueToValueMapTy VMap;
        if(llvm::Instruction* stOpInst = llvm::dyn_cast<llvm::Instruction>(store->getOperand(0))) {
          llvm::Instruction* NRef = llvm::dyn_cast<llvm::Instruction>(SingularNRef);
          if(stOpInst == NRef) {
            VMap[load] = NRef;
          } else {
            llvm::Instruction* cloneStOpInst = cloneExpr(stOpInst, load, NRef, phi);
            assert(cloneStOpInst && "Unable to clone the stored value and its trail");
            VMap[load] = cloneStOpInst;
          }
        } else if( llvm::isa<llvm::Constant>(store->getOperand(0)) ) {
          VMap[load] = store->getOperand(0);
        } else {}
        // Map the load with the top instruction of the newly cloned instruction set
        for (llvm::Instruction &I : *(load->getParent()))
          llvm::RemapInstruction(&I, VMap, llvm::RF_IgnoreMissingLocals |
                                 llvm::RF_NoModuleLevelChanges);
        // Remove the load instruction and its trail
        auto load_addr = load->getOperand(0);
        llvm::BitCastInst* load_addr_bc = NULL;
        if( (load_addr_bc = llvm::dyn_cast<llvm::BitCastInst>(load_addr)) )
          load_addr = load_addr_bc->getOperand(0);
        auto load_elemPtr = llvm::dyn_cast<llvm::GetElementPtrInst>(load_addr);
        load->eraseFromParent();
        if(load_addr_bc != NULL) load_addr_bc->eraseFromParent();
        load_elemPtr->eraseFromParent();
      } else tiler_error("Create Q_{N-1}","Only store can affect a load !");
    } else {}
  }
}

void create_qnm1::pushPeels() {
  llvm::DominatorTree &DT = getAnalysis<llvm::DominatorTreeWrapperPass>().getDomTree();
  llvm::LoopInfo &LI = getAnalysis<llvm::LoopInfoWrapperPass>().getLoopInfo();
  llvm::Loop *lastLoop = *LI.begin();
  assert(lastLoop && "Unable to locate the last loop in the program");
  llvm::BasicBlock* PeelEnd = peeled_blocks_map.at(lastLoop)[2];
  assert(PeelEnd && "Last peeled block of the loop not found");
  llvm::BasicBlock* InsertEnd = PeelEnd->getUniqueSuccessor();
  assert(InsertEnd && "Unable to locate the successor of the last peeled block of the last loop");
  llvm::BasicBlock *InsertBegin = llvm::SplitEdge(PeelEnd, InsertEnd, &DT, &LI);
  assert(InsertBegin && "Spliting the edge to create new block failed");
  for (auto I = LI.rbegin(), E = LI.rend(); I != E; ++I)
    InsertBegin = runPeelShifting(*I, InsertBegin, InsertEnd);
}

llvm::BasicBlock*
create_qnm1::runPeelShifting(llvm::Loop *L, llvm::BasicBlock* Exit,
                              llvm::BasicBlock* ExitSucc) {
  llvm::BasicBlock* PeelStart = peeled_blocks_map.at(L)[0];
  if(!PeelStart) tiler_error("Create Q_{N-1}", "First block of the peel not found");
  llvm::BasicBlock* PeelEnd = peeled_blocks_map.at(L)[2];
  if(!PeelEnd) tiler_error("Create Q_{N-1}", "Last block of the peel not found");
  llvm::BasicBlock* PeelStartPred = PeelStart->getUniquePredecessor();
  if(!PeelStartPred) tiler_error("Create Q_{N-1}", "Predecessor of the peel start not found");
  llvm::BasicBlock* PeelEndSucc = PeelEnd->getUniqueSuccessor();
  if(!PeelEndSucc) tiler_error("Create Q_{N-1}", "Successor of the peel end not found");

  // Order in which we get sub loops will be important
  for (llvm::Loop *SL : L->getSubLoops()) {
    // Before Pulling the peel of a sub loop out need to convert it into a loop
    // Requires adding a phi node, loop condition, compare and branch instructions
    // makePeelLoopy(L);
    llvm::BasicBlock* PeelStartSucc = PeelStart->getUniqueSuccessor();
    if(!PeelEndSucc) tiler_error("Create Q_{N-1}", "Successor of the peel start not found");
    llvm::BasicBlock* RetBlock = runPeelShifting(SL, PeelStart, PeelStartSucc);
    if(!RetBlock) { tiler_error("Peel Shifting", "Returned block null"); }
    if(RetBlock) tiler_error("Create Q_{N-1}", "Returned block was null");
    // Need to update the LoopInfo and MemorySSAUpdater after pulling blocks out
    // of a loop. May be this can be done like a DT recalculation. Looks tricky.
  }

  // Detach the peel from where its current located
  PeelStartPred->getTerminator()->setSuccessor(0, PeelEndSucc);

  // Attach between Exit and Exit Succ
  Exit->getTerminator()->setSuccessor(0, PeelStart);
  PeelEnd->getTerminator()->setSuccessor(0, ExitSucc);

  // Next peel should be attached after the current peel so return peel end
  return PeelEnd;
}

void create_qnm1::extractPeels() {
  llvm::DominatorTree &DT = getAnalysis<llvm::DominatorTreeWrapperPass>().getDomTree();
  llvm::LoopInfo &LI = getAnalysis<llvm::LoopInfoWrapperPass>().getLoopInfo();
  llvm::Loop* FirstLoop = *LI.rbegin();
  assert(FirstLoop && "Unable to locate the first loop in the program");
  llvm::BasicBlock* PeelStart = peeled_blocks_map.at(FirstLoop)[0];
  assert(PeelStart && "First peeled block of the loop not found");
  llvm::BasicBlock *PreHeader = FirstLoop->getLoopPreheader();
  assert(PreHeader && "PreHeader is NULL");
  llvm::BasicBlock* PreHeaderPred = PreHeader->getUniquePredecessor();
  assert(PreHeaderPred && "Predecessor of the PreHeader is NULL");
  /*
  llvm::Instruction *icmp = NULL;
  if (auto bi = llvm::dyn_cast<llvm::BranchInst>(PreHeaderPred->getTerminator()))
    if(bi->isConditional())
      icmp = llvm::dyn_cast<llvm::Instruction>(bi->getCondition());
  llvm::BasicBlock *BranchOutBlock = llvm::SplitBlock(PreHeaderPred, icmp, &DT, &LI);
  */
  llvm::BasicBlock *BranchOutBlock = llvm::SplitBlock(PreHeaderPred, PreHeaderPred->getTerminator(), &DT, &LI);
  // Detach the loops and directly jump to peels
  PeelStart->getUniquePredecessor()->getTerminator()->setSuccessor(0, NULL);
  BranchOutBlock->getUniquePredecessor()->getTerminator()->setSuccessor(0, PeelStart);

  llvm::Loop* LastLoop = *LI.begin();
  assert(LastLoop && "Unable to locate the Last loop in the program");
  llvm::BasicBlock* PeelEnd = peeled_blocks_map.at(LastLoop)[2];
  assert(PeelEnd && "Final peeled block of the last loop not found");
  llvm::BasicBlock* PeelEndSuccessor = PeelEnd->getUniqueSuccessor();
  assert(PeelEndSuccessor && "Successor of the last peeled block is NULL");
  for (llvm::BasicBlock *pb : llvm::predecessors(PeelEndSuccessor)) {
    if(pb == PeelEnd) continue;
    llvm::Instruction* term = pb->getTerminator();
    if (llvm::BranchInst* bi = llvm::dyn_cast<llvm::BranchInst>(term)) {
      llvm::BasicBlock* pointto = NULL;
      int pointto_index=-1;
      const int NumS = bi->getNumSuccessors();
      for(int i=0; i<NumS; i++) {
        llvm::BasicBlock* s = bi->getSuccessor(i);
        if(s == PeelEndSuccessor) {
          pointto_index = i;
        } else {
          pointto = s;
        }
      }
      if(pointto_index >= 0)
        bi->setSuccessor(pointto_index, pointto);
    }
  }

}

// Remove stores that are not needed in the peel
void create_qnm1::removeNonLoopBlockStores() {
  auto ld = ld_map.at(NULL);
  if(!ld) tiler_error("Peeling", "Unpopulated loop data map");
  for( auto b : ld->getCurrentBlocks() ) {
    if( b == NULL ) continue;
    if(find(all_peeled_blocks.begin(), all_peeled_blocks.end(), b)
       != all_peeled_blocks.end())
      continue; // Don't process the peeled blocks
    for( auto it = b->begin(), e = b->end(); it != e; ) {
      llvm::Instruction *I = &(*it);
      ++it;  // NOTE: If this increment is moved in the for statement it crashes!
      if(llvm::StoreInst* store = llvm::dyn_cast<llvm::StoreInst>(I)) {
        if(N == store->getOperand(1)) continue;
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
      } else {}
    }
  }
}

llvm::StringRef create_qnm1::getPassName() const {
  return "Generates the CFG of Q_{N-1}";
}

void create_qnm1::getAnalysisUsage(llvm::AnalysisUsage &au) const {
  au.addRequired<llvm::LoopInfoWrapperPass>();
  au.addRequired<llvm::DominatorTreeWrapperPass>();
  au.addRequired<llvm::ScalarEvolutionWrapperPass>();
  au.addRequired<llvm::AssumptionCacheTracker>();
}
