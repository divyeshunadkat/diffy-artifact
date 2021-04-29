#ifndef LLVMUTILS_H
#define LLVMUTILS_H

#include <set>
#include <list>
#include <map>
#include "utils/z3Utils.h"
#include "utils/options.h"
#include "daikon-inst/segment.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include "llvm/IR/TypeBuilder.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/Analysis/LoopIterator.h"
#include "llvm/Transforms/Utils/ValueMapper.h"
#include "llvm/Transforms/Utils/Cloning.h"
#pragma GCC diagnostic pop


typedef llvm::BasicBlock bb;
typedef std::set<const bb*> bb_set_t;
typedef std::vector<const bb*> bb_vec_t;

typedef llvm::Instruction inst;
typedef std::set<const inst*> inst_set_t;
typedef std::vector<const inst*> inst_vec_t;


#ifndef NDEBUG
#define LLVM_DUMP( ObjPtr ) { if(ObjPtr) ObjPtr->dump(); }
#else
#define LLVM_DUMP( ObjPtr ) {}
#endif

#define LLVM_DUMP_MARKED( ObjPtr ) { LLVM_DUMP(ObjPtr); \
                              tiler_warning("", "^^^^^^^^^^^^^^^^^^^^^^^"); }

class comment;
class src_loc;

std::unique_ptr<llvm::Module>
c2ir( std::string, llvm::LLVMContext&, std::vector< comment >& );

void c2bc( const std::string&, const std::string& );

std::unique_ptr<llvm::Module> c2ir( std::string, llvm::LLVMContext& );


llvm::Instruction*
estimate_comment_location( std::unique_ptr<llvm::Module>&, src_loc, src_loc);

void
estimate_comment_location(std::unique_ptr<llvm::Module>& module,
                          std::vector< comment >& comments,
                          std::map< const bb*,
                          std::pair< std::vector<std::string>,
                                     std::vector<std::string> >  >&
);

void setLLVMConfigViaCommandLineOptions( std::string strs );

void printSegmentInfo(segment& s);
void printBlockInfo(std::vector<llvm::BasicBlock*>& blockList);

  // Get original names of ssa variables from DEBUG information
std::string getVarName(const llvm::DbgValueInst* dbgVal );
std::string getVarName(const llvm::DbgDeclareInst* dbgVal );

typedef std::map<const llvm::Value*, std::string> name_map;
typedef std::map<std::string, const llvm::Value*> rev_name_map;

void buildNameMap( options& o, llvm::Function&, name_map&);

bool isRemInBop(const llvm::BinaryOperator*);
bool isRemInStore(const llvm::StoreInst*);
bool isAddInStore(const llvm::StoreInst*);
bool isSubInStore(const llvm::StoreInst*);
bool isMulInStore(const llvm::StoreInst*);
bool isDivInStore(const llvm::StoreInst*);

bool isValueInSubExpr(const llvm::Value*, const llvm::Value*);
bool onlyValueInSubExpr(const llvm::Value*, const llvm::Value*);
bool onlyValuesInSubExpr(std::list<const llvm::Value*>&, const llvm::Value*);
bool isLoadOrStoreInSubExpr(const llvm::Value*);

bool isInHeader(llvm::Instruction *, llvm::Loop *);
bool isOutOfLoop(llvm::Instruction *, llvm::Loop *);
bool isInLatch(llvm::Instruction *, llvm::Loop *);
bool isMyLatch(llvm::BasicBlock *, llvm::Loop *);
bool isSupported(llvm::Loop *);
bool isInSubLoop(llvm::BasicBlock *, llvm::Loop *, llvm::LoopInfo *);
unsigned valueIdxFromLatch( llvm::PHINode*, llvm::Loop *);
bool isLoopRotated( llvm::Loop * );

bool is_assume_call(const llvm::CallInst*);
bool is_assert_call(const llvm::CallInst*);
bool is_assert_loop(llvm::Loop*);

bool hasPhiNode(llvm::Value*);
llvm::Value* getPhiNode(llvm::Value*);
bool hasSuccessor(const llvm::BasicBlock*);

void getLoadsInSubExpr(llvm::Value*, std::list<llvm::Value*>&);

