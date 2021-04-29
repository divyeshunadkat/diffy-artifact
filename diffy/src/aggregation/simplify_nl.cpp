#include "simplify_nl.h"

char simplify_nl::ID = 0;

simplify_nl::simplify_nl(z3::context& z3_,
                         options& o_,
                         value_expr_map& def_map_,
                         std::map<llvm::Loop*, loopdata*>& ldm,
                         name_map& lMap,
                         std::map<std::string, llvm::Value*>& evMap,
                         std::unique_ptr<llvm::Module>& m,
                         glb_model& g_model_, llvm::Value* N_o
                         )
  : llvm::FunctionPass(ID)
  , z3_ctx(z3_)
  , o(o_)
  , ld_map(ldm)
  , module(m)
  , g_model(g_model_)
{
  N = N_o;
}

simplify_nl::~simplify_nl() {}

bool simplify_nl::runOnFunction( llvm::Function &f ) {
  if(f.getName() != o.funcName)
    return false;

  target_f = &f;

  // Get entry block
  entry_bb = &target_f->getEntryBlock();

  SingularNRef = getSingularNRef(N, entry_bb);
  if(SingularNRef == NULL) tiler_error("Simplify", "SingularNRef is NULL");

  return process_nl();
}

bool simplify_nl::process_nl() {
  bool retValue = false;
  llvm::DominatorTree &DT = getAnalysis<llvm::DominatorTreeWrapperPass>().getDomTree();
  llvm::LoopInfo &LI = getAnalysis<llvm::LoopInfoWrapperPass>().getLoopInfo();
  llvm::ScalarEvolution &SE = getAnalysis<llvm::ScalarEvolutionWrapperPass>().getSE();

  for (auto I = LI.rbegin(), E = LI.rend(); I != E; ++I) {
    llvm::Loop* L = *I;
    loopdata* ld = ld_map.at(L);
    int slc = 0;
    for (llvm::Loop *SL : L->getSubLoops()) {
      loopdata* sub_ld = ld_map.at(SL);
      bool is_in_scope = true;
      if(slc == 0) slc++;
      else is_in_scope = false;
      if (!SL->getExitingBlock() || !SL->getUniqueExitBlock())
        is_in_scope = false;
      if(check_conditional_in_loopbody(SL))
        is_in_scope = false;

      auto writ = sub_ld->arrWrite.begin();
      std::list<llvm::Value*> vl = writ->second;
      // Mark store expr in sub_l
      if(++writ != sub_ld->arrWrite.end()) is_in_scope = false;
      if(vl.size() != 1) is_in_scope = false;
      // Mark store expr in l
      llvm::StoreInst* l_st = NULL;
      llvm::AllocaInst* l_st_arr = NULL;
      llvm::Value* l_st_idx = NULL;
      if(ld->arrWrite.begin() != ld->arrWrite.end()) {
        auto owrit = ld->arrWrite.begin();
        std::list<llvm::Value*> ovl = owrit->second;
        if(ovl.size() != 1) is_in_scope = false;
        l_st = llvm::dyn_cast<llvm::StoreInst>(*(ovl.begin()));
        l_st_arr = getAlloca(l_st);
        l_st_idx = getIdx(l_st);
        if(auto cast = llvm::dyn_cast<llvm::CastInst>(l_st_idx))
          l_st_idx = cast->getOperand(0);
      }
      // Get store in sub_l
      auto store = llvm::dyn_cast<llvm::StoreInst>(*(vl.begin()));
      auto st_addr = store->getOperand(1);
      llvm::BitCastInst* st_addr_bc = NULL;
      if( (st_addr_bc = llvm::dyn_cast<llvm::BitCastInst>(st_addr)) )
        st_addr = st_addr_bc->getOperand(0);
      auto st_elemPtr = llvm::dyn_cast<llvm::GetElementPtrInst>(st_addr);
      auto st_arr = llvm::dyn_cast<llvm::AllocaInst>(st_elemPtr->getPointerOperand());
      if(l_st != NULL && l_st_arr != NULL && l_st_arr != st_arr) is_in_scope = false;
      auto st_idx = st_elemPtr->getOperand(1);
      if(st_elemPtr->getNumIndices() == 2) st_idx = st_elemPtr->getOperand(2);
      if(auto cast = llvm::dyn_cast<llvm::CastInst>(st_idx))
        st_idx = cast->getOperand(0);

      // Writes at IV and not to other indices
      if(!st_arr->isStaticAlloca())
        if(!(st_idx == ld->ctr || st_idx == sub_ld->ctr)) is_in_scope = false;
      if(l_st != NULL && l_st_arr != NULL && !l_st_arr->isStaticAlloca())
        if(l_st_idx != ld->ctr) is_in_scope = false;

      // Compute if same arr read and updated
      llvm::BinaryOperator* outer_op = NULL;
      llvm::Value* rec_term = NULL;
      llvm::Value* load = NULL;
      if(auto bop = llvm::dyn_cast<llvm::BinaryOperator>(store->getOperand(0)) ) {
        assert( bop );
        outer_op = bop;
        bool rec_arr = false;
        auto op0 = bop->getOperand( 0 );
        auto op1 = bop->getOperand( 1 );
        if(llvm::isa<llvm::LoadInst>(op0)) {
          auto op0_load = llvm::dyn_cast<llvm::LoadInst>(op0);
          auto op0_l_arr = getAlloca(op0_load);
          if(op0_l_arr == st_arr) {
            auto l_idx = getIdx(op0_load);
            if(auto cast = llvm::dyn_cast<llvm::CastInst>(l_idx))
              l_idx = cast->getOperand(0);
            if(l_idx == st_idx) {
              rec_arr = true;
              load = op0_load;
            }
          } else rec_term = op0;
        }  else rec_term = op0;
        if(llvm::isa<llvm::LoadInst>(op1) && !rec_arr) {
          auto op1_load = llvm::dyn_cast<llvm::LoadInst>(op1);
          auto op1_l_arr = getAlloca(op1_load);
          if(op1_l_arr == st_arr) {
            auto l_idx = getIdx(op1_load);
            if(auto cast = llvm::dyn_cast<llvm::CastInst>(l_idx))
              l_idx = cast->getOperand(0);
            if(l_idx == st_idx) {
              rec_arr = true;
              load = op1_load;
            }
          } else rec_term = op1;
        } else rec_term = op1;
        if(!rec_arr)
          is_in_scope = false;
      } else is_in_scope = false;
      if(rec_term == NULL || load == NULL || outer_op == NULL) is_in_scope = false;
      if(rec_term != NULL && rec_term != ld->ctr) is_in_scope = check_rt_scope(rec_term, L);
      if(is_in_scope && llvm::isa<llvm::LoadInst>(rec_term)) {
        auto r_load = llvm::dyn_cast<llvm::LoadInst>(rec_term);
        auto r_l_arr = getAlloca(r_load);
        if(r_l_arr == st_arr) is_in_scope = false;
      } else {}

      llvm::BinaryOperator* l_outer_op = NULL;
      llvm::Value* l_rec_term = NULL;
      llvm::Value* l_load = NULL;
      if(l_st != NULL) {
        if(auto bop = llvm::dyn_cast<llvm::BinaryOperator>(l_st->getOperand(0)) ) {
          assert( bop );
          l_outer_op = bop;
          bool l_rec_arr = false;
          auto op0 = bop->getOperand( 0 );
          auto op1 = bop->getOperand( 1 );
          if(llvm::isa<llvm::LoadInst>(op0)) {
            auto op0_load = llvm::dyn_cast<llvm::LoadInst>(op0);
            auto op0_l_arr = getAlloca(op0_load);
            if(op0_l_arr == l_st_arr) {
              auto l_idx = getIdx(op0_load);
              if(auto cast = llvm::dyn_cast<llvm::CastInst>(l_idx))
                l_idx = cast->getOperand(0);
              if(l_idx == l_st_idx) {
                l_rec_arr = true;
                l_load = op0_load;
              }
            } else l_rec_term = op0;
          }  else l_rec_term = op0;
          if(llvm::isa<llvm::LoadInst>(op1) && !l_rec_arr) {
            auto op1_load = llvm::dyn_cast<llvm::LoadInst>(op1);
            auto op1_l_arr = getAlloca(op1_load);
            if(op1_l_arr == l_st_arr) {
              auto l_idx = getIdx(op1_load);
              if(auto cast = llvm::dyn_cast<llvm::CastInst>(l_idx))
                l_idx = cast->getOperand(0);
              if(l_idx == l_st_idx) {
                l_rec_arr = true;
                l_load = op1_load;
              }
            } else l_rec_term = op1;
          } else l_rec_term = op1;
          if(!l_rec_arr)
            is_in_scope = false;
        } else is_in_scope = false;
        if(l_rec_term == NULL || l_load == NULL || l_outer_op == NULL) is_in_scope = false;
      }

      // Identify insertion point
      llvm::Instruction* insertion_pt = NULL;
      llvm::Value* Z = llvm::ConstantInt::get(llvm::Type::getInt32Ty(o.globalContext), 0);
      llvm::Instruction* noop = llvm::BinaryOperator::Create(llvm::Instruction::Add, Z, Z, "no-op");
      if(l_st != NULL) noop->insertAfter(l_st);
      else noop->insertBefore(SL->getUniqueExitBlock()->getTerminator());
      insertion_pt = noop;

      llvm::Instruction* NRef = llvm::dyn_cast<llvm::Instruction>(SingularNRef);
      llvm::Instruction* counter = llvm::dyn_cast<llvm::Instruction>(ld->ctr);

      // Gathering terms
      llvm::Value* l_cloneRecTerm = NULL;
      if(l_st != NULL && l_rec_term != NULL) {
        if(auto l_rec_term_inst = llvm::dyn_cast<llvm::Instruction>(l_rec_term)) {
          l_cloneRecTerm = cloneExpr(l_rec_term_inst, insertion_pt, NRef, counter);
        } else l_cloneRecTerm = l_rec_term;
      }
      llvm::Value* cloneRecTerm = NULL;
      if(auto rec_term_inst = llvm::dyn_cast<llvm::Instruction>(rec_term)) {
        cloneRecTerm = cloneExpr(rec_term_inst, insertion_pt, NRef, counter);
        if(cloneRecTerm == NULL) cloneRecTerm = rec_term;
      } else cloneRecTerm = rec_term;
      llvm::Value* clInit = NULL;
      llvm::Value* clExit = NULL;
      if(auto iv = llvm::dyn_cast<llvm::Instruction>(sub_ld->initValue)) {
        clInit = cloneExpr(iv, insertion_pt, NRef, counter);
        if(clInit == NULL) clInit = sub_ld->initValue;
      } else
        clInit = sub_ld->initValue;
      if(auto ev = llvm::dyn_cast<llvm::Instruction>(sub_ld->exitValue)) {
        clExit = cloneExpr(ev, insertion_pt, NRef, counter);
        if(clExit == NULL) clExit = sub_ld->exitValue;
      } else
        clExit = sub_ld->exitValue;
      if(!sub_ld->isStrictBound) {
        llvm::Value* step = llvm::ConstantInt::get(llvm::Type::getInt32Ty(o.globalContext), sub_ld->stepCnt);
        if(sub_ld->stepCnt > 0)
          clExit = llvm::BinaryOperator::Create(llvm::Instruction::Add, clExit, step, "step");
        else
          clExit = llvm::BinaryOperator::Create(llvm::Instruction::Sub, clExit, step, "step");
        llvm::Instruction* new_ins = llvm::dyn_cast<llvm::Instruction>(clExit);
        new_ins->insertBefore(insertion_pt);
      }
      // Compute multiple
      llvm::IRBuilder<> b(insertion_pt);
      llvm::Value* rec_mult = NULL;
      if(st_arr->isStaticAlloca() || st_idx == ld->ctr) {
        if(sub_ld->stepCnt > 0)
          if(!(sub_ld->initBound.is_numeral()) || get_numeral_int(sub_ld->initBound) > 0)
            rec_mult = b.CreateSub(clExit, clInit);
          else
            rec_mult = clExit;
        else
          if(!(sub_ld->exitBound.is_numeral()) || get_numeral_int(sub_ld->exitBound) > 0)
            rec_mult = b.CreateSub(clInit, clExit);
          else
            rec_mult = clInit;
      } else {
        if(rec_term == ld->ctr) is_in_scope = false;
        if(sub_ld->stepCnt > 0)
          if(!(sub_ld->initBound.is_numeral()))
            if(clInit == ld->ctr && clExit == ld->exitValue) {
              llvm::Value* One = llvm::ConstantInt::get(llvm::Type::getInt32Ty(o.globalContext), 1);
              rec_mult = b.CreateAdd(clInit, One);
            } else rec_mult = clInit;
          else if(get_numeral_int(sub_ld->initBound) > 0)
            rec_mult = b.CreateSub(clExit, clInit);
          else
            if(clExit == ld->exitValue)
              rec_mult = clExit;
            else {
              llvm::Value* One = llvm::ConstantInt::get(llvm::Type::getInt32Ty(o.globalContext), 1);
              llvm::Value* sub = b.CreateSub(ld->exitValue, clExit);
              rec_mult = b.CreateAdd(sub, One);
            }
        else
          if(!(sub_ld->exitBound.is_numeral()))
            if(clExit == ld->ctr && clInit == ld->initValue) {
              llvm::Value* One = llvm::ConstantInt::get(llvm::Type::getInt32Ty(o.globalContext), 1);
              rec_mult = b.CreateAdd(clExit, One);
            } else rec_mult = clExit;
          else if(get_numeral_int(sub_ld->exitBound) > 0)
            rec_mult = b.CreateSub(clInit, clExit);
          else
            rec_mult = clInit;
      }
      // if(!is_in_scope) tiler_error("diff_preds::", o.toolNameCaps + "_VERIFICATION_UNKNOWN");
      if(!is_in_scope) { std::cout << "\n" + o.toolNameCaps + "_VERIFICATION_UNKNOWN\n\n";  exit(1); }
      auto new_rec_term = b.CreateMul(rec_mult, cloneRecTerm);
      llvm::Value* new_idx = NULL;
      if(st_arr->isStaticAlloca()) new_idx = st_idx;
      else new_idx = ld->ctr;
      llvm::Type *i32_ptr_type = llvm::IntegerType::getInt32PtrTy(store->getContext());
      llvm::GetElementPtrInst* ld_new_elem_ptr = llvm::dyn_cast<llvm::GetElementPtrInst>
        (b.CreateGEP(st_arr, new_idx));
      llvm::BitCastInst* ld_new_elem_ptr_bc = NULL;
      if(st_arr->isStaticAlloca()) {
        ld_new_elem_ptr_bc = llvm::dyn_cast<llvm::BitCastInst>
          (b.CreateBitCast(ld_new_elem_ptr, i32_ptr_type));
      } else {}
      llvm::LoadInst* new_load = NULL;
      if(ld_new_elem_ptr_bc) {
        new_load = llvm::dyn_cast<llvm::LoadInst> (b.CreateLoad(ld_new_elem_ptr_bc));
      } else {
        new_load = llvm::dyn_cast<llvm::LoadInst> (b.CreateLoad(ld_new_elem_ptr));
      }


      llvm::Value* val = NULL;
      unsigned op = outer_op->getOpcode();
      switch( op ) {
      case llvm::Instruction::Add  : val = b.CreateAdd(new_load, new_rec_term); break;
      case llvm::Instruction::Sub  : val = b.CreateSub(new_load, new_rec_term); break;
      case llvm::Instruction::Mul  : val = b.CreateMul(new_load, new_rec_term); break;
      case llvm::Instruction::SDiv : val = b.CreateSDiv(new_load, new_rec_term); break;
      case llvm::Instruction::UDiv : val = b.CreateUDiv(new_load, new_rec_term); break;
      default: tiler_error("Simplify", "Unsupported operator");
      }
      if(l_st != NULL && l_cloneRecTerm != NULL) {
        unsigned op = l_outer_op->getOpcode();
        switch( op ) {
        case llvm::Instruction::Add  : val = b.CreateAdd(val, l_cloneRecTerm); break;
        case llvm::Instruction::Sub  : val = b.CreateSub(val, l_cloneRecTerm); break;
        case llvm::Instruction::Mul  : val = b.CreateMul(val, l_cloneRecTerm); break;
        case llvm::Instruction::SDiv : val = b.CreateSDiv(val, l_cloneRecTerm); break;
        case llvm::Instruction::UDiv : val = b.CreateUDiv(val, l_cloneRecTerm); break;
        default: tiler_error("Simplify", "Unsupported operator");
        }
      }
      llvm::Value* st_new_elem_ptr = llvm::dyn_cast<llvm::GetElementPtrInst>
        (b.CreateInBoundsGEP(st_arr, new_idx));
      if(st_arr->isStaticAlloca()) {
        st_new_elem_ptr = llvm::dyn_cast<llvm::BitCastInst>
          (b.CreateBitCast(st_new_elem_ptr, i32_ptr_type));
      } else {}
      b.CreateStore(val, st_new_elem_ptr);
      if(is_in_scope) {
        if(l_st != NULL) eraseExpr(l_st, NRef);
        eraseExpr(store, NRef);
        deleteDeadLoop(SL, &DT, &SE, &LI);
        retValue = true;
      }
    }
  }

  return retValue;
}

