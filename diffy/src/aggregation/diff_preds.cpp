#include "diff_preds.h"
#include "llvm/Transforms/Utils/ValueMapper.h"
#include "llvm/Transforms/Utils/Cloning.h"

char diff_preds::ID = 0;

diff_preds::diff_preds(std::unique_ptr<llvm::Module>& m,
                       z3::context& z3_,
                       options& o_,
                       value_expr_map& def_map_,
                       std::map<llvm::Loop*, loopdata*>& ldm,
                       name_map& lMap,
                       std::map<std::string, llvm::Value*>& evMap,
                       glb_model& g_model_,
                       std::map<llvm::Function*, std::map<const llvm::Value*,
                       const llvm::Value*>>& v2v_map,
                       int& unsup)
  : llvm::FunctionPass(ID)
  , module(m)
  , z3_ctx(z3_)
  , o(o_)
  , def_map(def_map_)
  , ld_map(ldm)
  , localNameMap(lMap)
  , exprValMap(evMap)
  , g_model(g_model_)
  , fn_v2v_map(v2v_map)
  , unsupported(unsup)
{
  ir2e = new irtoz3expr_index(z3_ctx, def_map, lMap, evMap, g_model);
}

diff_preds::~diff_preds() {}

bool diff_preds::runOnFunction( llvm::Function &f ) {
  if(f.getName() != o.funcName)
    return false;

  peel_function(&f);
  return true;
}

void diff_preds::
peel_function(llvm::Function* diff_fun) {

  diff_f = diff_fun;
  auto ld = ld_map.at(NULL);
  std::vector< loopdata* >& sub_loops = ld->childHeads;

  // Peel the function only if loops are present
  if(sub_loops.empty()) return;

  // Get entry block
  entry_bb = &diff_fun->getEntryBlock();

  // Identify the induction parameter N from the program
  N = find_ind_param_N(module, diff_f, entry_bb);

  // Check and abort if induction parameter is present in the index expression
  check_indu_param_in_index(diff_f, N, unsupported);

  // Create N-1 instruction and add before terminator of entry
  SingularNRef = getSingularNRef(N, entry_bb);
  if(SingularNRef == NULL) tiler_error("diff_preds::", "SingularNRef is NULL");
  NS = generateNSVal(N, SingularNRef, entry_bb);
  if(NS==NULL) tiler_error("diff_preds::", "NS is NULL");

  unsigned gap_num = 0;
  for( auto b : ld->getCurrentBlocks() ) {
    if( b == NULL ) {
      //processing loops in the function
      auto sub_loop = sub_loops[gap_num++];
      peel_loop( sub_loop->loop, sub_loop );
    }else{
      //processing the function blocks outside loops
      peel_outer_block(b);
    }
  }
  assert( gap_num == ld->childHeads.size() );
}