llvm::BasicBlock* getFirstBodyOfLoop(llvm::Loop *);
std::string getFuncNameForDaikon(llvm::Loop *);
llvm::Function *rand_prototype(llvm::Module *mod, llvm::LLVMContext& c);
llvm::Function *printf_prototype(llvm::Module *mod, llvm::LLVMContext& c);
llvm::Function *assume_prototype(llvm::Module *mod, llvm::LLVMContext& c);
llvm::Function *assert_prototype(llvm::Module *mod, llvm::LLVMContext& c);
llvm::Constant* geti8StrVal(llvm::Module& M,
                            char const* str,
                            llvm::Twine const& name,
                            llvm::LLVMContext& c);
void assertSingleNesting(llvm::Loop *L);
void assertNonNesting(llvm::Loop *L);
bool isIncrOp(llvm::Value *v);

// This function must be called if succ_b is indeed a successor
inline unsigned getSuccessorIndex( const bb* b, const bb* succ_b ) {
  auto t = b->getTerminator();
  unsigned idx_succ =0;
  for(; idx_succ < t->getNumSuccessors();idx_succ++ ) {
    if( succ_b == t->getSuccessor( idx_succ) ) return idx_succ;
  }
  assert( false );
  return idx_succ;
}

llvm::GetElementPtrInst* getGEP(llvm::LoadInst*);
llvm::GetElementPtrInst* getGEP(llvm::StoreInst*);
llvm::Value* getIdx(llvm::LoadInst*);
llvm::Value* getIdx(llvm::StoreInst*);
llvm::AllocaInst* getAlloca(llvm::LoadInst*);
llvm::AllocaInst* getAlloca(llvm::StoreInst*);
llvm::Loop* getNextLoop(std::list<llvm::Loop*> lList, llvm::Loop* L);
llvm::Value* getArrValueFromZ3Expr(llvm::Value*, z3::expr, llvm::IRBuilder<>&, llvm::LLVMContext&,
                                   std::map<std::string, llvm::Value*>&, std::set<llvm::Value*>&);
llvm::Value* getValueFromZ3Expr(z3::expr, llvm::IRBuilder<>&, llvm::LLVMContext&,
                                std::map<std::string, llvm::Value*>&, std::set<llvm::Value*>&);
llvm::Value* getValueFromZ3SubExpr(z3::expr, llvm::IRBuilder<>&, llvm::LLVMContext&,
                                   std::map<std::string, llvm::Value*>&, std::set<llvm::Value*>&);
bool checkRangeOverlap(z3::expr, z3::expr);
void collect_fun_bb(llvm::Function*, std::vector<llvm::BasicBlock*>&);
void get_topologically_sorted_bb(llvm::Function*,
                                 std::vector<llvm::BasicBlock*>&);

void collect_arr(llvm::Function &f, std::set<llvm::AllocaInst*>&);


void computeTopologicalOrder(llvm::Function &F,
                             std::map<const llvm::BasicBlock*,
                                      std::set<const llvm::BasicBlock*>>& bedges,
                             std::vector<const llvm::BasicBlock*>& bs,
                             std::map< const llvm::BasicBlock*, unsigned >& o_map);
void collect_loop_backedges(llvm::Pass *p,
                            std::map< const llvm::BasicBlock*,
                                      std::set<const llvm::BasicBlock*>>& loop_ignore_edge,
                            std::map< const llvm::BasicBlock*,
                                      std::set<const llvm::BasicBlock*>>& rev_loop_ignore_edge);

void collect_block_ancestors(llvm::BasicBlock* b,
                             std::map<llvm::BasicBlock*, std::set<llvm::BasicBlock*>>& block_preds_map);

llvm::Value* collect_loads_recs(llvm::Value*, llvm::Value*, std::list<llvm::Value*>&);

void find_cutpoints(llvm::Pass* P, llvm::Function &f, std::vector< llvm::BasicBlock* >& cutPoints);
void create_segments(llvm::Function &f,
                     std::vector< llvm::BasicBlock* >& cutPoints,
                     std::vector< segment >& segVec);
int readInt( const llvm::ConstantInt* );
void buildBlockMap(llvm::BasicBlock*, std::map<std::string, llvm::Value*>&);
bool deleteLoop(llvm::Loop *, llvm::DominatorTree &, llvm::ScalarEvolution &, llvm::LoopInfo &);

std::string getLocation(const llvm::BasicBlock* b );
std::string getLocation(const llvm::Instruction* I );
std::string getLocRange(const llvm::BasicBlock* b );

src_loc getLoc( const llvm::Instruction* I );
z3::sort llvm_to_z3_sort( z3::context& , llvm::Type* );
z3::expr read_const( const llvm::Value*, z3::context& );