bool simplify_nl::check_rt_scope(llvm::Value* rt, llvm::Loop* L) {
  if(rt == NULL || L == NULL) return false;
  if(llvm::isa<llvm::ConstantInt>(rt)) {
    return true;
  } else if (llvm::isa<llvm::PHINode>(rt)) {
    return false;
  } else if(auto bop = llvm::dyn_cast<llvm::BinaryOperator>(rt) ) {
    bool b1 = check_rt_scope(bop->getOperand(0), L);
    bool b2 = check_rt_scope(bop->getOperand(1), L);
    return b1 && b2;
  } else if(auto cast = llvm::dyn_cast<llvm::CastInst>(rt)) {
    return check_rt_scope(cast->getOperand(0), L);
  } else if(llvm::isa<llvm::LoadInst>(rt)) {
    auto load = llvm::dyn_cast<llvm::LoadInst>(rt);
    if(load == SingularNRef) return true;
    auto arr = getAlloca(load);
    if(arr != NULL && arr->isStaticAlloca()) return true;
    bool rt_scope = true;
    bool flag = false;
    llvm::LoopInfo &LI = getAnalysis<llvm::LoopInfoWrapperPass>().getLoopInfo();
    for (auto I = LI.begin(), E = LI.end(); I != E; ++I) {
      llvm::Loop* TL = *I;
      if(TL != L && !flag) continue;
      if(TL == L) { flag = true; continue; }
      loopdata* ld = ld_map.at(TL);
      for (auto wbit = ld->arrWrite.begin(); wbit!=ld->arrWrite.end(); wbit++) {
        auto wr_arry = wbit->first;
        auto wr_arry_alloca = llvm::dyn_cast<llvm::AllocaInst>(wr_arry);
        if(arr != wr_arry_alloca) continue;
        std::list<llvm::Value*> vl = wbit->second;
        for(auto v : vl) {
          auto store = llvm::dyn_cast<llvm::StoreInst>(v);
          rt_scope = rt_scope && check_rt_scope(store->getOperand(0), TL);
        }
      }
    }
    return rt_scope;
  } else {}
  return false;
}

llvm::StringRef simplify_nl::getPassName() const {
  return "Simplify NL";
}

void simplify_nl::getAnalysisUsage(llvm::AnalysisUsage &au) const {
  au.addRequired<llvm::LoopInfoWrapperPass>();
  au.addRequired<llvm::DominatorTreeWrapperPass>();
  au.addRequired<llvm::ScalarEvolutionWrapperPass>();
}