// Peel an iteration of the loop
void diff_preds::
peel_loop(llvm::Loop* L, loopdata* ld) {
  loopList.push_back(L);
  identify_interferences(L, ld);

  bb* preheader = L->getLoopPreheader();

  // Remove the back edge
  remove_loop_back_edge(L);

  std::list<const llvm::Value*> val_l;
  val_l.push_back(N);
  val_l.push_back(ld->ctr);
  std::map<llvm::Value*,std::list<llvm::Value*>>::iterator wbit = ld->arrWrite.begin();
  std::map<llvm::Value*,std::list<llvm::Value*>>::iterator weit = ld->arrWrite.end();
  for (; wbit!=weit; wbit++) {
    auto wr_arry = wbit->first;
    auto wr_arry_alloca = llvm::dyn_cast<llvm::AllocaInst>(wr_arry);
    std::list<llvm::Value*> vl = wbit->second;
    for(auto v : vl) {
      auto store = llvm::dyn_cast<llvm::StoreInst>(v);
      llvm::Value* st_val = store->getOperand(0);
      
      if(isValueInSubExpr(N, st_val) && onlyValuesInSubExpr(val_l, st_val)) {
        // if(isValueInSubExpr(ld->ctr, st_val)) {
        //   std::cout << "\n\nLoop counter in the stored value\n\n";
        //   std::cout << "\n" + o.toolNameCaps + "_VERIFICATION_UNKNOWN\n";
        //   exit(1);
        // }
        z3::expr diff_e = compute_diff_expr(st_val);
        z3::expr N_expr = ir2e->getZ3Expr(N).front();
        inplace_substitute( diff_e, ld->ctrZ3Expr, N_expr );
        llvm::Value* diff_val = get_val_for_diff(diff_e, preheader, NULL);
        
        add_diff_val(wr_arry_alloca, diff_val);
        // add_diff_expr(wr_arry_alloca, diff_e);
        /*
        if(wr_arry_alloca->isStaticAlloca()) {
          add_const_aggr_val(wr_arry_alloca, diff_val);
        } else {
          add_diff_val(wr_arry_alloca, diff_val);
          add_diff_expr(wr_arry_alloca, diff_e);
        }
        */
      } else {}
    }

    for(auto v : vl) {
      auto store = llvm::dyn_cast<llvm::StoreInst>(v);
      llvm::Value* st_val = store->getOperand(0);
      
      if(isValueInSubExpr(N, st_val) && onlyValuesInSubExpr(val_l, st_val)) {
        // No record needed
      } else if (llvm::dyn_cast<llvm::ConstantInt>(store->getOperand(0))) {
        // Do nothing about constant assignments to arrays or scalars
      } else {
        std::list<llvm::Value*> load_lst;
        llvm::Value* rec_t = collect_loads_recs(store->getOperand(0), N, load_lst);
        append_diff_invs(wr_arry_alloca, store, load_lst, rec_t, L, ld);
        additional_record(wr_arry_alloca, load_lst, rec_t);
      }
    }
  }

  // Replace i with N-1
  ld->VMap[ld->ctr] = NS;
  llvm::remapInstructionsInBlocks(to_small_vec(ld->blocks), ld->VMap);
  ld->VMap.clear();
  // Remove phi inst of i
  if(llvm::Instruction *I = llvm::dyn_cast<llvm::Instruction>(ld->ctr)) {
    I->eraseFromParent();
  }

}