std::vector<llvm::BasicBlock*> to_std_vec(llvm::SmallVector<llvm::BasicBlock*, 20>&);
llvm::SmallVector<llvm::BasicBlock*, 40> to_small_vec(std::vector<llvm::BasicBlock*>&);

bool cmp_loop_by_line_num (llvm::Loop*, llvm::Loop*);
void remove_loop_back_edge(llvm::Loop *);
void populate_arr_rd_wr(llvm::BasicBlock*,
                        std::map<llvm::Value*, std::list<llvm::Value*>>&,
                        std::map<llvm::Value*, std::list<llvm::Value*>>& );

llvm::PHINode* getInductionVariable(llvm::Loop*, llvm::ScalarEvolution*);

llvm::Value* find_ind_param_N(std::unique_ptr<llvm::Module>& module,
                              llvm::Function* f, llvm::BasicBlock* entry_bb);
bool check_conditional_in_loopbody(llvm::Loop *L);
void check_conditional_in_loopbody(llvm::Pass* p, llvm::Value* N, int& unsupported);
void check_indu_param_in_index(llvm::Function* f, llvm::Value* N, int& unsupported);
bool checkDependence(llvm::Value* expr, llvm::Value* val);
llvm::Instruction* cloneExpr(llvm::Instruction*, llvm::Instruction*,
                             llvm::Instruction*, llvm::Instruction*);
void eraseExpr(llvm::Instruction* expr, llvm::Instruction* skip);

void cloneLoopBlocksAtEnd(llvm::Loop *L, unsigned IterNumber, llvm::BasicBlock *NewExit,
                          llvm::BasicBlock *NewExitTop, llvm::BasicBlock *NewExitBot,
                          llvm::SmallVectorImpl<llvm::BasicBlock *> &NewBlocks,
                          llvm::LoopBlocksDFS &LoopBlocks, llvm::ValueToValueMapTy &VMap,
                          llvm::ValueToValueMapTy &LVMap, llvm::DominatorTree *DT,
                          llvm::LoopInfo *LI);

bool myPeelLoop(llvm::Loop *L, unsigned PeelCount, llvm::LoopInfo *LI,
                llvm::ScalarEvolution *SE, llvm::DominatorTree *DT,
                llvm::AssumptionCache *AC, bool PreserveLCSSA, llvm::Value *N,
                llvm::Value *NS, llvm::Value *exitVal, int stepCnt,
                llvm::ValueToValueMapTy &LVMap,
                std::vector<llvm::BasicBlock*>& peeledBlocks);

llvm::BranchInst *getLoopGuardBR(llvm::Loop *L, bool isPeeled);

llvm::Value* getSingularNRef(llvm::Value* N, llvm::BasicBlock* entry_bb);
llvm::Instruction* generateNSVal(llvm::Value* N, llvm::Value* SingularNRef,
                                 llvm::BasicBlock* entry_bb);

llvm::BasicBlock*
create_cloned_remapped_bb( const llvm::BasicBlock* basicBlock,
                           const llvm::Twine& Name,
                           llvm::Function* F);
std::vector<llvm::BasicBlock*>
collect_cloned_loop_blocks(llvm::Pass*, llvm::Loop*, loopdata*);

llvm::AllocaInst* create_new_alloca(llvm::AllocaInst*);
void remap_store(llvm::StoreInst*, llvm::AllocaInst*, llvm::AllocaInst*);
void remap_load(llvm::LoadInst*, llvm::AllocaInst*, llvm::AllocaInst*);
void add_equality_stmt(llvm::BasicBlock*, llvm::AllocaInst*,
                       llvm::AllocaInst*, llvm::LLVMContext&);

typedef std::map< std::pair<const llvm::Value*,unsigned>,
                  z3::expr > ValueExprMap;

class value_expr_map {
public:
  value_expr_map( z3::context& ctx_ ) : ctx( ctx_ ) {};
  void insert_term_map( const llvm::Value*, unsigned, z3::expr );
  void insert_term_map( const llvm::Value*, z3::expr );
  z3::expr insert_new_def( const llvm::Value* op, unsigned c_count );
  z3::expr insert_new_def( const llvm::Value* );
  z3::expr read_constant( const llvm::Value* );
  z3::expr read_term( const llvm::Value*, unsigned );
  z3::expr get_earlier_term( const llvm::Value*, unsigned );
  z3::expr get_term( const llvm::Value*, unsigned, std::string );
  z3::expr get_term( const llvm::Value*, unsigned );
  z3::expr get_term( const llvm::Value*, std::string );
  z3::expr get_term( const llvm::Value* );
  z3::expr get_latest_term( const llvm::Value* );
  z3::expr create_fresh_name( const llvm::Value*, std::string );
  z3::expr create_fresh_name( const llvm::Value* );
  unsigned get_max_version( const llvm::Value*);
  const std::vector<unsigned>& get_versions( const llvm::Value* );
  void copy_values(value_expr_map& m);
  void dump();
  void print( std::ostream& o );
  std::list<z3::expr> get_expr_list();
private:
  z3::context& ctx;
  std::map< std::pair<const llvm::Value*,unsigned>, z3::expr > vmap;
  std::map< const llvm::Value*, std::vector<unsigned> > versions;
  std::vector<unsigned> dummy_empty_versions;
};