void diff_preds::
append_diff_invs(llvm::AllocaInst* wr_arry_alloca, llvm::StoreInst* store,
                 std::list<llvm::Value*>& load_lst, llvm::Value* rec_t,
                 llvm::Loop* L, loopdata* ld) {
  std::list<llvm::Value*> tmp_diff_list;
  llvm::Type *i32_ptr_type = llvm::IntegerType::getInt32PtrTy(o.globalContext);
  std::list<const llvm::Value*> val_l;
  val_l.push_back(N);
  val_l.push_back(ld->ctr);
  llvm::BasicBlock* pre_header = ld->loop->getLoopPreheader();
  llvm::Instruction* term = pre_header->getTerminator();
  if(!term) tiler_error("diff_preds::", "Preheader block does not have unique term inst");

  llvm::Instruction* insertion_pt=NULL;
  if(wr_arry_alloca->isStaticAlloca()) {
    insertion_pt = term;
  } else {
    llvm::Value* Z = llvm::ConstantInt::get(llvm::Type::getInt32Ty(o.globalContext), 0);
    llvm::Instruction* I = llvm::BinaryOperator::Create(llvm::Instruction::Add, Z, Z, "no-op");
    I->insertAfter(store);
    insertion_pt = I;
  }

  llvm::IRBuilder<> b(insertion_pt);
  auto st_addr = store->getOperand(1);
  llvm::BitCastInst* st_addr_bc = NULL;
  if( (st_addr_bc = llvm::dyn_cast<llvm::BitCastInst>(st_addr)) )
    st_addr = st_addr_bc->getOperand(0);
  auto st_elemPtr = llvm::dyn_cast<llvm::GetElementPtrInst>(st_addr);
  auto idx = st_elemPtr->getOperand(1);
  if(st_elemPtr->getNumIndices() == 2) idx = st_elemPtr->getOperand(2);

  llvm::GetElementPtrInst* new_elem_ptr = llvm::dyn_cast<llvm::GetElementPtrInst>
    (b.CreateGEP(wr_arry_alloca, idx));
  llvm::BitCastInst* new_elem_ptr_bc = NULL;
  if(wr_arry_alloca->isStaticAlloca()) {
    new_elem_ptr_bc = llvm::dyn_cast<llvm::BitCastInst>
      (b.CreateBitCast(new_elem_ptr, i32_ptr_type));
  } else {}
  llvm::LoadInst* new_load = NULL;
  if(new_elem_ptr_bc) {
   new_load = llvm::dyn_cast<llvm::LoadInst> (b.CreateLoad(new_elem_ptr_bc));
  } else {
   new_load = llvm::dyn_cast<llvm::LoadInst> (b.CreateLoad(new_elem_ptr));
  }

  llvm::Value* val = new_load;
  bool at_least_one_diff_val = false;

  for(auto v : load_lst) {
    auto rd_load = llvm::dyn_cast<llvm::LoadInst>(v);
    auto load_arry_alloca = getAlloca(rd_load);
    
    /*
    std::list<llvm::Value*>& const_aggr_l = const_aggr_map[load_arry_alloca];
    for(llvm::Value* c_aggr_val : const_aggr_l) {
      if(wr_arry_alloca->isStaticAlloca()) {
        if(wr_arry_alloca == load_arry_alloca) continue;
        if(load_arry_alloca->isStaticAlloca()) {
          c_aggr_val = b.CreateMul(c_aggr_val, NS);
          val = b.CreateAdd(val, c_aggr_val);
          at_least_one_diff_val = true;
        }
      } else {
        val = b.CreateAdd(val, c_aggr_val);
        at_least_one_diff_val = true;
      }
      if(!wr_arry_alloca->isStaticAlloca())
        tmp_diff_list.push_back(c_aggr_val);
      else
        add_const_aggr_val(wr_arry_alloca, c_aggr_val);
    }
    */

    std::list<llvm::Value*>& diff_val_l = diff_val_map[load_arry_alloca];
    for(llvm::Value* diff_val : diff_val_l) {
      if(wr_arry_alloca->isStaticAlloca()) {
        if(wr_arry_alloca == load_arry_alloca) continue;
        if(!load_arry_alloca->isStaticAlloca()) {
          diff_val = b.CreateMul(diff_val, NS);
        } else {        }
        val = b.CreateAdd(val, diff_val);
        at_least_one_diff_val = true;
      } else {
        if(wr_arry_alloca == load_arry_alloca) {
          val = b.CreateAdd(val, diff_val);
          at_least_one_diff_val = true;
        }
      }
      
      if(wr_arry_alloca != load_arry_alloca)
        tmp_diff_list.push_back(diff_val);
      /*
      if(!wr_arry_alloca->isStaticAlloca()) {
        if(wr_arry_alloca != load_arry_alloca)
          tmp_diff_list.push_back(diff_val);
      } else {
        add_const_aggr_val(wr_arry_alloca, diff_val);
      }
      */
    }
    if ( wr_arry_alloca->isStaticAlloca() && wr_arry_alloca == load_arry_alloca
         && rec_t &&  isValueInSubExpr(N, rec_t) && onlyValuesInSubExpr(val_l, rec_t)) {
      z3::expr diff_e = compute_diff_expr(rec_t);
      z3::expr N_expr = ir2e->getZ3Expr(N).front();
      z3::expr Nm1_expr = N_expr - 1;
      inplace_substitute( diff_e, ld->ctrZ3Expr, N_expr );
      llvm::Value* diff_val = get_val_for_diff(diff_e, NULL, insertion_pt);
      // add_diff_val(wr_arry_alloca, diff_val);
      diff_val = b.CreateMul(diff_val, NS);
      // add_const_aggr_val(wr_arry_alloca, diff_val);
      add_diff_val(wr_arry_alloca, diff_val);

      // add_diff_expr(wr_arry_alloca, Nm1_expr*diff_e);
      val = b.CreateAdd(val, diff_val);
      at_least_one_diff_val = true;
    } else {}
  }

  if ( !wr_arry_alloca->isStaticAlloca() && rec_t && isValueInSubExpr(N, rec_t)
       && onlyValuesInSubExpr(val_l, rec_t)) {
    z3::expr diff_e = compute_diff_expr(rec_t);
    z3::expr N_expr = ir2e->getZ3Expr(N).front();
    inplace_substitute( diff_e, ld->ctrZ3Expr, N_expr );
    llvm::Value* diff_val = get_val_for_diff(diff_e, NULL, insertion_pt);
    add_diff_val(wr_arry_alloca, diff_val);
    // add_diff_expr(wr_arry_alloca, diff_e);
  } else {}

  if(at_least_one_diff_val) {
    auto st_new_elem_ptr = llvm::dyn_cast<llvm::GetElementPtrInst>
      (b.CreateGEP(wr_arry_alloca, idx));
    if(wr_arry_alloca->isStaticAlloca()) {
      auto st_new_elem_ptr_bc = llvm::dyn_cast<llvm::BitCastInst>
        (b.CreateBitCast(st_new_elem_ptr, i32_ptr_type));
      b.CreateStore(val, st_new_elem_ptr_bc);
    } else {
      b.CreateStore(val, st_new_elem_ptr);
    }
  } else {
    new_load->eraseFromParent();
    if(new_elem_ptr_bc) new_elem_ptr_bc->eraseFromParent();
    new_elem_ptr->eraseFromParent();
  }

  for(auto v : tmp_diff_list)
    add_diff_val(wr_arry_alloca, v);
}

void diff_preds::
additional_record(llvm::AllocaInst* wr_arry_alloca,
                  std::list<llvm::Value*>& load_lst, llvm::Value* rec_t) {
  for(auto v : load_lst) {
    auto rd_load = llvm::dyn_cast<llvm::LoadInst>(v);
    auto load_arry_alloca = getAlloca(rd_load);
    if(wr_arry_alloca == load_arry_alloca) continue;
    if(wr_arry_alloca->isStaticAlloca() &&
       !load_arry_alloca->isStaticAlloca()) {
      add_diff_val(wr_arry_alloca, rd_load);
    } else {}
  }
  if(rec_t != NULL) {
      add_const_aggr_val(wr_arry_alloca, rec_t);
  }
}

// Remove stores that are not needed
// Add new statements needed while processing outer block
void diff_preds::
peel_outer_block(llvm::BasicBlock* bb) {
  llvm::Type *i32_ptr_type = llvm::IntegerType::getInt32PtrTy(bb->getContext());
  auto ld = ld_map.at(NULL);
  populate_arr_rd_wr(bb, ld->arrRead, ld->arrWrite);
  if(!ld->arrWrite.empty()) {
    std::map<llvm::Value*,std::list<llvm::Value*>>::iterator wbit = ld->arrWrite.begin();
    std::map<llvm::Value*,std::list<llvm::Value*>>::iterator weit = ld->arrWrite.end();
    for (; wbit!=weit; wbit++) {
      auto arry = wbit->first;
      llvm::AllocaInst *arry_alloca = llvm::dyn_cast<llvm::AllocaInst>(arry);
      std::list<llvm::Value*> vl = wbit->second;
      for(auto v : vl) {
        llvm::StoreInst* store = llvm::dyn_cast<llvm::StoreInst>(v);
        llvm::Value* st_val = store->getOperand(0);
        if(isValueInSubExpr(N, st_val) && onlyValueInSubExpr(N, st_val)) {
          z3::expr diff_e = compute_diff_expr(st_val);
          llvm::Value* diff_val = get_val_for_diff(diff_e, bb, store);
          if(arry_alloca->isStaticAlloca()) {
          llvm::AllocaInst* new_arry_alloca = create_new_alloca(arry_alloca);
          // Add the newly created alloca to the alloca_map obtained from the fn_v2v_map
          if(fn_v2v_map.count(diff_f) > 0) {
            std::map<const llvm::Value*, const llvm::Value*>& alloca_map = fn_v2v_map[diff_f];
            if(alloca_map.count(arry_alloca) > 0) {
              alloca_map[new_arry_alloca] = alloca_map[arry_alloca];
            } else {
              tiler_error("diff_preds::","Unable to map newly created alloca");
            }
          } else {
            tiler_error("diff_preds::","No value map found for the function under analysis");
          }

          llvm::IRBuilder<> b(store);
          auto elemPtr = getGEP(store);
          auto idx = getIdx(store);
          auto new_elem_ptr = llvm::dyn_cast<llvm::GetElementPtrInst>
            (b.CreateInBoundsGEP(new_arry_alloca, idx));
          auto new_elem_ptr_bc = b.CreateBitCast(new_elem_ptr, i32_ptr_type);
          llvm::LoadInst* load = llvm::dyn_cast<llvm::LoadInst>
            (b.CreateLoad(new_elem_ptr_bc));
          llvm::Value* val = b.CreateAdd(load, diff_val);
          b.CreateStore(val, elemPtr);
          
          //add_const_aggr_val(arry_alloca, diff_val);
          add_diff_val(arry_alloca, diff_val);

          store->eraseFromParent();
          } else {
            add_diff_val(arry_alloca, diff_val);
            store->eraseFromParent();
          }
        } else if (llvm::dyn_cast<llvm::ConstantInt>(store->getOperand(0))) {
          auto addr = store->getOperand(1);
          llvm::BitCastInst* addr_bc = NULL;
          if( (addr_bc = llvm::dyn_cast<llvm::BitCastInst>(addr)) )
            addr = addr_bc->getOperand(0);
          auto gep = llvm::dyn_cast<llvm::GetElementPtrInst>(addr);
          //          if(arry_alloca->isStaticAlloca()) {
            store->eraseFromParent();
            if(addr_bc != NULL) addr_bc->eraseFromParent();
            gep->eraseFromParent();
          //          } else {} // Do not remove the constant assignments for size N arrays
        } else if(auto load = llvm::dyn_cast<llvm::LoadInst>(store->getOperand(0))) {
          auto load_addr = load->getOperand(0);
          llvm::BitCastInst* load_addr_bc = NULL;
          if( (load_addr_bc = llvm::dyn_cast<llvm::BitCastInst>(load_addr)) )
            load_addr = load_addr_bc->getOperand(0);
          auto load_elemPtr = llvm::dyn_cast<llvm::GetElementPtrInst>(load_addr);
          auto load_arry = load_elemPtr->getPointerOperand();
          //  auto load_arry_alloca = llvm::dyn_cast<llvm::AllocaInst>(load_arry);
          auto st_addr = store->getOperand(1);
          llvm::BitCastInst* st_addr_bc = NULL;
          if( (st_addr_bc = llvm::dyn_cast<llvm::BitCastInst>(st_addr)) )
            st_addr = st_addr_bc->getOperand(0);
          auto st_elemPtr = llvm::dyn_cast<llvm::GetElementPtrInst>(st_addr);
          auto idx = st_elemPtr->getOperand(1);
          if(st_elemPtr->getNumIndices() == 2)
            idx = st_elemPtr->getOperand(2);
          llvm::IRBuilder<> b(store);

          std::list<llvm::Value*>& diff_val_l = diff_val_map[load_arry];
          llvm::Value* diff_val = NULL;
          for(llvm::Value* v : diff_val_l) {
            if(v==NULL) continue;
            diff_val = v;
          }
          if(diff_val != NULL) {
            llvm::Value* elemPtr_or_bcPtr = NULL;
            if(st_addr_bc) {
              elemPtr_or_bcPtr = st_addr_bc;
            } else {
              elemPtr_or_bcPtr = st_elemPtr;
            }
            llvm::LoadInst* new_load = llvm::dyn_cast<llvm::LoadInst>
              (b.CreateLoad(elemPtr_or_bcPtr));
            llvm::Value* val = b.CreateAdd(new_load, diff_val);
            auto new_elem_ptr = llvm::dyn_cast<llvm::GetElementPtrInst>
              (b.CreateInBoundsGEP(arry_alloca, idx));
            auto new_elem_ptr_bc = b.CreateBitCast(new_elem_ptr, i32_ptr_type);
            b.CreateStore(val, new_elem_ptr_bc);
            add_diff_val(arry_alloca, diff_val);
          }
          
          /*
          llvm::Value* elemPtr_or_bcPtr = NULL;
          if(st_addr_bc) elemPtr_or_bcPtr = st_addr_bc;
          else elemPtr_or_bcPtr = st_elemPtr;
          llvm::LoadInst* new_load = llvm::dyn_cast<llvm::LoadInst>
            (b.CreateLoad(elemPtr_or_bcPtr));
          llvm::Value* val = new_load;
          bool atleast_one_val = false;
          std::list<llvm::Value*>& diff_val_l = diff_val_map[load_arry];
          for(llvm::Value* diff_val : diff_val_l) {
            if(diff_val == NULL) continue;
            if(load_arry_alloca->isStaticAlloca()) {
              diff_val = b.CreateMul(diff_val, NS);
              val = b.CreateAdd(val, diff_val);
              if(arry_alloca->isStaticAlloca())
                add_const_aggr_val(arry_alloca, diff_val);
              else
                add_diff_val(arry_alloca, diff_val);
            }
            atleast_one_val = true;
          }
          std::list<llvm::Value*>& const_aggr_l = const_aggr_map[load_arry];
          for(llvm::Value* c_aggr_val : const_aggr_l) {
            if(c_aggr_val == NULL) continue;
            val = b.CreateAdd(val, c_aggr_val);
            if(arry_alloca->isStaticAlloca())
              add_const_aggr_val(arry_alloca, c_aggr_val);
            else
              add_diff_val(arry_alloca, c_aggr_val);
            atleast_one_val = true;
          }
          if(!atleast_one_val) {
            new_load->eraseFromParent();
          } else {
            auto new_elem_ptr = llvm::dyn_cast<llvm::GetElementPtrInst>
              (b.CreateInBoundsGEP(arry_alloca, idx));
            auto new_elem_ptr_bc = b.CreateBitCast(new_elem_ptr, i32_ptr_type);
            b.CreateStore(val, new_elem_ptr_bc);
          }
          */
          store->eraseFromParent();
          
          if(diff_val == NULL) {
          // if(!atleast_one_val) {
            if(st_addr_bc != NULL) st_addr_bc->eraseFromParent();
            st_elemPtr->eraseFromParent();
          }
          load->eraseFromParent();
          if(load_addr_bc != NULL) load_addr_bc->eraseFromParent();
          load_elemPtr->eraseFromParent();
        } else {
          std::cout << "Warning: These instructions are currently not supported";
        }
      }
    }
  } else {}
}