class fun_clonner_mod_pass : public llvm::ModulePass
{
  public:
  static char ID;
  options& o;
  std::string name_suffix;
  std::map<llvm::Function*, std::string>& fn_hash_map;
  std::map<std::string, llvm::ValueToValueMapTy>& fwd_fn_v2v_map;
  std::map<llvm::Function*, std::map<const llvm::Value*,
                                     const llvm::Value*>>& rev_fn_v2v_map;
  std::map<const bb*, std::pair<std::vector<std::string>,
                                std::vector<std::string>>>& bb_cmt_map;
  bool make_target;

  fun_clonner_mod_pass(options&, std::string,
                       std::map<llvm::Function*, std::string>& fn_h_map,
                       std::map<std::string, llvm::ValueToValueMapTy>&,
                       std::map<llvm::Function*,
                         std::map<const llvm::Value*, const llvm::Value*>>&,
                       std::map<const bb*, std::pair<std::vector<std::string>,
                         std::vector<std::string> > >&, bool);
  void create_alloca_map(llvm::Function*, llvm::ValueToValueMapTy&,
                         std::map<const llvm::Value*, const llvm::Value*>&);
  void adjust_bb_cmt_map(llvm::ValueToValueMapTy&);
  std::string random_string();
  virtual bool runOnModule( llvm::Module & );
  virtual llvm::StringRef getPassName() const;
};

class preprocess : public llvm::FunctionPass
{
  public:
  static char ID;

  std::unique_ptr<llvm::Module>& module;
  options& o;

  preprocess(std::unique_ptr<llvm::Module>&, options&);
  ~preprocess();

  llvm::Function* target_f = NULL;
  llvm::BasicBlock* entry_bb = NULL;
  llvm::Value* N = NULL;
  llvm::LoadInst* N_load = NULL;

  void remove_glb_N_loads();
  void remove_unwanted_assume();
  void check_nesting_depth();

  virtual bool runOnFunction(llvm::Function &f);
  llvm::StringRef getPassName() const;
  void getAnalysisUsage(llvm::AnalysisUsage &au) const;
};

class count_loops : public llvm::FunctionPass
{
  public:
  static char ID;

  std::unique_ptr<llvm::Module>& module;
  options& o;
  int loop_count=0;

  count_loops(std::unique_ptr<llvm::Module>&, options&);
  ~count_loops();

  virtual bool runOnFunction(llvm::Function &f);
  llvm::StringRef getPassName() const;
  void getAnalysisUsage(llvm::AnalysisUsage &au) const;
};

const llvm::DataLayout& getDataLayout(llvm::Instruction*);

typedef std::map<llvm::Value*,int>  llvm_mono;
struct compare_mono
{
   bool operator() (const llvm_mono& lhs, const llvm_mono& rhs) const {
     auto it1 = lhs.begin();
     auto it2 = rhs.begin();
     while( it1 != lhs.end() && it2 != rhs.end() ) {
       llvm::Value* i1 = it1->first;
       llvm::Value* i2 = it2->first;
       if( i1 > i2 ) return it2->second > 0;
       if( i1 < i2 ) return it1->second < 0;
       if( i1 == i2 ) {
         if( it1->second == it2->second ) {
           it1++;
           it2++;
         }else
           return it1->second < it2->second;
       }
     }
     if( it2 != rhs.end() ) return it2->second > 0;
     if( it1 != lhs.end() ) return it1->second < 0;
     return false;
   }
};

typedef std::map<llvm_mono,llvm::Constant*,compare_mono> llvm_poly;

void
get_simplified_polynomial( llvm::Value* I,
                           const std::vector<llvm::Value*>& vars,
                           const llvm::DataLayout& DL,
                           llvm_poly& );

void dump( const llvm_poly& );

#endif