void diff_preds::add_diff_expr(llvm::AllocaInst* arry_alloca, z3::expr diff_expr) {
  std::list<z3::expr>& arr_diff_l = diff_expr_map[arry_alloca];
  arr_diff_l.push_back(diff_expr);
}

void diff_preds::add_diff_val(llvm::AllocaInst* arry_alloca, llvm::Value* local_diff_val) {
  std::list<llvm::Value*>& arr_local_diff_l = diff_val_map[arry_alloca];
  arr_local_diff_l.push_back(local_diff_val);
}

void diff_preds::add_const_aggr_val(llvm::AllocaInst* arry_alloca, llvm::Value* diff_val) {
    std::list<llvm::Value*>& const_aggr_l = const_aggr_map[arry_alloca];
    const_aggr_l.push_back(diff_val);
}

void diff_preds::identify_interferences(llvm::Loop *currL, loopdata* ld) {
  std::map<llvm::Value*,std::list<llvm::Value*>>::iterator rbit = ld->arrRead.begin();
  std::map<llvm::Value*,std::list<llvm::Value*>>::iterator reit = ld->arrRead.end();
  for (; rbit!=reit; rbit++) {
    bool skip = true;
    bool found = false;
    std::list<llvm::Loop*>::reverse_iterator lbit=loopList.rbegin();
    std::list<llvm::Loop*>::reverse_iterator leit=loopList.rend();
    for(; lbit!=leit; lbit++) {
      llvm::Loop* L = *lbit;
      if(currL != NULL) {
        if(skip && L != currL )
          continue;
        if(L == currL) {
          skip = false;
          continue;
        }
      } else { return; }
      assert(L);
      loopdata* wrld = NULL;
      if( ld_map.find(L) != ld_map.end() )
        wrld = ld_map.at(L);
      else
        tiler_error("Difference::",
                    "Didn't find loop data for the incoming value");
      assert(wrld);
      if(wrld->arrWrite.count(rbit->first) > 0) {
        if( auto alloca = llvm::dyn_cast<llvm::AllocaInst>(rbit->first) ) {
          if(alloca->isStaticAlloca()) {
            ld->interfering_loops[rbit->first] = L;
            found = true;
            break;
          } else if (checkRangeOverlap(ld->readTile.at(rbit->first),
                                       wrld->writeTile.at(rbit->first))) {
            ld->interfering_loops[rbit->first] = L;
            found = true;
            break;
          } else {} // Are there other cases ??
        } else {} // Do nothing
      } else {} // Do nothing
    }
    if(!found) {
      ld->interfering_loops[rbit->first] = NULL;
    } else {} // Found the loop
  }
}

z3::expr diff_preds::compute_diff_expr(llvm::Value* v) {
  // Compute the difference value using Z3
  z3::expr N_expr = ir2e->getZ3Expr(N).front();
  z3::expr Nm1_expr = N_expr - 1;
  z3::expr e_N = ir2e->getZ3Expr(v).front();
  z3::context& z3_ctx = e_N.ctx();
  z3::expr_vector out_vec(z3_ctx), in_vec(z3_ctx);
  out_vec.push_back( N_expr );
  in_vec.push_back( Nm1_expr );
  z3::expr e_Nm1 = e_N.substitute( out_vec, in_vec );
  z3::expr diff_e = (e_N - e_Nm1).simplify();
  return diff_e;
}

llvm::Value* diff_preds::get_val_for_diff(z3::expr diff_e, llvm::BasicBlock* b,
                                          llvm::Instruction* insertpt) {
  llvm::Instruction* insertionpt = NULL;
  if(insertpt != NULL) {
    insertionpt = insertpt;
  } else {
    if(b!=NULL)
      insertionpt = b->getFirstNonPHI();
    else
      tiler_error("diff_preds::", "Unknown insertion point");
  }
  llvm::IRBuilder<> builder(insertionpt);
  std::set< llvm::Value* > arrays_seen;
  llvm::Value* diff_val = getValueFromZ3Expr(diff_e, builder, module->getContext(),
                                             exprValMap, arrays_seen);
  return diff_val;
}

llvm::StringRef diff_preds::getPassName() const {
  return "Differencing for inductive verification";
}

void diff_preds::getAnalysisUsage(llvm::AnalysisUsage &au) const {
  au.addRequired<llvm::LoopInfoWrapperPass>();
}

