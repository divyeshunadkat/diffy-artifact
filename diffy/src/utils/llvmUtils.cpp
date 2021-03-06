#include <limits>
#include <random>
#include "llvmUtils.h"
#include "utils/graphUtils.h"
#include "daikon-inst/comments.h" //todo: move to utils
#include <boost/algorithm/string.hpp>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wstrict-aliasing"
// pragam'ed to aviod warnings due to llvm included files
#include "llvm/IR/DIBuilder.h"
#include "llvm/IR/LegacyPassManager.h"
// #include "llvm/IR/ConstantFold.h" // later versions of llvm will need this
#include "llvm/Analysis/ConstantFolding.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/LoopSimplify.h"

//clang related code
#include <clang/CodeGen/CodeGenAction.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/CompilerInvocation.h>
#include <clang/Basic/DiagnosticOptions.h>
#include <clang/Basic/TargetInfo.h>
#pragma GCC diagnostic pop


#define CLANG_VERSION "6.0"

void c2bc( const std::string& fileName, const std::string& outName )
{
  // make a system call
  std::ostringstream cmd;
  cmd << "clang-" << CLANG_VERSION
      << " -emit-llvm -fno-omit-frame-pointer -Xclang -disable-O0-optnone -gdwarf-2 "
      << fileName << " -o " << outName << " -c";
  // std::cout << cmd.str() << "\n";
  if( system( cmd.str().c_str() ) != 0 ) exit(1);
}


// best guess of location
class estimate_loc_pass : public llvm::BasicBlockPass {

  src_loc ref;
  src_loc ref_end;
  src_loc low_est;
  src_loc up_est;
  llvm::Instruction* I_low = NULL;
  llvm::Instruction* I_up = NULL;
public:
  static char ID;

  llvm::Instruction* get_I_low() { return I_low; }
  llvm::Instruction* get_I_up() { return I_up; }

  src_loc get_low_estimate() {
    return low_est;
  }

  src_loc get_up_estimate() {
    return up_est;
  }

public:
  estimate_loc_pass( src_loc& loc_, src_loc& ref_end_) :
    llvm::BasicBlockPass(ID), ref(loc_),ref_end(ref_end_),
    low_est( 0,0, loc_.file),
    up_est( UINT_MAX, UINT_MAX, loc_.file) {
  };
  ~estimate_loc_pass() {};

  virtual bool runOnBasicBlock( llvm::BasicBlock &bb ) {
    for( llvm::Instruction& I : bb.getInstList() ) {
      const llvm::DebugLoc d = I.getDebugLoc();
      if( d ) {
        unsigned l = d.getLine();
        unsigned c  = d.getCol();
        std::string f =llvm::cast<llvm::DIScope>(d.getScope())->getFilename();

        src_loc curr(l,c,f);
        if( f != curr.file ) continue;
        if( COMPARE_OBJ2(curr, low_est, line, col) ||
            COMPARE_OBJ2(up_est, curr, line, col) )
          continue;
        if( ref_end == curr ||
            COMPARE_OBJ2(ref_end, curr, line, col)  ) {
          up_est = curr;
          I_up = &I;
        }
        if( ref == curr ||
            COMPARE_OBJ2(curr, ref, line, col)  ) {
          low_est = curr;
          I_low = &I;
        }


        // {
        //   std::cerr << "\n";
        //   dump_triple( low_est );
        //   dump_triple( ref );
        //   dump_triple( up_est );
        //   dump_triple( curr );
        //   std::cerr << "\n";
        // }

        // auto curr = std::make_tuple(l,c,f);
        // if( f != std::get<2>(curr) ) continue;
        // if( COMPARE_TUPLE2(curr, low_est, 0, 1) ||
        //     COMPARE_TUPLE2(up_est, curr, 0, 1) )
        //   continue;
        // if( ref_end == curr ||
        //     COMPARE_TUPLE2(ref_end, curr, 0, 1)  ) {
        //   up_est = curr;
        //   I_up = &I;
        // }
        // if( ref == curr ||
        //     COMPARE_TUPLE2(curr, ref, 0, 1)  ) {
        //   low_est = curr;
        //   I_low = &I;
        // }
      }
    }

    return false;
  }
  
  virtual void getAnalysisUsage(llvm::AnalysisUsage &au) const {
      au.setPreservesAll();
  }
  virtual llvm::StringRef getPassName() const {
    return "estimate location pass: finds estimate of a source location!!";
  }
};

char estimate_loc_pass::ID = 0;


llvm::Instruction*
estimate_comment_location( std::unique_ptr<llvm::Module>& module,
                        src_loc start, src_loc end) {
  llvm::legacy::PassManager passMan;
  auto estimate_pass = new estimate_loc_pass( start, end );
  passMan.add( estimate_pass );
  passMan.run( *module.get() );
  // {
  //   estimate_pass->get_I_low()->dump();
  //   estimate_pass->get_I_up()->dump();
  //   dump_triple( estimate_pass->get_low_estimate() );
  //   dump_triple( estimate_pass->get_up_estimate() );
  // }
  return estimate_pass->get_I_up();
  // return estimate_pass->get_low_estimate();
}

void
estimate_comment_location(std::unique_ptr<llvm::Module>& module,
                          std::vector< comment >& comments,
                          std::map< const bb*,
          std::pair< std::vector<std::string>, std::vector<std::string> > >&
                          bb_comment_map ) {
  // collect all the comments that are written at the same program point
  std::map< llvm::Instruction*, std::vector<std::string> > comment_map;
  std::vector< std::vector< llvm::Instruction* > > Is;
  for( auto comment : comments ) {
    auto start = comment.start;
    auto end = comment.end;
    auto comment_text = comment.text;
    llvm::Instruction* I =
      estimate_comment_location( module, start, end );
    if( comment_map.find(I) != comment_map.end() ) {
      auto& c_vec = comment_map.at(I);
      c_vec.push_back(comment_text);
    }else{
      comment_map[I].push_back(comment_text);
      bool done = false;
      for( auto& B_Is : Is ) {
        if( B_Is[0]->getParent() == I->getParent() ) {
          unsigned i = 0;
          for( auto& Io : B_Is[0]->getParent()->getInstList() ) {
            if( i == B_Is.size() || &Io == I ) break;
            if( &Io == B_Is[i] ) i++;
          }
          B_Is.insert( B_Is.begin() + i, I);
          done = true;
        }
      }
      if( !done ) Is.push_back({I});
    }
  }

  for( auto& B_Is : Is ) {
    //intially all instructions in B_Is have same block
    for( llvm::Instruction* I : B_Is ) {
      llvm::BasicBlock* bb = I->getParent();
      auto& pair = bb_comment_map[bb];
      llvm::Instruction* first = &(*(bb->getInstList().begin()));
      if( bb->getTerminator() == I ) {
        pair.second = comment_map[I];
      }else if( first == I ) {
        pair.first = comment_map[I];
      }else{
        pair.second = comment_map[I];
        bb->splitBasicBlock(I);
      }
    }
  }
}


//
// clang source to llvm soruce location
// warning: the code is without several guards (check CGDebugInfo.cpp in clang)
src_loc
getLocFromClangSource( const clang::SourceLocation& loc,
                       const clang::SourceManager& sm) {
  unsigned line = sm.getPresumedLoc(loc).getLine();
  unsigned col = sm.getPresumedLoc(loc).getColumn();
  std::string file = sm.getFilename(loc);
  return src_loc(line,col,file);
}

//this function is a copy from CompilerInstance::ExecuteActuib
bool ExecuteAction( clang::CompilerInstance& CI,
                    clang::FrontendAction &Act,
                    std::vector< comment >& comments_found) {

  // FIXME: Take this as an argument, once all the APIs we used have moved to
  // taking it as an input instead of hard-coding llvm::errs.
  llvm::raw_ostream &OS = llvm::errs();

  // Create the target instance.
  CI.setTarget(clang::TargetInfo::CreateTargetInfo(CI.getDiagnostics(),
                                            CI.getInvocation().TargetOpts));
  if (!CI.hasTarget())
    return false;
  CI.getTarget().adjust(CI.getLangOpts());
  CI.getTarget().adjustTargetOptions(CI.getCodeGenOpts(), CI.getTargetOpts());

  for (const clang::FrontendInputFile &FIF : CI.getFrontendOpts().Inputs) {
    // Reset the ID tables if we are reusing the SourceManager and parsing
    // regular files.
    if (CI.hasSourceManager() && !Act.isModelParsingAction())
      CI.getSourceManager().clearIDTables();

    if (Act.BeginSourceFile( CI, FIF)) {
      Act.Execute();
      clang::ASTContext& ast_ctx = CI.getASTContext();
      clang::SourceManager& sm = CI.getSourceManager();
      clang::RawCommentList& comment_list = ast_ctx.getRawCommentList();
      for( clang::RawComment* cmnt : comment_list.getComments() ) {
        // OS << comment->getRawText( sm ) << "\n";
        //todo: check prefix of the comment
        std::string multi_cmt = cmnt->getRawText( sm );

        std::vector<std::string> cmts;
        boost::split(cmts, multi_cmt, [](char c){return c == '\n';});
        for( auto cmt : cmts ) {
          comment c;
          boost::algorithm::trim(cmt);
          if(COMMENT_PREFIX == cmt.substr(0, COMMENT_PREFIX_LEN) ) {
            c.text = cmt.substr(COMMENT_PREFIX_LEN, cmt.size()-COMMENT_PREFIX_LEN );
            c.start = getLocFromClangSource(cmnt->getSourceRange().getBegin(), sm);
            c.end = getLocFromClangSource( cmnt->getSourceRange().getEnd(), sm);
            comments_found.push_back(c);
          }
        }
      }
      Act.EndSourceFile();
    }
  }

  // Notify the diagnostic client that all files were processed.
  CI.getDiagnostics().getClient()->finish();

  if ( CI.getDiagnosticOpts().ShowCarets) {
    // We can have multiple diagnostics sharing one diagnostic client.
    // Get the total number of warnings/errors from the client.
    unsigned NumWarnings = CI.getDiagnostics().getClient()->getNumWarnings();
    unsigned NumErrors = CI.getDiagnostics().getClient()->getNumErrors();

    if (NumWarnings)
      OS << NumWarnings << " warning" << (NumWarnings == 1 ? "" : "s");
    if (NumWarnings && NumErrors)
      OS << " and ";
    if (NumErrors)
      OS << NumErrors << " error" << (NumErrors == 1 ? "" : "s");
  }

  return !CI.getDiagnostics().getClient()->getNumErrors();
}


//Direct translation via API clang
std::unique_ptr<llvm::Module> c2ir( std::string filename,
                                    llvm::LLVMContext& llvm_ctx,
                                    std::vector< comment >& comments ) {

  // return nullptr;

  // look in clang/include/clang/Driver/CC1Options.td
  // and clang/lib/Frontend/CompilerInvocation.cpp
  // to find right param names
  std::vector<const char *> args;
  args.push_back( "-emit-llvm" );
  args.push_back( "-disable-llvm-passes" );
  args.push_back( "-debug-info-kind=standalone" );
  args.push_back( "-dwarf-version=2" );
  args.push_back( "-dwarf-column-info" );
  args.push_back( "-mdisable-fp-elim");
  args.push_back( "-femit-all-decls" );
  args.push_back( "-O1" );
  args.push_back( "-disable-O0-optnone" );
  args.push_back( filename.c_str() );

  clang::CompilerInstance Clang;
  Clang.createDiagnostics();

  std::shared_ptr<clang::CompilerInvocation> CI(new clang::CompilerInvocation());
  clang::CompilerInvocation::CreateFromArgs( *CI.get(), &args[0],
                                             &args[0] + args.size(),
                                            Clang.getDiagnostics());
  Clang.setInvocation(CI);
  clang::CodeGenAction *Act = new clang::EmitLLVMOnlyAction(&llvm_ctx);

  if (!ExecuteAction(Clang, *Act, comments))
  // if (!Clang.ExecuteAction(*Act))
    return nullptr;

  std::unique_ptr<llvm::Module> module = Act->takeModule();

  return std::move(module);

  return nullptr;
}


std::unique_ptr<llvm::Module> c2ir( std::string filename,
                                    llvm::LLVMContext& llvm_ctx ) {
  std::vector< comment > comments_found;
  return move( c2ir( filename, llvm_ctx, comments_found ) );
}



void setLLVMConfigViaCommandLineOptions( std::string strs ) {
  std::string n = "test";
  const char* array[2];
  array[0] = n.c_str();
  array[1] = strs.c_str();
  llvm::cl::ParseCommandLineOptions( 2, array );
}

void printSegmentInfo(segment& s) {
  std::cout << "\nPrinting segment info for segment " << &s;
  std::cout << "\nPrinting entry blocks\n";
  printBlockInfo(s.entryCutPoints);
  std::cout << "\nPrinting exit blocks\n";
  printBlockInfo(s.exitCutPoints);
  std::cout << "\nPrinting body blocks\n";
  printBlockInfo(s.bodyBlocks);
}

void printBlockInfo(std::vector<llvm::BasicBlock*>& blockList) {
  for(const llvm::BasicBlock* b : blockList) {
    b->printAsOperand(llvm::errs(), false);
    std::cout << "\n";
  }
}


std::string getVarName(const llvm::DbgValueInst* dVal ) {
  // auto var = dVal->getValue();
  auto md = dVal->getVariable();
  llvm::DIVariable* diMd = llvm::dyn_cast<llvm::DIVariable>(md);
  return (std::string)( diMd->getName() );
}

std::string getVarName(const llvm::DbgDeclareInst* dDecl ) {
  // auto var = dDecl->getAddress();
  auto md = dDecl->getVariable();
  llvm::DIVariable* diMd = llvm::dyn_cast<llvm::DIVariable>(md);
  auto str = (std::string)( diMd->getName() );
  return str;
}

void buildNameMap( options& o, llvm::Function& f,
                   name_map& localNameMap) {
  std::map<const llvm::Value*, std::set<std::string> > l_name_map_extra;
 // std::map<std::string, llvm::Value*>& nameValueMap) {
  //  std::cout << "Inside buildNameMap\n";
  //  localNameMap.clear();
  //  nameValueMap.clear();
  for( llvm::inst_iterator iter(f),end(f,true); iter != end; ++iter ) {
    llvm::Instruction* I = &*iter;
    llvm::Value* var = NULL;
    llvm::MDNode* md = NULL;
    std::string str;
    if( llvm::DbgDeclareInst* dDecl =
        llvm::dyn_cast<llvm::DbgDeclareInst>(I) ) {
      var = dDecl->getAddress();
      md = dDecl->getVariable();
      llvm::DIVariable* diMd = llvm::dyn_cast<llvm::DIVariable>(md);
      str = (std::string)( diMd->getName() );
//      std::cout << "Got the name:" << str << "\n";
    } else if( llvm::DbgValueInst* dVal =
               llvm::dyn_cast<llvm::DbgValueInst>(I)) {
      var = dVal->getValue();
      md = dVal->getVariable();
      llvm::DIVariable* diMd = llvm::dyn_cast<llvm::DIVariable>(md);
      str = (std::string)( diMd->getName() );
      if( llvm::isa<llvm::ConstantInt>(var) ) {
        var = dVal;
      }
//      std::cout << "Got the name:" << str << "\n";
    }
    if( var ) {
      // if var is non-null add the name to the map
      if( localNameMap.find(var) != localNameMap.end() ) {
        if( str != localNameMap.at( var ) )
          l_name_map_extra[var].insert( str );
      }
      localNameMap[var] = str;
      //      nameValueMap[str] = var;
//to look at the scope field
// check if there has been a declaration with same name with different
// line number
//        auto it = declarationLocationMap.find( str );
//        if( it == declarationLocationMap.end() ) {
//          declarationLocationMap[str] = lineNum;
//          localNameMap[var] = str;
//          nameValueMap[str] = var;
//        }else if( it->second == lineNum ) {
//          localNameMap[var] = str;
//          nameValueMap[str] = var;
//        }else{
//          localNameMap[var] = str + "_at_"+ std::to_string( lineNum );
//          nameValueMap[str] = var;
//        }
    }
  }

  //Extend names to phiNodes
  // if phinode names are misssing, 
  for( auto& b: f.getBasicBlockList() ) {
    for( llvm::BasicBlock::iterator I = b.begin(); llvm::isa<llvm::PHINode>(I); ++I) {
      llvm::PHINode *phi = llvm::cast<llvm::PHINode>(I);
      unsigned num = phi->getNumIncomingValues();
      std::set<std::string> names;
      for ( unsigned i = 0 ; i < num ; i++ ) {
        llvm::Value *v = phi->getIncomingValue(i);
        if(llvm::Instruction *inI = llvm::dyn_cast<llvm::Instruction>(v)) {
          if( localNameMap.find(inI) != localNameMap.end() ) {
            auto back_name = localNameMap.at(inI);
            names.insert( back_name );
            if( l_name_map_extra.find(inI) != l_name_map_extra.end() )
              set_insert( l_name_map_extra.at(inI), names );
          }
        }
      }
      bool found = ( localNameMap.find(phi) != localNameMap.end() );
      if( names.size() == 1) {
        std::string name = *names.begin();
        if( found && localNameMap.at(phi) != name ) {
          if( o.verbosity > 5 ) {
            tiler_warning("build name map::","multiple names found for a value!!");
          }
        }
        localNameMap[phi] = name;
      }else if( found &&  l_name_map_extra.find( phi ) == l_name_map_extra.end() ) {
        // the name is already there
        std::string name = localNameMap.at(phi);
        if( names.find( name ) ==  names.end() &&  o.verbosity > 5 ) {
          tiler_warning("build name map::","multiple mismatching names found for a value!!");
        }
      }else if( names.size() > 1 || l_name_map_extra.find( phi ) != l_name_map_extra.end() ) {
        if( o.verbosity > 5 ) {
          for( auto name : names ) {
            std::cout << name << "\n";
          }
          if(found) std::cout << localNameMap.at(phi) << "\n" ;
          if( l_name_map_extra.find(phi) != l_name_map_extra.end() )
             for( auto name : l_name_map_extra.at( phi ) ) { std::cout << name << "\n"; }
          tiler_warning( "build name map::","multiple names found!!");
        }
      }

      //       auto back_name = localNameMap.at(inI);
      // if( found && name != localNameMap.at(inI) )
      //   tiler_error("build name map::","phi node has multiple names!!");
      // name = localNameMap.at(inI);
      // found = true;

      // if( !found ) {
      //   // If this function fails, investigate to find the (correct) name
      //   //tiler_error("build name map::","name of a phi node not found!!");
      // }
    }
  }
}

bool isRemInBop(const llvm::BinaryOperator* bop) {
  unsigned op = bop->getOpcode();
  switch( op ) {
  case llvm::Instruction::URem : return true;
  case llvm::Instruction::SRem: return true;
  case llvm::Instruction::FRem: return true;
  default: return false;
  }
  return false;
}

bool isRemInStore(const llvm::StoreInst* store) {
  auto val = store->getOperand(0);
  if( auto cast = llvm::dyn_cast<llvm::CastInst>(val) ) {
    val = cast->getOperand(0);
  } else {}
  if(auto bop = llvm::dyn_cast<llvm::BinaryOperator>(val) ) {
    unsigned op = bop->getOpcode();
    switch( op ) {
    case llvm::Instruction::URem : return true;
    case llvm::Instruction::SRem: return true;
    case llvm::Instruction::FRem: return true;
    default: return false;
    }
  } else {
    return false;
  }
}

bool isAddInStore(const llvm::StoreInst* store) {
  auto val = store->getOperand(0);
  if( auto cast = llvm::dyn_cast<llvm::CastInst>(val) ) {
    val = cast->getOperand(0);
  } else {}
  if(auto bop = llvm::dyn_cast<llvm::BinaryOperator>(val) ) {
    unsigned op = bop->getOpcode();
    switch( op ) {
    case llvm::Instruction::Add : return true;
    case llvm::Instruction::FAdd: return true;
    default: return false;
    }
  } else {
    return false;
  }
}

bool isSubInStore(const llvm::StoreInst* store) {
  auto val = store->getOperand(0);
  if( auto cast = llvm::dyn_cast<llvm::CastInst>(val) ) {
    val = cast->getOperand(0);
  } else {}
  if(auto bop = llvm::dyn_cast<llvm::BinaryOperator>(val) ) {
    unsigned op = bop->getOpcode();
    switch( op ) {
    case llvm::Instruction::Sub : return true;
    case llvm::Instruction::FSub: return true;
    default: return false;
    }
  } else {
    return false;
  }
}

bool isMulInStore(const llvm::StoreInst* store) {
  auto val = store->getOperand(0);
  if( auto cast = llvm::dyn_cast<llvm::CastInst>(val) ) {
    val = cast->getOperand(0);
  } else {}
  if(auto bop = llvm::dyn_cast<llvm::BinaryOperator>(val) ) {
    unsigned op = bop->getOpcode();
    switch( op ) {
    case llvm::Instruction::Mul : return true;
    case llvm::Instruction::FMul: return true;
    default: return false;
    }
  } else {
    return false;
  }
}

bool isDivInStore(const llvm::StoreInst* store) {
  auto val = store->getOperand(0);
  if( auto cast = llvm::dyn_cast<llvm::CastInst>(val) ) {
    val = cast->getOperand(0);
  } else {}
  if(auto bop = llvm::dyn_cast<llvm::BinaryOperator>(val) ) {
    unsigned op = bop->getOpcode();
    switch( op ) {
    case llvm::Instruction::SDiv: return true;
    case llvm::Instruction::UDiv: return true;
    case llvm::Instruction::FDiv: return true;
    default: return false;
    }
  } else {
    return false;
  }
}

bool isValueInSubExpr(const llvm::Value* val, const llvm::Value* expr) {
  if(val == expr) return true;
  if(auto bop = llvm::dyn_cast<llvm::BinaryOperator>(expr) ) {
    bool res = isValueInSubExpr(val, bop->getOperand(0));
    if(res) return true;
    res = isValueInSubExpr(val, bop->getOperand(1));
    return res;
  } else if( llvm::isa<llvm::CallInst>(expr) ) {
    return false;
    //    tiler_error( "llvmutils::", "Call instruction as sub-expression not supported");
  } else if( auto store = llvm::dyn_cast<llvm::StoreInst>(expr) ) {
    return isValueInSubExpr(val, store->getOperand(0));
  } else if( llvm::isa<llvm::GetElementPtrInst>(expr) ) {
    return false;
    //    tiler_error( "llvmutils::", "GEP as sub-expression not supported");
  } else if(auto load = llvm::dyn_cast<llvm::LoadInst>(expr) ) {
    return isValueInSubExpr(val, load->getOperand(0));
    //    return false;
  } else if( auto cast = llvm::dyn_cast<llvm::CastInst>(expr) ) {
    return isValueInSubExpr(val, cast->getOperand(0));
  } else if( llvm::isa<llvm::AllocaInst>(expr) ) {
    return false;
    //    tiler_error( "llvmutils::", "Alloca as sub-expression not supported");
    // return isValueInExpr(val, alloca->getArraySize());
  } else {
    return false;
  }
}

bool onlyValueInSubExpr(const llvm::Value* val, const llvm::Value* expr) {
  if( auto bop = llvm::dyn_cast<llvm::BinaryOperator>(expr) ) {
    bool res = true;
    if( ! llvm::isa<llvm::ConstantInt>(bop->getOperand(0)) )
      res = onlyValueInSubExpr(val, bop->getOperand(0));
    if( ! llvm::isa<llvm::ConstantInt>(bop->getOperand(1)) )
      if(res) res = res && onlyValueInSubExpr(val, bop->getOperand(1));
    return res;
  } else if( auto cast = llvm::dyn_cast<llvm::CastInst>(expr) ) {
    return onlyValueInSubExpr(val, cast->getOperand(0));
  } else if( auto load = llvm::dyn_cast<llvm::LoadInst>(expr) ) {
    return onlyValueInSubExpr(val, load->getOperand(0));
    /*
  } else if( llvm::isa<llvm::StoreInst>(expr) ) {
    return false;
  } else if( llvm::isa<llvm::GetElementPtrInst>(expr) ) {
    return false;
  } else if( llvm::isa<llvm::CallInst>(expr) ) {
    return false;
  } else if( llvm::isa<llvm::AllocaInst>(expr) ) {
    return false;
    */
  } else {
    if(val == expr) return true;
    else return false;
  }
}

bool onlyValuesInSubExpr(std::list<const llvm::Value*>& val_l, const llvm::Value* expr) {
  if( auto bop = llvm::dyn_cast<llvm::BinaryOperator>(expr) ) {
    bool res = true;
    if( ! llvm::isa<llvm::ConstantInt>(bop->getOperand(0)) )
      res = onlyValuesInSubExpr(val_l, bop->getOperand(0));
    if( ! llvm::isa<llvm::ConstantInt>(bop->getOperand(1)) )
      if(res) res = res && onlyValuesInSubExpr(val_l, bop->getOperand(1));
    return res;
  } else if( auto cast = llvm::dyn_cast<llvm::CastInst>(expr) ) {
    return onlyValuesInSubExpr(val_l, cast->getOperand(0));
  } else if( auto load = llvm::dyn_cast<llvm::LoadInst>(expr) ) {
    return onlyValuesInSubExpr(val_l, load->getOperand(0));
  } else {
    for(auto v : val_l)
      if(v == expr) return true;
    return false;
  }
}

bool isLoadOrStoreInSubExpr(const llvm::Value* expr) {
  if(auto bop = llvm::dyn_cast<llvm::BinaryOperator>(expr) ) {
    bool res = isLoadOrStoreInSubExpr(bop->getOperand(0));
    if(res) return true;
    return isLoadOrStoreInSubExpr(bop->getOperand(1));
  } else if(auto call = llvm::dyn_cast<llvm::CallInst>(expr) ) {
    llvm::Function* fp = call->getCalledFunction();
    if( llvm::dyn_cast<llvm::IntrinsicInst>(call) ) {
      return false;
    } else if( fp != NULL && fp->getName().startswith("__VERIFIER") ) {
      return false;
    } else {
      tiler_error( "llvmutils::", "Call instruction as sub-expression not supported");
    }
  } else if( llvm::dyn_cast<llvm::StoreInst>(expr) ) {
    return true;
  } else if( llvm::dyn_cast<llvm::GetElementPtrInst>(expr) ) {
    return true;
  } else if( llvm::dyn_cast<llvm::LoadInst>(expr) ) {
    return true;
  } else if( auto cast = llvm::dyn_cast<llvm::CastInst>(expr) ) {
    return isLoadOrStoreInSubExpr(cast->getOperand(0));
  } else if( llvm::dyn_cast<llvm::AllocaInst>(expr) ) {
    tiler_error( "llvmutils::", "Alloca as sub-expression not supported");
  } else {
    return false;
  }
}

void getLoadsInSubExpr(llvm::Value* expr, std::list<llvm::Value*>& loads) {
  if( auto load = llvm::dyn_cast<llvm::LoadInst>(expr) ) {
    loads.push_back(load);
  } else if(auto bop = llvm::dyn_cast<llvm::BinaryOperator>(expr) ) {
    getLoadsInSubExpr(bop->getOperand(0), loads);
    getLoadsInSubExpr(bop->getOperand(1), loads);
  } else if( llvm::dyn_cast<llvm::CallInst>(expr) ) {
    tiler_error( "llvmutils::", "Call instruction as sub-expression not supported");
  } else if( auto store = llvm::dyn_cast<llvm::StoreInst>(expr) ) {
    getLoadsInSubExpr(store->getOperand(0), loads);
  } else if( auto cast = llvm::dyn_cast<llvm::CastInst>(expr) ) {
    getLoadsInSubExpr(cast->getOperand(0), loads);
  } else if( llvm::dyn_cast<llvm::AllocaInst>(expr) ) {
    tiler_error( "llvmutils::", "Alloca as sub-expression not supported");
  } else {}
}

bool isInHeader(llvm::Instruction *I, llvm::Loop *L) {
//  std::cout << "In isInHeader\n";
  auto h = L->getHeader();
  auto b = I->getParent();
  if(h==b) {
//    std::cout << "In Header\n";
    return true;
  } else {
//    std::cout << "Not in Header\n";
    return false;
  }
}

bool isOutOfLoop(llvm::Instruction *I, llvm::Loop *L) {
//  std::cout << "In isOutOfLoop\n";
  if(L==NULL) {
    return true;
  }
  auto bb = I->getParent();
  for( auto b: L->getBlocks() ) {
    if(b == bb) {
//      std::cout << "Is in the loop\n";
      return false;
    }
  }
//  std::cout << "Is out of the loop\n";
  return true;
}

bool isMyLatch(llvm::BasicBlock *b, llvm::Loop *L) {
//  std::cout << "In isInLatch\n";
  if(L==NULL) {
    return false;
  }
  auto h = L->getLoopLatch();
  return h == b;
}

bool isInLatch(llvm::Instruction *I, llvm::Loop *L) {
  if(L==NULL) {
    return false;
  }
  auto b = I->getParent();
  // return isMyLatch( b, L ); //todo:
  auto h = L->getLoopLatch();
  if(h==b) {
    return true;
  } else {
    return false;
  }
}

unsigned valueIdxFromLatch( llvm::PHINode* phi, llvm::Loop * loop) {
  assert( phi->getNumIncomingValues() == 2 );
  unsigned i =0;
  if( isMyLatch( phi->getIncomingBlock(0), loop) ) { i = 0;
  }else if( isMyLatch( phi->getIncomingBlock(1), loop) ) {
    i = 1;
  }else{ tiler_error( "llvmutils::", "header latch did not match!!"); }
  return i;
}

bool isLoopRotated( llvm::Loop * loop ) {
  auto t = loop->getLoopLatch()->getTerminator();
  if( auto br = llvm::dyn_cast<llvm::BranchInst>(t) ) {
    return br->isConditional();
  }
  tiler_error( "llvmutils::", "loops always end with conditionals!!");
  return false;
}

// Check whether the loop can be analyzed by us.
bool isSupported(llvm::Loop *L) {
  // Make sure the loop is in simplified form
  if (!L->isLoopSimplifyForm())
    return false;

  // Only support loops that contain a single exit
  if (!L->getExitingBlock() || !L->getUniqueExitBlock())
    return false;

  return true;
}

// Check if the given basic block is in sub loop of the current loop
bool isInSubLoop(llvm::BasicBlock *BB, llvm::Loop *CurLoop, llvm::LoopInfo *LI) {
  if(!CurLoop->contains(BB)) {
    std::cout << "Check is valid only if the basic block is in the current loop or its sub loop";
  }
  return LI->getLoopFor(BB) != CurLoop;
}

bool hasPhiNode(llvm::Value* v) {
  assert(v);
  if (llvm::isa<llvm::PHINode>(v)) {
    return true;
  } else if(llvm::isa<llvm::BinaryOperator>(v)) {
    llvm::BinaryOperator* bop = llvm::dyn_cast<llvm::BinaryOperator>(v);
    auto op0 = bop->getOperand( 0 );
    auto op1 = bop->getOperand( 1 );
    if(hasPhiNode(op0)) { return true; }
    if(hasPhiNode(op1)) { return true; }
    return false;
  } else {
    return false;
  }
}

llvm::Value* getPhiNode(llvm::Value* expr) {
  assert(expr);
  if (llvm::isa<llvm::PHINode>(expr)) {
    return expr;
  } else if(llvm::isa<const llvm::GlobalVariable>(expr)) {
    return NULL;    // Global Variables are not phi and they don't have phi
  } else if(auto bop = llvm::dyn_cast<llvm::BinaryOperator>(expr)){
    auto phi0 = getPhiNode(bop->getOperand( 0 ));
    if(phi0 != NULL) { return phi0; }
    auto phi1 = getPhiNode(bop->getOperand( 1 ));
    if(phi1 != NULL) { return phi1; }
    return NULL;
  } else {
    return NULL;
  }
}

// Return true of the block has atleast one successor
bool hasSuccessor(const llvm::BasicBlock* b) {
  if(llvm::succ_begin(b) == llvm::succ_end(b)) {
    return false;
  } else {
    return true;
  }
}

// Return the first Basic Block in the Body of the Current Loop
llvm::BasicBlock* getFirstBodyOfLoop(llvm::Loop *CurLoop){
  llvm::BasicBlock* head = CurLoop->getHeader();
  const llvm::TerminatorInst *TInst = head->getTerminator();
  //  const llvm::Instruction *TInst = head->getTerminator();      // LLVM 8
  assert(TInst->isTerminator());
  //  unsigned nbSucc = TInst->getNumSuccessors();
  bool found = false;
  unsigned i = 0;
  llvm::BasicBlock *next = TInst->getSuccessor(i);
  while(!found) {
    if(next!=CurLoop->getExitBlock())
      found=true;
    else{
      i++;
      next = TInst->getSuccessor(i);
    }
  }
  std::cout << "First body block in the current loop is :" << next->getName().str() << "\n";
  return next;
}

std::string getFuncNameForDaikon(llvm::Loop *L) {
  std::string fName = "__TILER_Loop_";
  auto loc = L->getStartLoc();
  fName = fName + std::to_string(loc->getLine());
  return fName;
}

llvm::Function *rand_prototype(llvm::Module *mod, llvm::LLVMContext& glbContext) {
  llvm::FunctionType *rand_type =
    llvm::FunctionType::get( llvm::Type::getInt32Ty(mod->getContext()),
                             false);

  /* llvm::TypeBuilder<int(void), false>::get(glbContext);  // TypeBuilder deprecated from 8.0.1 */

  auto attr_list =  llvm::AttributeList().addAttribute(mod->getContext(), 1U, llvm::Attribute::NoAlias);

  llvm::Constant *c = mod->getOrInsertFunction( "rand", rand_type, attr_list );

  llvm::Function *func = NULL;
  if(llvm::isa<llvm::CastInst>(c)) {
    func = llvm::cast<llvm::Function>( c->getOperand(0));
  } else {
    func = llvm::cast<llvm::Function>( c );
  }
  return func;
}

llvm::Function *printf_prototype(llvm::Module *mod, llvm::LLVMContext& glbContext) {
  llvm::FunctionType *printf_type =
    llvm::FunctionType::get( llvm::Type::getInt32Ty(mod->getContext()),
                             llvm::PointerType::get(llvm::Type::getInt8Ty(mod->getContext()), false),
                             true);
    /* llvm::TypeBuilder<int(char *, ...), false>::get(glbContext); // TypeBuilder deprecated from 8.0.1 */

  auto attr_list =  llvm::AttributeList().addAttribute(mod->getContext(), 1U, llvm::Attribute::NoAlias);

  llvm::Function *func = llvm::cast<llvm::Function>(mod->getOrInsertFunction(
      "printf", printf_type, attr_list ));

  return func;
}

llvm::Function *assume_prototype(llvm::Module *mod, llvm::LLVMContext& glbContext) {
  llvm::FunctionType *assume_type = NULL;
    llvm::FunctionType::get( llvm::Type::getVoidTy(mod->getContext()),
                             llvm::Type::getInt32Ty(mod->getContext()),
                             false);
  //      llvm::TypeBuilder<void(int), false>::get(glbContext);

  auto attr_list = llvm::AttributeList().addAttribute(mod->getContext(), 1U, llvm::Attribute::NoAlias);

  llvm::Function *func = llvm::cast<llvm::Function>(mod->getOrInsertFunction(
      "__llbmc_assume", assume_type, attr_list ));

  return func;
}

llvm::Function *assert_prototype(llvm::Module *mod, llvm::LLVMContext& glbContext) {
  llvm::FunctionType *assert_type = NULL;
    llvm::FunctionType::get( llvm::Type::getVoidTy(mod->getContext()),
                             llvm::Type::getInt32Ty(mod->getContext()),
                             false);
    //      llvm::TypeBuilder<void(int), false>::get(glbContext);

  auto attr_list = llvm::AttributeList().addAttribute(mod->getContext(), 1U, llvm::Attribute::NoAlias);

  llvm::Function *func = llvm::cast<llvm::Function>(mod->getOrInsertFunction(
      "__llbmc_assert", assert_type, attr_list));

  return func;
}

llvm::Constant* geti8StrVal(llvm::Module& M, char const* str, llvm::Twine const& name, llvm::LLVMContext& ctx) {
  //  llvm::LLVMContext& ctx = llvm::getGlobalContext();
  llvm::Constant* strConstant = llvm::ConstantDataArray::getString(ctx, str);
  llvm::GlobalVariable* GVStr =
      new llvm::GlobalVariable(M, strConstant->getType(), true,
                         llvm::GlobalValue::InternalLinkage, strConstant, name);
  llvm::Constant* zero = llvm::Constant::getNullValue(llvm::IntegerType::getInt32Ty(ctx));
  llvm::Constant* indices[] = {zero, zero};
  llvm::Constant* strVal = llvm::ConstantExpr::getGetElementPtr(strConstant->getType(), GVStr, indices, true);
  return strVal;
}

void assertSingleNesting(llvm::Loop *L) {
  if(!L->empty()) {
    L = *L->begin();
    auto it = L->begin();
    it++;
    assert(it == L->end());
    assert(L->empty());
  }
}

void assertNonNesting(llvm::Loop *L) {
  assert(L->empty());
}

bool isIncrOp(llvm::Value *V) {
  bool isAdd=true;
  if( llvm::Instruction *I = llvm::dyn_cast<llvm::Instruction>(V) ) {
    if( auto bop = llvm::dyn_cast<llvm::BinaryOperator>(I) ) {
      auto op0 = bop->getOperand(0);
      auto op1 = bop->getOperand(1);
      unsigned op = bop->getOpcode();
      switch( op ) {
        case llvm::Instruction::Add :
          isAdd=true;
          break;
        case llvm::Instruction::Sub :
          isAdd=false;
          break;
        default:
          std::cout << "\n\nInvalid operation. Must be + or -\n\n";
          exit(1);
      }
      if( llvm::ConstantInt *C = llvm::dyn_cast<llvm::ConstantInt>(op0) ) {
        if(isAdd && C->isNegative()) {
          return false;
        } else if(!isAdd && C->isNegative()) {
          return true;
        }
      } else if( llvm::ConstantInt *C = llvm::dyn_cast<llvm::ConstantInt>(op1) ) {
        if(isAdd && C->isNegative()) {
          return false;
        } else if(!isAdd && C->isNegative()) {
          return true;
        }
      }
    }
  }
  return true;
}

llvm::GetElementPtrInst* getGEP(llvm::LoadInst* load) {
  auto addr = load->getOperand(0);
  if( auto addr_bc = llvm::dyn_cast<llvm::BitCastInst>(addr) )
    addr = addr_bc->getOperand(0);
  if( auto elemPtr = llvm::dyn_cast<llvm::GetElementPtrInst>(addr))
    return elemPtr;
  else
    return NULL; // No GEP for global variables
}

llvm::GetElementPtrInst* getGEP(llvm::StoreInst* store) {
  auto addr = store->getOperand(1);
  if( auto addr_bc = llvm::dyn_cast<llvm::BitCastInst>(addr) )
    addr = addr_bc->getOperand(0);
  if( auto elemPtr = llvm::dyn_cast<llvm::GetElementPtrInst>(addr) )
    return elemPtr;
  else
    return NULL; // No GEP for global variables
}

llvm::Value* getIdx(llvm::LoadInst* load) {
  if(auto elemPtr = getGEP(load)) {
    auto idx = elemPtr->getOperand(1);
    if(elemPtr->getNumIndices() == 2) idx = elemPtr->getOperand(2);
    return idx;
  } else {
    return NULL; // No indices for global variables
  }
}

llvm::Value* getIdx(llvm::StoreInst* store) {
  if(auto elemPtr = getGEP(store)) {
    auto idx = elemPtr->getOperand(1);
    if(elemPtr->getNumIndices() == 2) idx = elemPtr->getOperand(2);
    return idx;
  } else {
    return NULL; // No indices for global variables
  }
}

llvm::AllocaInst* getAlloca(llvm::LoadInst* load) {
  if(auto elemPtr = getGEP(load)) {
    auto arry = elemPtr->getPointerOperand();
    llvm::AllocaInst *arry_alloca = llvm::dyn_cast<llvm::AllocaInst>(arry);
    return arry_alloca;
  } else {
    return NULL; // No alloca inst for global variables
  }
}

llvm::AllocaInst* getAlloca(llvm::StoreInst* store) {
  if(auto elemPtr = getGEP(store)) {
    auto arry = elemPtr->getPointerOperand();
    llvm::AllocaInst *arry_alloca = llvm::dyn_cast<llvm::AllocaInst>(arry);
    return arry_alloca;
  } else {
    return NULL; // No alloca inst for global variables
  }
}

llvm::Loop* getNextLoop(std::list<llvm::Loop*> lList, llvm::Loop* L) {
  bool flag = false;
  for(llvm::Loop* lo : lList) {
    if(flag) {
      return lo;
    }
    if(lo == L) {
      flag = true;
    }
  }
  return NULL;
}

llvm::Value* getArrValueFromZ3Expr(llvm::Value *V, z3::expr e, llvm::IRBuilder<> &irb, llvm::LLVMContext& c, std::map<std::string, llvm::Value*>& exprValMap, std::set<llvm::Value*>& arrSet) {
  llvm::Value *res = getValueFromZ3Expr(e, irb, c, exprValMap, arrSet);
  if(V != NULL ) {
    res = irb.CreateGEP(V, res);
    res = irb.CreateLoad(res);
    res = irb.CreateSExt(res, llvm::IntegerType::getInt64Ty(c));
    assert(res);
  }
  return res;
}

llvm::Value* getValueFromZ3Expr(z3::expr e, llvm::IRBuilder<> &irb, llvm::LLVMContext& c, std::map<std::string, llvm::Value*>& exprValMap, std::set<llvm::Value*>& arrSet) {
  llvm::Value *res = NULL;
  if(e.is_numeral()) {
    int64_t num;
    if (Z3_get_numeral_int64(e.ctx(), e, &num)) {
      res = llvm::ConstantInt::get(llvm::IntegerType::getInt32Ty(c), num);
      assert(res);
    }
  } else if (e.is_var()) {
    std::string varName = e.decl().name().str();
    //    std::cout << "\n Getting Val for Expr: " << varName << "\n";
    res = exprValMap.at(varName);
    assert(res);
    if(llvm::isa<llvm::GlobalVariable>(res)) {
      res = irb.CreateLoad(res);
    } else {
      //      res = irb.CreateSExt(res, llvm::IntegerType::getInt64Ty(c));
    }
    assert(res);
  } else if (e.is_app()) {
    //    std::cout << "\n Getting val for subexpr \n";
    res = getValueFromZ3SubExpr(e, irb, c, exprValMap, arrSet);
    assert(res);
  } else if (e.is_quantifier()) {
    tiler_error("llvmUtils", "encountered a quantifier");
  }
  return res;
}

llvm::Value* getValueFromZ3SubExpr(z3::expr e, llvm::IRBuilder<> &irb, llvm::LLVMContext& c, std::map<std::string, llvm::Value*>& exprValMap, std::set<llvm::Value*>& arrSet) {
  std::list<llvm::Value*> argValList;

  unsigned args = e.num_args();
  for (unsigned i = 0; i<args; i++)
  {
    z3::expr arg = e.arg(i);
    argValList.push_back(getValueFromZ3Expr(arg, irb, c, exprValMap, arrSet));
  }

  Z3_decl_kind dk = e.decl().decl_kind();
  std::list<llvm::Value*>::const_iterator argListIt;
  argListIt = argValList.begin();

  if (dk == Z3_OP_MUL) {
    llvm::Value* res = *argListIt;
    assert(res);
    argListIt++;
    for(;argListIt != argValList.end(); argListIt++)
    {
      assert(*argListIt);
      res = irb.CreateMul(res, *argListIt);
      assert(res);
    }
    return res;
  } else if (dk == Z3_OP_ADD) {
    llvm::Value* res = *argListIt;
    assert(res);
    argListIt++;
    for(;argListIt != argValList.end(); argListIt++)
    {
      assert(*argListIt);
      res = irb.CreateAdd(res, *argListIt);
      assert(res);
    }
    return res;
  } else if (dk == Z3_OP_SUB) {
    llvm::Value* res = *argListIt;
    assert(res);
    argListIt++;
    for(;argListIt != argValList.end(); argListIt++)
    {
      assert(*argListIt);
      res = irb.CreateSub(res, *argListIt);
      assert(res);
    }
    return res;
  } else if (dk == Z3_OP_DIV || dk == Z3_OP_IDIV) {
    llvm::Value* res = *argListIt;
    assert(res);
    argListIt++;
    for(;argListIt != argValList.end(); argListIt++)
    {
      assert(*argListIt);
      res = irb.CreateSDiv(res, *argListIt);
      assert(res);
    }
    return res;
  } else if (dk == Z3_OP_REM) {
    llvm::Value* res = *argListIt;
    assert(res);
    argListIt++;
    for(;argListIt != argValList.end(); argListIt++)
    {
      assert(*argListIt);
      res = irb.CreateSRem(res, *argListIt);
      assert(res);
    }
    return res;
  } else if (dk == Z3_OP_UMINUS) {
    llvm::Value* res = irb.CreateNeg(*argListIt);
    assert(res);
    return res;
  } else if (dk == Z3_OP_SELECT) {
    //    std::cout << "\n Found a select statement with " << args << " args \n";
    llvm::Value* Arr = *argListIt;
    assert(Arr);
    argListIt++;
    llvm::Value* Ind = *argListIt;
    assert(Ind);
    llvm::Value* res = irb.CreateGEP(Arr, Ind);
    assert(res);
    res = irb.CreateLoad(res);
    assert(res);
    //    res = irb.CreateSExt(res, llvm::IntegerType::getInt64Ty(c));
    assert(res);
    return res;
  } else if (dk == Z3_OP_NOT) {
    llvm::Value* res = irb.CreateNot(*argListIt);
    assert(res);
    return res;
  } else if (dk == Z3_OP_AND) {
    llvm::Value* res = *argListIt;
    assert(res);
    argListIt++;
    for(;argListIt != argValList.end(); argListIt++)
    {
      assert(*argListIt);
      res = irb.CreateAnd(res, *argListIt);
      assert(res);
    }
    res = irb.CreateSExt(res, llvm::IntegerType::getInt32Ty(c));
    assert(res);
    return res;
  } else if (dk == Z3_OP_OR) {
    llvm::Value* res = *argListIt;
    assert(res);
    argListIt++;
    for(;argListIt != argValList.end(); argListIt++)
    {
      assert(*argListIt);
      res = irb.CreateOr(res, *argListIt);
      assert(res);
    }
    res = irb.CreateSExt(res, llvm::IntegerType::getInt32Ty(c));
    assert(res);
    return res;
  } else if (dk == Z3_OP_EQ) {
    llvm::Value* res = *argListIt;
    assert(res);
    argListIt++;
    for(;argListIt != argValList.end(); argListIt++)
    {
      assert(*argListIt);
      res = irb.CreateICmpEQ(res, *argListIt);
      assert(res);
    }
    res = irb.CreateSExt(res, llvm::IntegerType::getInt32Ty(c));
    assert(res);
    return res;
  } else if (dk == Z3_OP_GE) {
    llvm::Value* res = *argListIt;
    assert(res);
    argListIt++;
    for(;argListIt != argValList.end(); argListIt++)
    {
      assert(*argListIt);
      res = irb.CreateICmpSGE(res, *argListIt);
      assert(res);
    }
    res = irb.CreateSExt(res, llvm::IntegerType::getInt32Ty(c));
    assert(res);
    return res;
  } else if (dk == Z3_OP_GT) {
    llvm::Value* res = *argListIt;
    assert(res);
    argListIt++;
    for(;argListIt != argValList.end(); argListIt++)
    {
      assert(*argListIt);
      res = irb.CreateICmpSGT(res, *argListIt);
      assert(res);
    }
    res = irb.CreateSExt(res, llvm::IntegerType::getInt32Ty(c));
    assert(res);
    return res;
  } else if (dk == Z3_OP_LE) {
    llvm::Value* res = *argListIt;
    assert(res);
    argListIt++;
    for(;argListIt != argValList.end(); argListIt++)
    {
      assert(*argListIt);
      res = irb.CreateICmpSLE(res, *argListIt);
      assert(res);
    }
    res = irb.CreateSExt(res, llvm::IntegerType::getInt32Ty(c));
    assert(res);
    return res;
  } else if (dk == Z3_OP_LT) {
    llvm::Value* res = *argListIt;
    assert(res);
    argListIt++;
    for(;argListIt != argValList.end(); argListIt++)
    {
      assert(*argListIt);
      res = irb.CreateICmpSLT(res, *argListIt);
      assert(res);
    }
    res = irb.CreateSExt(res, llvm::IntegerType::getInt32Ty(c));
    assert(res);
    return res;
  } else {
    std::string varName = e.decl().name().str();
    llvm::Value* res = exprValMap.at(varName);
    assert(res);
    if(arrSet.count(res) == 0) {
      if(llvm::isa<llvm::GlobalVariable>(res)) {
        res = irb.CreateLoad(res);
      } else {
        //  res = irb.CreateSExt(res, llvm::IntegerType::getInt64Ty(c));
      }
      assert(res);
    }
    return res;
  }
}

bool checkRangeOverlap(z3::expr range1, z3::expr range2) {
  z3::context& z3_ctx = range1.ctx();
  z3::expr_vector ipsrc(z3_ctx);
  z3::expr_vector ipdst(z3_ctx);

  std::string lbName = "__lb";
  std::string ubName = "__ub";
  z3::expr lb = z3_ctx.int_const(lbName.c_str());
  z3::expr ub = z3_ctx.int_const(ubName.c_str());

  std::string lbpName = lbName+"_p";
  std::string ubpName = ubName+"_p";
  z3::expr lbp = z3_ctx.int_const(lbpName.c_str());
  z3::expr ubp = z3_ctx.int_const(ubpName.c_str());

  ipsrc.push_back(ub);
  ipsrc.push_back(lb);
  ipdst.push_back(ubp);
  ipdst.push_back(lbp);

  z3::expr sub_range2 = range2.substitute(ipsrc, ipdst);

  z3::solver s(z3_ctx);
  s.add(range1);
  s.add(sub_range2);
  if (s.check() == z3::sat) {
    return true;
  } else {
    return false;
  }
}

void collect_fun_bb(llvm::Function* f, std::vector<llvm::BasicBlock*>& fun_bb_vec) {
  for( auto bbit = f->begin(), end = f->end(); bbit != end; bbit++ ) {
    llvm::BasicBlock* bb = &(*bbit);
    fun_bb_vec.push_back(bb);
  }
}

void get_topologically_sorted_bb(llvm::Function *f,
                                 std::vector<llvm::BasicBlock*>& fun_bb_vec) {
  llvm::ReversePostOrderTraversal<llvm::Function*> RPOT(f);
  for (llvm::ReversePostOrderTraversal<llvm::Function*>::rpo_iterator RI = RPOT.begin(),
         RE = RPOT.end(); RI != RE; ++RI) {
    llvm::BasicBlock* b = *RI;
    fun_bb_vec.push_back(b);
    // b->print( llvm::outs() ); llvm::outs() << "\n\n";
  }
}

void collect_arr(llvm::Function &f, std::set<llvm::AllocaInst*>& arrSet) {
  arrSet.clear();
  for( auto bbit = f.begin(), end = f.end(); bbit != end; bbit++ ) {
    llvm::BasicBlock* bb = &(*bbit);
    for( llvm::Instruction& Iobj : bb->getInstList() ) {
      llvm::Instruction* I = &(Iobj);
      if( auto alloc = llvm::dyn_cast<llvm::AllocaInst>(I) ) {
        if( alloc->isArrayAllocation() &&
            !alloc->getType()->getElementType()->isIntegerTy()
            && !alloc->getType()->getElementType()->isFloatTy()) {
          tiler_error( "llvmUtils", "only pointers to intergers and floats is allowed!" );
        }
        arrSet.insert( alloc );
      }
    }
  }
}

bool is_assume_call(const llvm::CallInst* call) {
  assert( call );
  llvm::Function* fp = call->getCalledFunction();
  if( fp != NULL &&
      (fp->getName() == "_Z6assumeb" || fp->getName() == "assume"
       || fp->getName() == "assume_abort_if_not" ||
       fp->getName().startswith("__VERIFIER_assume") ) ) {
    return true;
  } else if (fp == NULL) {
    const llvm::Value * val = call->getCalledValue();
    if( auto CE = llvm::dyn_cast<llvm::ConstantExpr>(val) ) {
      if(CE->isCast()) {
        if(CE->getOperand(0)->getName() == "assume" ||
           CE->getOperand(0)->getName() == "assume_abort_if_not" ||
           CE->getOperand(0)->getName() == "_Z6assumeb" ||
           CE->getOperand(0)->getName().startswith("__VERIFIER_assume")) {
          return true;
        }
      }
    }
  }
  return false;
}

bool is_assert_call(const llvm::CallInst* call ) {
  assert( call );
  llvm::Function* fp = call->getCalledFunction();
  if( fp != NULL &&
      (fp->getName() == "_Z6assertb" || fp->getName() == "assert" ||
       fp->getName().startswith("__VERIFIER_assert") ) ) {
    return true;
  } else if (fp == NULL) {
    const llvm::Value * val = call->getCalledValue();
    if( auto CE = llvm::dyn_cast<llvm::ConstantExpr>(val) ) {
      if(CE->isCast()) {
        if(CE->getOperand(0)->getName() == "assert" ||
           CE->getOperand(0)->getName() == "_Z6assertb" ||
           CE->getOperand(0)->getName().startswith("__VERIFIER_assert") ) {
          return true;
        }
      }
    }
  }
  return false;
}

bool is_assert_loop( llvm::Loop* L ) {
  bool assert_seen=false;
  for( auto bb: L->getBlocks() ) {
    for( auto it = bb->begin(), e = bb->end(); it != e; ++it) {
      llvm::Instruction *I = &(*it);
      if( auto call = llvm::dyn_cast<llvm::CallInst>(I) ) {
        if(llvm::isa<llvm::DbgValueInst>(I) ||llvm::isa<llvm::DbgDeclareInst>(I)){
          // Ignore debug instructions
        }else{
          assert_seen = assert_seen || is_assert_call(call);
        }
      }
    }
  }
  return assert_seen;
}

class bb_succ_iter : public llvm::succ_const_iterator {
public:
  bb_succ_iter( llvm::succ_const_iterator begin_,
                llvm::succ_const_iterator end_,
                std::set<const llvm::BasicBlock*>& back_edges ) :
    llvm::succ_const_iterator( begin_ ), end(end_), b_edges( back_edges ) {
    llvm::succ_const_iterator& it = (llvm::succ_const_iterator&)*this;
    while( it != end && exists( b_edges, (const llvm::BasicBlock*)*it) ) ++it;
  };

  bb_succ_iter( llvm::succ_const_iterator begin_,
                llvm::succ_const_iterator end_ ) :
    llvm::succ_const_iterator( begin_ ), end(end_) {};

  bb_succ_iter( llvm::succ_const_iterator end_ ) :
    llvm::succ_const_iterator( end_ ), end( end_ ) {};

  llvm::succ_const_iterator end;
  std::set<const llvm::BasicBlock*> b_edges;
  bb_succ_iter& operator++() {
    llvm::succ_const_iterator& it = (llvm::succ_const_iterator&)*this;
    do{
      ++it;
    }while( it != end && exists( b_edges, (const llvm::BasicBlock*)*it) );
    return *this;
  }
};

void computeTopologicalOrder( llvm::Function &F,
                              std::map<const llvm::BasicBlock*,std::set<const llvm::BasicBlock*>>& bedges,
                              std::vector<const llvm::BasicBlock*>& bs,
                              std::map< const llvm::BasicBlock*, unsigned >& o_map) {

  auto f = [&bedges](const llvm::BasicBlock* b) {
    if( exists( bedges, b ) ) {
      return bb_succ_iter( llvm::succ_begin(b), llvm::succ_end(b),bedges.at(b));
    }else{
      return bb_succ_iter( llvm::succ_begin(b), llvm::succ_end(b));
    }
  };

  auto e = [](const llvm::BasicBlock* b) { return bb_succ_iter( llvm::succ_end(b) ); };

  const llvm::BasicBlock* h = &F.getEntryBlock();
  bs.clear();
  o_map.clear();
  topological_sort<const llvm::BasicBlock*, bb_succ_iter>( h, f, e, bs, o_map );
}

// Pass p must declare it requires LoopInfoWrapperPass
void collect_loop_backedges(llvm::Pass *p,
                            std::map< const llvm::BasicBlock*,
                                      std::set<const llvm::BasicBlock*>>& loop_ignore_edge,
                            std::map< const llvm::BasicBlock*,
                                      std::set<const llvm::BasicBlock*>>& rev_loop_ignore_edge) {

  //todo: llvm::FindFunctionBackedges could have done the job
  auto &LIWP = p->getAnalysis<llvm::LoopInfoWrapperPass>();
  auto LI = &LIWP.getLoopInfo();
  std::vector<llvm::Loop*> loops, stack;
  for(auto I = LI->rbegin(), E = LI->rend(); I != E; ++I) stack.push_back(*I);
  while( !stack.empty() ) {
    llvm::Loop *L = stack.back();
    stack.pop_back();
    loops.push_back( L );
    for(auto I = L->begin(), E = L->end(); I != E; ++I) stack.push_back(*I);
  }
  loop_ignore_edge.clear();
  rev_loop_ignore_edge.clear();
  for( llvm::Loop *L : loops ) {
    auto h = L->getHeader();
    llvm::SmallVector<llvm::BasicBlock*,10> LoopLatches;
    L->getLoopLatches( LoopLatches );
    for( llvm::BasicBlock* bb : LoopLatches ) {
      loop_ignore_edge[h].insert( bb );
      rev_loop_ignore_edge[bb].insert(h);
    }
  }
}

void collect_block_ancestors(llvm::BasicBlock* b,
                             std::map<llvm::BasicBlock*, std::set<llvm::BasicBlock*>>& block_preds_map) {
  auto& preds = block_preds_map[b];
  std::vector<llvm::BasicBlock*> stack;
  std::vector<llvm::BasicBlock*> processed;
  processed.push_back(b);
  for (llvm::BasicBlock *pb : llvm::predecessors(b)) {
    if(block_preds_map.find(pb) != block_preds_map.end()) {
      preds.insert( pb );
      processed.push_back(pb);
      for(llvm::BasicBlock *ab : block_preds_map.at(pb)) {
        preds.insert( ab );
        processed.push_back(ab);
      }
    } else {
      stack.push_back(pb);
      processed.push_back(pb);
    }
  }
  while( !stack.empty() ) {
    llvm::BasicBlock *pb = stack.back();
    stack.pop_back();
    preds.insert( pb );
    for (llvm::BasicBlock *ppb : llvm::predecessors(pb)) {
      if(find(processed.begin(), processed.end(), ppb) == processed.end()) {
        stack.push_back(ppb);
        processed.push_back(ppb);
      }
    }
  }
}

llvm::Value* collect_loads_recs(llvm::Value* v, llvm::Value* N, std::list<llvm::Value*>& load_lst) {
  llvm::Value* rec_t = NULL;
  if(llvm::isa<llvm::LoadInst>(v)) {
      load_lst.push_back(v);
  } else if(auto bop = llvm::dyn_cast<llvm::BinaryOperator>(v)) {
    if(isRemInBop(bop)) return NULL;
    assert( bop );
    llvm::Value* rec_t0 = NULL;
    llvm::Value* rec_t1 = NULL;
    auto op0 = bop->getOperand( 0 );
    auto op1 = bop->getOperand( 1 );
    if(llvm::isa<llvm::BinaryOperator>(op0)) {
      rec_t0 = collect_loads_recs(op0, N, load_lst);
    } else if(auto op0_l = llvm::dyn_cast<llvm::LoadInst>(op0)) {
      if(op0_l->getOperand(0) != N) load_lst.push_back(op0_l);
      else rec_t0 = op0_l;
    } else rec_t0 = op0;
    if(llvm::isa<llvm::BinaryOperator>(op1)) {
      rec_t1 = collect_loads_recs(op1, N, load_lst);
    } else if(auto op1_l = llvm::dyn_cast<llvm::LoadInst>(op1)) {
      if(op1_l->getOperand(0) != N) load_lst.push_back(op1_l);
      else rec_t1 = op1_l;
    } else rec_t1 = op1;
    if(rec_t0 != NULL) rec_t = rec_t0;
    if(rec_t1 != NULL) rec_t = rec_t1;
    if(rec_t0 != NULL && rec_t1 != NULL) rec_t = bop;
  } else {
    rec_t = v;
  }
  return rec_t;
}

// Pass p must declare it requires LoopInfoWrapperPass
void find_cutpoints(llvm::Pass* P, llvm::Function &f, std::vector< llvm::BasicBlock* >& cutPoints) {
  cutPoints.clear();
  cutPoints.push_back(&f.getEntryBlock());
  std::vector<llvm::Loop*> stack;
  auto &LIWP = P->getAnalysis<llvm::LoopInfoWrapperPass>();
  auto LI = &LIWP.getLoopInfo();
  for(auto I = LI->rbegin(), E = LI->rend(); I != E; ++I) stack.push_back(*I);
  while( !stack.empty() ) {
    llvm::Loop *L = stack.back();
    stack.pop_back();
    cutPoints.push_back( L->getHeader() );
    for(auto I = L->begin(), E = L->end(); I != E; ++I) stack.push_back(*I);
  }
}

void create_segments(llvm::Function &f,
                     std::vector< llvm::BasicBlock* >& cutPoints,
                     std::vector< segment >& segVec) {
  segVec.clear();
  std::map< llvm::BasicBlock*, bool > bbVisited;
  std::vector< llvm::BasicBlock* > stack;

  for (auto fi = f.begin(), fe = f.end(); fi != fe; ++fi)  bbVisited[&(*fi)] = false;

  for(llvm::BasicBlock* bb : cutPoints) {
    for (llvm::succ_iterator sit = succ_begin(bb), set = succ_end(bb); sit != set; ++sit) {
      llvm::BasicBlock* b = *sit;
      segment s;
      s.entryCutPoints.push_back(bb);
      bbVisited[bb] = true;
      std::map<std::string, llvm::Value*> nameValueMap;
      buildBlockMap(bb, nameValueMap);
      s.assuMapCPs[bb] = nameValueMap;
      if(exists(cutPoints, b)) {
        if(!exists(s.exitCutPoints, b)) {
          s.exitCutPoints.push_back(b);
        }
        std::map<std::string, llvm::Value*> nameValueMap;
        buildBlockMap(b, nameValueMap);
        s.assertMapCPs[b] = nameValueMap;
      } else if(!bbVisited.at(b)) {
        stack.push_back(b);
        while(!stack.empty()) {
          llvm::BasicBlock* sbb = stack.back();
          if(bbVisited.at(sbb)) {
            stack.pop_back();
          } else {
            s.bodyBlocks.push_back(sbb);
            bbVisited[sbb] = true;
            stack.pop_back();
            for (llvm::succ_iterator sit = succ_begin(sbb), set = succ_end(sbb); sit != set; ++sit) {
              llvm::BasicBlock* b = *sit;
              if(exists(cutPoints, b)) {
                if(!exists(s.exitCutPoints, b)) {
                  s.exitCutPoints.push_back(b);
                }
                std::map<std::string, llvm::Value*> nameValueMap;
                buildBlockMap(b, nameValueMap);
                s.assertMapCPs[b] = nameValueMap;
              } else if(!bbVisited.at(b)) {
                stack.push_back(b);
              }
            }
            for (llvm::pred_iterator pit = pred_begin(sbb), pet = pred_end(sbb); pit != pet; ++pit) {
              llvm::BasicBlock* b = *pit;
              if(exists(cutPoints, b)) {
                if(!exists(s.entryCutPoints, b)) {
                  s.entryCutPoints.push_back(b);
                }
                std::map<std::string, llvm::Value*> nameValueMap;
                buildBlockMap(b, nameValueMap);
                s.assuMapCPs[b] = nameValueMap;
              } else if(!bbVisited.at(b)) {
                stack.push_back(b);
              }
            }
          }
        }
        if(!s.bodyBlocks.empty()) {
          segVec.push_back(s);
        }
      }
    }
  }
}

void buildBlockMap(llvm::BasicBlock* bb, std::map<std::string, llvm::Value*>& nameValueMap) {
  for (llvm::Instruction &II : *bb){
    llvm::Instruction* I = &II;
    llvm::Value* var = NULL;
    llvm::MDNode* md = NULL;
    std::string str;
    if( llvm::DbgDeclareInst* dDecl =
        llvm::dyn_cast<llvm::DbgDeclareInst>(I) ) {
      var = dDecl->getAddress();
      md = dDecl->getVariable();
      llvm::DIVariable* diMd = llvm::dyn_cast<llvm::DIVariable>(md);
      str = (std::string)( diMd->getName() );
    } else if( llvm::DbgValueInst* dVal =
               llvm::dyn_cast<llvm::DbgValueInst>(I)) {
      var = dVal->getValue();
      md = dVal->getVariable();
      llvm::DIVariable* diMd = llvm::dyn_cast<llvm::DIVariable>(md);
      str = (std::string)( diMd->getName() );
    }
    if( var ) {
      nameValueMap[str] = var;
    }
  }
}

int readInt( const llvm::ConstantInt* c ) {
  const llvm::APInt& n = c->getUniqueInteger();
  unsigned len = n.getNumWords();
  if( len > 1 ) tiler_error("llvmUtils", "long integers not supported!!" );
  const uint64_t *v = n.getRawData();
  return *v;
}

// Remove a loop
bool deleteLoop(llvm::Loop *L, llvm::DominatorTree &DT, llvm::ScalarEvolution &SE,
                llvm::LoopInfo &LI) {

  llvm::SmallPtrSet<llvm::BasicBlock *, 8> blocks;
  blocks.insert(L->block_begin(), L->block_end());

  llvm::BasicBlock *preheader = L->getLoopPreheader();
  if (!preheader)
    return false;

  llvm::SmallVector<llvm::BasicBlock *, 4> exitingBlocks;
  L->getExitingBlocks(exitingBlocks);

  llvm::SmallVector<llvm::BasicBlock *, 4> exitBlocks;
  L->getUniqueExitBlocks(exitBlocks);
  // Single exit block to branch to
  if (exitBlocks.size() != 1)
    return false;

  // Tell ScalarEvolution that the loop is deleted
  SE.forgetLoop(L);

  // Connect the preheader directly to the exit block
  llvm::TerminatorInst *TI = preheader->getTerminator();
  //  llvm::Instruction *TI = preheader->getTerminator();      // LLVM 8
  assert(TI && TI->isTerminator());
  llvm::BasicBlock *exitBlock = exitBlocks[0];
  TI->replaceUsesOfWith(L->getHeader(), exitBlock);

  // Rewrite phis in the exit block to get their inputs from
  // the preheader instead of the exiting block
  llvm::BasicBlock *exitingBlock = exitingBlocks[0];
  llvm::BasicBlock::iterator BI = exitBlock->begin();
  while (llvm::PHINode *P = llvm::dyn_cast<llvm::PHINode>(BI)) {
    int j = P->getBasicBlockIndex(exitingBlock);
    assert(j >= 0 && "Can't find exiting block in exit block's phi node!");
    P->setIncomingBlock(j, preheader);
    for (unsigned i = 1; i < exitingBlocks.size(); ++i)
      P->removeIncomingValue(exitingBlocks[i]);
    ++BI;
  }
  std::cout << "Updated the Phi nodes of the exit block\n";

  // Update the dominator tree
  // Remove  blocks that will be deleted from the reference counting scheme
  llvm::SmallVector<llvm::DomTreeNode*, 8> ChildNodes;
  for (llvm::Loop::block_iterator LBI = L->block_begin(), LE = L->block_end();
       LBI != LE; ++LBI) {
    // Move all of the block's children to be children of the preheader in DT
    // Remove DT entry for the block
    ChildNodes.insert(ChildNodes.begin(), DT[*LBI]->begin(), DT[*LBI]->end());
    for (llvm::DomTreeNode *ChildNode : ChildNodes) {
      DT.changeImmediateDominator(ChildNode, DT[preheader]);
    }
    ChildNodes.clear();
    DT.eraseNode(*LBI);

    // Remove the block from the reference counting
    (*LBI)->dropAllReferences();
  }

  // std::cout << "Erase the blocks from the loop";
  for (llvm::BasicBlock *BB : blocks)
    BB->eraseFromParent();

  // Remove the blocks from loopinfo
  for (llvm::BasicBlock *BB : blocks)
    LI.removeBlock(BB);

  // Update LoopInfo
// #ifndef LLVM_SVN
//   LI.markAsRemoved(L);
// #endif

  return true;
}


//todo: streamline getLoc and getLocation
src_loc
getLoc( const llvm::Instruction* I ) {
  if( auto dbg = llvm::dyn_cast<llvm::DbgInfoIntrinsic>(I) ) {
    //    if( auto dbg = llvm::dyn_cast<llvm::DbgVariableIntrinsic>(dbgi) ) {
      auto loc = dbg->getVariableLocation();
      if( auto I_val = llvm::dyn_cast<llvm::Instruction>(loc) ) {
        if( I_val ) I = I_val;
      }else if( llvm::dyn_cast<llvm::Constant>(loc) ) {
        // what to do??
      } else {
        tiler_error("llvmUtils", "Unknown type");
      }
      //    } else {
      //      tiler_error("llvmUtils", "Not handled type of Info Intrinsic Inst");
      //    }
  }
  const llvm::DebugLoc d = I->getDebugLoc();
  if( d ) {
    unsigned l = d.getLine();
    unsigned c  = d.getCol();
    std::string f =llvm::cast<llvm::DIScope>(d.getScope())->getFilename();
    return src_loc(l,c,f);
  }
  return src_loc();
}

std::string getLocation(const llvm::Instruction* I ) {
  const llvm::DebugLoc d = I->getDebugLoc();
  if( d ) {
    unsigned line = d.getLine();
    unsigned col  = d.getCol();
    auto *Scope = llvm::cast<llvm::DIScope>(d.getScope());
    std::string fname = Scope->getFilename();
    std::string l_name =  fname + ":" + std::to_string(line) + ":" + std::to_string(col);
    return l_name;
  }else{
    return "";
  }
}

std::string getLocation(const llvm::BasicBlock* b ) {
  auto I = b->getFirstNonPHIOrDbg();
  return getLocation(I);
}


std::string getLocRange(const llvm::BasicBlock* b ) {
  unsigned minLine = std::numeric_limits<unsigned>::max();
  unsigned minCol  = std::numeric_limits<unsigned>::max();
  unsigned maxLine = 0;
  unsigned maxCol  = 0;
  std::string fname = "";
  for( const llvm::Instruction& Iobj : b->getInstList() ) {
    const llvm::Instruction* I = &(Iobj);
    const llvm::DebugLoc d = I->getDebugLoc();
    if( d ) {
      unsigned line = d.getLine();
      unsigned col  = d.getCol();
      auto *Scope = llvm::cast<llvm::DIScope>(d.getScope());
      if( fname.empty() ) fname = Scope->getFilename();
      if( line < minLine ) {
        minLine = line;
        minCol = col;
      }else if( line == minLine ) {
        if( col < minCol )
          minCol = col;
      }
      if( line > maxLine ) {
        maxLine = line;
        maxCol = col;
      }else if( line == maxLine ) {
        if( col > maxCol )
          maxCol = col;
      }
    }
  }
  std::string l_name =  fname + ":"
    + std::to_string(minLine) + ":"+  std::to_string(minCol) + "-"
    + std::to_string(maxLine) + ":"+  std::to_string(maxCol);

  return l_name;
}

z3::sort llvm_to_z3_sort( z3::context& c, llvm::Type* t ) {
  if( t->isIntegerTy() ) {
    if( t->isIntegerTy( 16 ) ) return c.int_sort();
    if( t->isIntegerTy( 32 ) ) return c.int_sort();
    if( t->isIntegerTy( 64 ) ) return c.int_sort();
    if( t->isIntegerTy( 8 ) ) return c.bool_sort();
    if( t->isIntegerTy( 1 ) ) return c.bool_sort();
  }
  if( t->isArrayTy() ) {
    llvm::Type* te = t->getArrayElementType();
    z3::sort z_te = llvm_to_z3_sort(c, te);
    return c.array_sort( c.int_sort(), z_te );
  }
  if( t->isFloatTy() ) {
    return c.real_sort();
  }
  tiler_error("llvmUtils", "only int and bool sorts are supported");
  // return c.bv_sort(32); // needs to be added
  // return c.bv_sort(16);
  // return c.bv_sort(64);
  // return c.bool_sort();
  // return c.real_sort();
  return c.int_sort(); // dummy return
}

z3::expr read_const( const llvm::Value* op, z3::context& ctx ) {
  assert( op );
  if( const llvm::ConstantInt* c = llvm::dyn_cast<llvm::ConstantInt>(op) ) {
    unsigned bw = c->getBitWidth();
    if(bw == 16 || bw == 32 || bw == 64 ) {
      int i = readInt( c );
      return ctx.int_val(i);
    }else if(bw == 1 || bw == 8) {
      int i = readInt( c );
      assert( i == 0 || i == 1 );
      if( i == 1 ) return ctx.bool_val(true); else return ctx.bool_val(false);
    }else
      tiler_error("llvmUtils", "unrecognized constant!" );
  }else if( llvm::isa<llvm::ConstantPointerNull>(op) ) {
    tiler_error("llvmUtils", "Constant pointer are not implemented!!" );
    // }else if( LLCAST( llvm::ConstantPointerNull, c, op) ) {
    return ctx.int_val(0);
  }else if( llvm::isa<llvm::UndefValue>(op) ) {
    llvm::Type* ty = op->getType();
    if( auto i_ty = llvm::dyn_cast<llvm::IntegerType>(ty) ) {
      int bw = i_ty->getBitWidth();
      if(bw == 16 || bw == 32 || bw == 64 ) { return get_fresh_int(ctx);
      }else if(      bw == 1  ) { return get_fresh_bool(ctx);
      }
    }
    tiler_error("llvmUtils", "unsupported type: "<< ty << "!!");
  }else if(const llvm::ConstantFP* c = llvm::dyn_cast<llvm::ConstantFP>(op) ) {
    const llvm::APFloat& n = c->getValueAPF();
    double v = n.convertToDouble();
    return ctx.real_val(std::to_string(v).c_str());
    //tiler_error("llvmUtils", "Floating point constant not implemented!!" );
  }else if( llvm::isa<llvm::Constant>(op) ) {
    tiler_error("llvmUtils", "non int constants are not implemented!!" );
    std::cerr << "un recognized constant!";
    //     // int i = readInt(c);
    //     // return eHandler->mkIntVal( i );
  }else if( llvm::isa<llvm::ConstantExpr>(op) ) {
    tiler_error("llvmUtils", "case for constant not implemented!!" );
  }else if( llvm::isa<llvm::ConstantArray>(op) ) {
    // const llvm::ArrayType* n = c->getType();
    // unsigned len = n->getNumElements();
    //return ctx.arraysort();
    tiler_error("llvmUtils", "case for constant not implemented!!" );
  }else if( llvm::isa<llvm::ConstantStruct>(op) ) {
    // const llvm::StructType* n = c->getType();
    tiler_error("llvmUtils", "case for constant not implemented!!" );
  }else if( llvm::isa<llvm::ConstantVector>(op) ) {
    // const llvm::VectorType* n = c->getType();
    tiler_error("llvmUtils", "vector constant not implemented!!" );
  }
  z3::expr e(ctx);
  return e; // contains no expression;
}

std::vector<llvm::BasicBlock*> to_std_vec(llvm::SmallVector<llvm::BasicBlock*, 20>& bb_sv) {
  std::vector<llvm::BasicBlock*> bb_stdv;
  for(bb* b : bb_sv) {
    bb_stdv.push_back(b);
  }
  return bb_stdv;
}

llvm::SmallVector<llvm::BasicBlock*, 40> to_small_vec(std::vector<llvm::BasicBlock*>& bb_vec) {
  llvm::SmallVector<llvm::BasicBlock*, 40> bb_sv;
  for(bb* b : bb_vec) {
    bb_sv.push_back(b);
  }
  return bb_sv;
}

bool cmp_loop_by_line_num (llvm::Loop* l1, llvm::Loop* l2) {
  assert(l1); assert(l2);
  const llvm::DebugLoc d1 = l1->getStartLoc();
  const llvm::DebugLoc d2 = l2->getStartLoc();
  if(d1.getLine() < d2.getLine()) {
    return true;
  } else {
    return false;
  }
}

void remove_loop_back_edge (llvm::Loop* L) {
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

void
populate_arr_rd_wr(llvm::BasicBlock* bb,
                   std::map<llvm::Value*, std::list<llvm::Value*>>& arrRead,
                   std::map<llvm::Value*, std::list<llvm::Value*>>& arrWrite) {
  arrRead.clear();
  arrWrite.clear();
  for( auto it = bb->begin(), e = bb->end(); it != e; ++it) {
    llvm::Instruction *I = &(*it);
    if( auto store = llvm::dyn_cast<llvm::StoreInst>(I) ) {
      auto addr = store->getOperand(1);
      if( auto addr_bc = llvm::dyn_cast<llvm::BitCastInst>(addr) )
        addr = addr_bc->getOperand(0);
      if( auto elemPtr = llvm::dyn_cast<llvm::GetElementPtrInst>(addr) ) {
        auto arry = elemPtr->getPointerOperand();
        arrWrite[arry].push_back(store);
      } else {} // Do nothing
    } else if( auto load = llvm::dyn_cast<llvm::LoadInst>(I) ) {
      auto addr = load->getOperand(0);
      if( auto addr_bc = llvm::dyn_cast<llvm::BitCastInst>(addr) )
        addr = addr_bc->getOperand(0);
      if(auto elemPtr = llvm::dyn_cast<llvm::GetElementPtrInst>(addr) ) {
        auto arry = elemPtr->getPointerOperand();
        arrRead[arry].push_back(load);
      } else{} // Do nothing
    } else {} // Do nothing for inst other than store and load
  }
}


// Following function is taken from the llvm class lib/Transform/Scalar/LoopInterchange
// We require that the PHINode must be only of the integer type.
llvm::PHINode* getInductionVariable(llvm::Loop *L, llvm::ScalarEvolution *SE) {
  llvm::PHINode* InnerIndexVar = L->getCanonicalInductionVariable();
  if (InnerIndexVar)
    return InnerIndexVar;
  if (L->getLoopLatch() == nullptr || L->getLoopPredecessor() == nullptr)
    return nullptr;
  for (llvm::BasicBlock::iterator I = L->getHeader()->begin();
       llvm::isa<llvm::PHINode>(I); ++I) {
    llvm::PHINode *PhiVar = llvm::cast<llvm::PHINode>(I);
    llvm::Type *PhiTy = PhiVar->getType();
    if (!PhiTy->isIntegerTy())
      return nullptr;
    const llvm::SCEVAddRecExpr *AddRec =
      llvm::dyn_cast<llvm::SCEVAddRecExpr>(SE->getSCEV(PhiVar));
    if (!AddRec || !AddRec->isAffine())
      continue;
    const llvm::SCEV *Step = AddRec->getStepRecurrence(*SE);
    if (!llvm::isa<llvm::SCEVConstant>(Step))
      continue;
    // Found the induction variable.
    // FIXME: Handle loops with more than one induction variable. Note that,
    // currently, legality makes sure we have only one induction variable.
    return PhiVar;
  }
  return nullptr;
}

llvm::Value* find_ind_param_N(std::unique_ptr<llvm::Module>& module,
                              llvm::Function* f, llvm::BasicBlock* entry_bb) {
  llvm::Value* N = NULL;
  bool checkAssignment = false;
  if(f->arg_size() == 1 ) {
    // Use the only param to the function as the program parameter
    N = &(*f->arg_begin());
  } else if(!module->global_empty()) {
    // Use the only global variable as the program parameter
    int glbCntr = 0;
    for( auto iter_glb= module->global_begin(),end_glb = module->global_end();
         iter_glb != end_glb; ++iter_glb ) {
      glbCntr ++;
      N = &*iter_glb; //3.8
    }
    //    if(glbCntr != 1)
    //      tiler_error("llvmUtils", "Unable to find the program variable N for induction");
    checkAssignment = true;
  } else {
    for( auto it = entry_bb->begin(), e = entry_bb->end(); it != e; ++it) {
      llvm::Instruction *I = &(*it);
      if( auto alloc = llvm::dyn_cast<llvm::AllocaInst>(I) ) {
        if(alloc->isStaticAlloca()) continue;
        if( auto zext = llvm::dyn_cast<llvm::ZExtInst>(alloc->getArraySize()) ) {
          if(llvm::isa<llvm::BinaryOperator>(zext->getOperand(0)) ) break;
          N = zext->getOperand(0);
          checkAssignment=true;
          break;
        }
      }
    }
  }
  if(N == NULL) {
    tiler_error("llvmUtils", "Unable to find a program variable to induct on");
  }
  // If N is not a function parameter check that there is only one non-det assignment
  // to N in the first block of the program.
  // Otherwise, report error due to inability to find the full-program induction vairable.
  if(checkAssignment) {
    int storeToNCntr = 0;
    for( auto it = entry_bb->begin(), e = entry_bb->end(); it != e; ++it) {
      llvm::Instruction *I = &(*it);
      if( auto store = llvm::dyn_cast<llvm::StoreInst>(I) ) {
        if(N == store->getOperand(1)) {
          if( auto call = llvm::dyn_cast<llvm::CallInst>(store->getOperand(0)) ) {
            llvm::Function* fp = call->getCalledFunction();
            if( fp != NULL && fp->getName().startswith("__VERIFIER_nondet_") ) {
              storeToNCntr++;
            }
          }
        }
      }
    }
    if(storeToNCntr > 1)
      tiler_error("llvmUtils",
                  "Unable to find the program variable N for induction");
    int intermediateCnt = storeToNCntr;
    for( auto bbit = f->begin(), end = f->end(); bbit != end; bbit++ ) {
      llvm::BasicBlock* bb = &(*bbit);
      if(bb == entry_bb) continue;
      for( auto it = bb->begin(), e = bb->end(); it != e; ++it) {
        llvm::Instruction *I = &(*it);
        if( auto store = llvm::dyn_cast<llvm::StoreInst>(I) ) {
          if(N == store->getOperand(1)) storeToNCntr++;
        }
      }
    }
    if(intermediateCnt != storeToNCntr)
      tiler_error("llvmUtils",
                  "Unable to find the program variable N for induction");
  }
  return N;
}

bool check_conditional_in_loopbody(llvm::Loop *L) {
  for( auto b: L->getBlocks() ) {
    if(b == L->getLoopLatch()) continue;
    for( auto it = b->begin(), e = b->end(); it != e; ++it) {
      llvm::Instruction *I = &(*it);
      if (llvm::BranchInst* bi = llvm::dyn_cast<llvm::BranchInst>(I)) {
        if(bi->isUnconditional()) continue;
        if( llvm::isa<llvm::ICmpInst>(bi->getCondition()) ) {
          return true;
        } else {}
      } else {}
    }
  }
  return false;
}

// Pass p must declare it requires LoopInfoWrapperPass
void check_conditional_in_loopbody(llvm::Pass* p, llvm::Value* N, int& unsupported) {
 auto &LIWP = p->getAnalysis<llvm::LoopInfoWrapperPass>();
  auto LI = &LIWP.getLoopInfo();
  for (auto I = LI->rbegin(), E = LI->rend(); I != E; ++I) {
    llvm::Loop *L = *I;
    for( auto b: L->getBlocks() ) {
      if(b == L->getLoopLatch()) continue;
      for( auto it = b->begin(), e = b->end(); it != e; ++it) {
        llvm::Instruction *I = &(*it);
        if (llvm::BranchInst* bi = llvm::dyn_cast<llvm::BranchInst>(I)) {
          if(bi->isUnconditional()) continue;
          if( auto icmp = llvm::dyn_cast<llvm::ICmpInst>(bi->getCondition()) ) {
            if(isValueInSubExpr(N, icmp->getOperand( 0 )))
              unsupported = 2;
            if(isValueInSubExpr(N, icmp->getOperand( 1 )))
              unsupported = 2;
          } else {
            tiler_error("llvmUtils",
                        "Non integer comparision is currently not supported");
          }
        } else {}
      }
    }
  }
}

void check_indu_param_in_index(llvm::Function *f, llvm::Value* N, int& unsupported) {
  for( auto bbit = f->begin(), end = f->end(); bbit != end; bbit++ ) {
    llvm::BasicBlock* bb = &(*bbit);
    for( llvm::Instruction& Iobj : bb->getInstList() ) {
      llvm::Instruction* I = &(Iobj);
      if( auto load = llvm::dyn_cast<llvm::LoadInst>(I) ) {
        auto addr = load->getOperand(0);
        if(auto elemPtr = llvm::dyn_cast<llvm::GetElementPtrInst>(addr) ) {
          auto idx = elemPtr->getOperand(1);
          if(elemPtr->getNumIndices() == 2) idx = elemPtr->getOperand(2);
          if(isValueInSubExpr(N, idx))
            unsupported = 3;
        }
      }
    }
  }
}

bool checkDependence(llvm::Value* expr, llvm::Value* val) {
  if(val == expr) return true;
  if(auto bop = llvm::dyn_cast<llvm::BinaryOperator>(expr) ) {
    bool res = checkDependence(bop->getOperand(0), val);
    if(res) return true;
    return checkDependence(bop->getOperand(1), val);
  } else if(llvm::ICmpInst* icmp = llvm::dyn_cast<llvm::ICmpInst>(expr)) {
    bool res = checkDependence(icmp->getOperand(0), val);
    if(res) return true;
    return checkDependence(icmp->getOperand(1), val);
  } else if( llvm::isa<llvm::CallInst>(expr) ) {
    // In general, need to check the parameters of the call I think
    return false;
  } else if( auto gep = llvm::dyn_cast<llvm::GetElementPtrInst>(expr) ) {
    return checkDependence(gep->getOperand(1), val);
  } else if(auto load = llvm::dyn_cast<llvm::LoadInst>(expr) ) {
    return checkDependence(load->getOperand(0), val);
  } else if(auto store = llvm::dyn_cast<llvm::StoreInst>(expr) ) {
    bool res = checkDependence(store->getOperand(0), val);
    if(res) return true;
    return checkDependence(store->getOperand(1), val);
  } else if( auto cast = llvm::dyn_cast<llvm::CastInst>(expr) ) {
    checkDependence(cast->getOperand(0), val);
  } else if( llvm::isa<llvm::AllocaInst>(expr) ) {
    return false;
  } else {
    return false;
  }
}

llvm::Instruction* cloneExpr(llvm::Instruction* expr, llvm::Instruction* insert_pos,
                             llvm::Instruction* skip, llvm::Instruction* phi) {
  if(expr == skip) return NULL;
  if(auto bop = llvm::dyn_cast<llvm::BinaryOperator>(expr)) {
    auto* bopcl = bop->clone();
    bopcl->insertBefore(insert_pos);
    llvm::ValueToValueMapTy VMap;
    if(llvm::Instruction* op1 = llvm::dyn_cast<llvm::Instruction>(bopcl->getOperand(0))) {
      auto* clOp1 = cloneExpr(op1, bopcl, skip, phi);
      if(clOp1 != NULL) VMap[op1] = clOp1;
    }
    if(llvm::Instruction* op2 = llvm::dyn_cast<llvm::Instruction>(bopcl->getOperand(1))) {
      auto* clOp2 = cloneExpr(op2, bopcl, skip, phi);
      if(clOp2 != NULL) VMap[op2] = clOp2;
    }
    llvm::RemapInstruction(bopcl, VMap, llvm::RF_IgnoreMissingLocals |
                           llvm::RF_NoModuleLevelChanges);
    return bopcl;
  } else if(auto gep = llvm::dyn_cast<llvm::GetElementPtrInst>(expr)) {
    auto* g = gep ->clone();
    g->insertBefore(insert_pos);
    llvm::ValueToValueMapTy VMap;
    if(llvm::Instruction* idx1 = llvm::dyn_cast<llvm::Instruction>(gep->getOperand(1))) {
      llvm::Instruction* clIdx1 = cloneExpr(idx1, g, skip, phi);
      if(clIdx1 != NULL) VMap[idx1] = clIdx1;
    }
    if(gep->getNumIndices() == 2) {
      if(llvm::Instruction* idx2 = llvm::dyn_cast<llvm::Instruction>(gep->getOperand(2))) {
        llvm::Instruction* clIdx2 = cloneExpr(idx2, g, skip, phi);
        if(clIdx2 != NULL) VMap[idx2] = clIdx2;
      }
    }
    llvm::RemapInstruction(g, VMap, llvm::RF_IgnoreMissingLocals |
                           llvm::RF_NoModuleLevelChanges);
    return g;
  } else if(auto load = llvm::dyn_cast<llvm::LoadInst>(expr) ) {
    auto* l = load->clone();
    l->insertBefore(insert_pos);
    llvm::ValueToValueMapTy VMap;
    if(llvm::Instruction* op = llvm::dyn_cast<llvm::Instruction>(l->getOperand(0))) {
      llvm::Instruction* v = cloneExpr(op, l, skip, phi);
      if(v != NULL) VMap[op] = v;
    }
    llvm::RemapInstruction(l, VMap, llvm::RF_IgnoreMissingLocals |
                           llvm::RF_NoModuleLevelChanges);
    return l;
  } else if(auto cast = llvm::dyn_cast<llvm::CastInst>(expr)) {
    auto* c = cast->clone();
    c->insertBefore(insert_pos);
    llvm::ValueToValueMapTy VMap;
    if(llvm::Instruction* op = llvm::cast<llvm::Instruction>(c->getOperand(0))) {
      auto* v = cloneExpr(op, c, skip, phi);
      if(v != NULL) VMap[op] = v;
    }
    llvm::RemapInstruction(c, VMap, llvm::RF_IgnoreMissingLocals |
                           llvm::RF_NoModuleLevelChanges);
    return c;
  } else if(llvm::isa<llvm::PHINode>(expr)) {
    return phi;
    // tiler_error("Cloning", "Cloning a PHI node is a very very tricky thing!");
  } else if(llvm::isa<llvm::CallInst>(expr)) {
    // Parameters to be cloned ?
    tiler_error("Cloning", "Call Instruciton currently not supported as a sub-expression");
  } else if(llvm::isa<llvm::StoreInst>(expr)) {
    tiler_error("Cloning", "Store cannot be a part of the sub-expression");
  } else if(llvm::isa<llvm::AllocaInst>(expr)) {
    return NULL;     // Dont clone or remap
  } else if(llvm::isa<const llvm::GlobalVariable>(expr)) {
    return NULL;    // Dont clone or remap
  } else {
    tiler_error("Cloning", "Case undefined for this expression");
  }
  return NULL;
}

void eraseExpr(llvm::Instruction* expr, llvm::Instruction* skip) {
  if(expr == NULL) return;
  if(expr == skip) return;
  if(auto bop = llvm::dyn_cast<llvm::BinaryOperator>(expr)) {
    auto op0 = bop->getOperand(0);
    auto op1 = bop->getOperand(1);
    bop->eraseFromParent();
    if(auto op0Inst = llvm::dyn_cast<llvm::Instruction>(op0))
      eraseExpr(op0Inst, skip);
    if(auto op1Inst = llvm::dyn_cast<llvm::Instruction>(op1))
      eraseExpr(op1Inst, skip);
  } else if(auto gep = llvm::dyn_cast<llvm::GetElementPtrInst>(expr)) {
    auto idx = gep->getOperand(1);
    if(gep->getNumIndices() == 2)
      idx = gep->getOperand(2);
    gep->eraseFromParent();
    if(auto idxInst = llvm::dyn_cast<llvm::Instruction>(idx))
      eraseExpr(idxInst, skip);
  } else if(auto load = llvm::dyn_cast<llvm::LoadInst>(expr) ) {
    auto op0 = load->getOperand(0);
    load->eraseFromParent();
    if(auto op0Inst = llvm::dyn_cast<llvm::Instruction>(op0))
       eraseExpr(op0Inst, skip);
  } else if(auto cast = llvm::dyn_cast<llvm::CastInst>(expr)) {
    auto op0 = cast->getOperand(0);
    cast->eraseFromParent();
    if(auto op0Inst = llvm::dyn_cast<llvm::Instruction>(op0))
      eraseExpr(op0Inst, skip);
  } else if(llvm::isa<llvm::PHINode>(expr)) {
    // Don't erase
    //tiler_error("Erasing", "Erasing a PHI node is a very bad idea!");
  } else if(llvm::isa<llvm::CallInst>(expr)) {
    tiler_error("Erasing", "Erasing call Instrucitons currently not supported");
  } else if(auto store = llvm::dyn_cast<llvm::StoreInst>(expr)) {
    auto op0 = store->getOperand(0);
    auto op1 = store->getOperand(1);
    store->eraseFromParent();
    if(auto op1Inst = llvm::dyn_cast<llvm::Instruction>(op1))
      eraseExpr(op1Inst, skip);
    if(auto op0Inst = llvm::dyn_cast<llvm::Instruction>(op0))
       eraseExpr(op0Inst, skip);
  } else if(llvm::isa<llvm::AllocaInst>(expr)) {
    // Don't erase
  } else if(llvm::isa<const llvm::GlobalVariable>(expr)) {
    // Don't erase
  } else {
    tiler_error("Erasing", "Erasing undefined for the given expression");
  }
}

/// Following function is inspired from lib/Transforms/Utils/LoopUnrollPeel.cpp
///
/// \brief Clones the body of the loop L, putting it between \p InsertTop and \p
/// InsertBot. FinalBot is the block to totally exit from all iterations.
/// \param IterNumber The serial number of the iteration currently being
/// peeled off.
/// \param[out] NewBlocks A list of the the blocks in the newly created clone.
/// \param[out] VMap The value map between the loop and the new clone.
/// \param LoopBlocks A helper for DFS-traversal of the loop.
/// \param LVMap A value-map that maps instructions from the original loop to
/// instructions in the last peeled-off iteration.
void
cloneLoopBlocksAtEnd(llvm::Loop *L, unsigned IterNumber, llvm::BasicBlock *InsertTop,
                     llvm::BasicBlock *InsertBot, llvm::BasicBlock *FinalBot,
                     llvm::SmallVectorImpl<llvm::BasicBlock *> &NewBlocks,
                     llvm::LoopBlocksDFS &LoopBlocks, llvm::ValueToValueMapTy &VMap,
                     llvm::ValueToValueMapTy &LVMap, llvm::DominatorTree *DT,
                     llvm::LoopInfo *LI) {
  llvm::BasicBlock *Header = L->getHeader();
  llvm::BasicBlock *Latch = L->getLoopLatch();

  llvm::Function *F = Header->getParent();
  llvm::LoopBlocksDFS::RPOIterator BlockBegin = LoopBlocks.beginRPO();
  llvm::LoopBlocksDFS::RPOIterator BlockEnd = LoopBlocks.endRPO();
  llvm::Loop *ParentLoop = L->getParentLoop();

  /*
  llvm::BasicBlock *PreHeader = L->getLoopPreheader();
  llvm::errs() << "\nPreHeader Block\n\n";
  PreHeader->print( llvm::errs() );
  llvm::errs() << "\nTop Anchor\n\n";
  InsertTop->print( llvm::errs() );
  */

  // For each block in the original loop, create a new copy,
  // and update the value map with the newly created values.
  for (llvm::LoopBlocksDFS::RPOIterator BB = BlockBegin; BB != BlockEnd; ++BB) {
    llvm::BasicBlock *NewBB = CloneBasicBlock(*BB, VMap, ".peel", F);
    NewBlocks.push_back(NewBB);

    if (ParentLoop)
      ParentLoop->addBasicBlockToLoop(NewBB, *LI);

    VMap[*BB] = NewBB;

    // If dominator tree is available, insert nodes to represent cloned blocks.
    if (DT) {
      if (Header == *BB)
        DT->addNewBlock(NewBB, InsertTop);
      else {
        llvm::DomTreeNode *IDom = DT->getNode(*BB)->getIDom();
        // VMap must contain entry for IDom, as the iteration order is RPO.
        DT->addNewBlock(NewBB, llvm::cast<llvm::BasicBlock>(VMap[IDom->getBlock()]));
      }
    }

    /*
    llvm::errs() << "\n\n";
    llvm::cast<llvm::BasicBlock>(*BB)->print( llvm::errs() );
    llvm::errs() << "\n\n";
    NewBB->print( llvm::errs() );
    llvm::errs() << "\n\n";
    */
  }

  /*
  llvm::errs() << "\nBottom Anchor\n";
  InsertBot->print( llvm::errs() );
  llvm::errs() << "\nFinal Exit\n";
  FinalBot->print( llvm::errs() );
  */

  // Hook-up the control flow for the newly inserted blocks.
  // The new header is hooked up directly to the "newexit", which is either
  // the original loop exit (for the first iteration) or the previous
  // iteration's exit block (for every other iteration)
  InsertTop->getTerminator()->setSuccessor(0, llvm::cast<llvm::BasicBlock>(VMap[Header]));

  // Similarly, for the latch:
  // The original exiting edge is still hooked up to the loop exit.
  // The backedge now goes to the "newexittop", which is either the copied exit
  // of the loop (for the last peeled iteration) or the copied exit of the next
  // iteration (for every other iteration)
  llvm::BasicBlock *NewLatch = llvm::cast<llvm::BasicBlock>(VMap[Latch]);
  llvm::BranchInst *NewLatchBR = llvm::cast<llvm::BranchInst>(NewLatch->getTerminator());

  // llvm::errs() << "\nBranch Inst in Latch before setting successor\n";
  // NewLatchBR->print( llvm::errs() );

  unsigned HeaderIdx = (NewLatchBR->getSuccessor(0) == Header ? 0 : 1);
  NewLatchBR->setSuccessor(HeaderIdx, InsertBot);
  NewLatchBR->setSuccessor(1 - HeaderIdx, FinalBot);
  if (DT)
    DT->changeImmediateDominator(InsertBot, NewLatch);

  // llvm::errs() << "\nBranch Inst in Latch after setting successor\n";
  // NewLatchBR->print( llvm::errs() );

  // The new copy of the loop body starts with a bunch of PHI nodes
  // that pick an incoming value from the previous loop iteration.
  // Since this copy is no longer part of the loop, we resolve this statically:
  // For the first iteration, we use the value from the latch of the loop.
  // For any other iteration, we replace the phi with the value generated by
  // the immediately preceding clone of the loop body (which represents
  // the previous iteration).

  llvm::BasicBlock::iterator I = Header->begin();
  for ( ; llvm::isa<llvm::PHINode>(I); ++I) {
    llvm::PHINode *NewPHI = llvm::cast<llvm::PHINode>(VMap[&*I]);
    llvm::Value *LatchVal = NewPHI->getIncomingValueForBlock(Latch);
    llvm::Instruction *LatchInst = llvm::dyn_cast<llvm::Instruction>(LatchVal);
    if (IterNumber == 0) {
      VMap[&*I] = LatchVal;     // To be mapped to NS later
    } else {
      VMap[&*I] = LVMap[LatchInst];     // To be mapped to NS later
    }

    llvm::cast<llvm::BasicBlock>(VMap[Header])->getInstList().erase(NewPHI);
  }

  // Fix up the outgoing values - we need to add a value for the iteration
  // we've just created. Note that this must happen *after* the incoming
  // values are adjusted, since the value going out of the latch may also be
  // a value coming into the header.
  for (llvm::BasicBlock::iterator I = FinalBot->begin(); llvm::isa<llvm::PHINode>(I); ++I) {
    llvm::PHINode *PHI = llvm::cast<llvm::PHINode>(I);
    llvm::Value *LatchVal = PHI->getIncomingValueForBlock(Latch);
    llvm::Instruction *LatchInst = llvm::dyn_cast<llvm::Instruction>(LatchVal);
    if (LatchInst && L->contains(LatchInst))
      LatchVal = VMap[LatchVal];
    PHI->addIncoming(LatchVal, llvm::cast<llvm::BasicBlock>(VMap[Latch]));
  }

  // LastValueMap is updated with the values for the current loop
  // which are used the next time this function is called.
  for (const auto &KV : VMap)
    LVMap[KV.first] = KV.second;

}

/// Following function is inspired from lib/Transforms/Utils/LoopUnrollPeel.cpp
///
/// \brief Peel off the last \p PeelCount iterations of loop \p L.
///
/// Note that this does not peel them off as a single straight-line block.
/// Rather, each iteration is peeled off separately, and needs to check the
/// exit condition.
bool
myPeelLoop(llvm::Loop *L, unsigned PeelCount, llvm::LoopInfo *LI,
           llvm::ScalarEvolution *SE, llvm::DominatorTree *DT,
           llvm::AssumptionCache *AC, bool PreserveLCSSA, llvm::Value *N,
           llvm::Value *NS, llvm::Value *exitVal, int stepCnt,
           llvm::ValueToValueMapTy &LVMap,
           std::vector<llvm::BasicBlock*>& peeledBlocks) {
  // Only peeling single iteration is currently supported specifically for Advanced FPI
  if(PeelCount != 1)
    return false;

  // Make sure the loop is in simplified form
  if (!L->isLoopSimplifyForm())
    return false;

  // Only peel loops that contain a single exit
  if (!L->getExitingBlock() || !L->getUniqueExitBlock())
    return false;

  // Don't try to peel loops where the latch is not the exiting block.
  // This can be an indication of two different things:
  // 1) The loop is not rotated.
  // 2) The loop contains irreducible control flow that involves the latch.
  if (L->getLoopLatch() != L->getExitingBlock())
    return false;

  llvm::LoopBlocksDFS LoopBlocks(L);
  LoopBlocks.perform(LI);

  llvm::BasicBlock *Header = L->getHeader();
  llvm::BasicBlock *Latch = L->getLoopLatch();
  llvm::BasicBlock *Exit = L->getUniqueExitBlock();
  llvm::BranchInst *ExitBR = llvm::cast<llvm::BranchInst>(Exit->getTerminator());
  llvm::BasicBlock *ExitSucc = ExitBR->getSuccessor(0);

  llvm::Function *F = Header->getParent();

  /*
  llvm::BasicBlock *PreHeader = L->getLoopPreheader();
  llvm::errs() << "\n\nPreHeader Block\n\n";
  PreHeader->print( llvm::errs() );
  llvm::errs() << "\n\nHeader Block\n\n";
  Header->print( llvm::errs() );
  llvm::errs() << "\n\nLatch Block\n\n";
  Latch->print( llvm::errs() );
  llvm::errs() << "\n\nExit Block\n\n";
  Exit->print( llvm::errs() );
  llvm::errs() << "\n\nExit Succcessor Block\n\n";
  ExitSucc->print( llvm::errs() );
  */

  // Set up all the necessary basic blocks. It is convenient to split the
  // Exit into 3 parts - two blocks to anchor the peeled copy of the loop
  // body, and the final exit to skip subsequent iterations.

  // Peeling the last iteration transforms.
  //
  // PreHeader:
  // ...
  // Header:
  //   LoopBody
  //   If (cond) goto Header
  // Exit:
  //
  // into
  //
  // PreHeader:
  // ...
  // Header:
  //  LoopBody
  //  If (cond') goto Header
  // Exit:
  // InsertTop:
  //   LoopBody
  //   If (!cond) goto FinalBot
  // InsertBot:
  // FinalBot:
  //
  // Each following iteration will split the current bottom anchor "bottom"
  // in two, and put the new copy of the loop body between these two blocks.
  // That is, after peeling another iteration from the example above, we'll split
  // InsertBot, and get:
  //
  // PreHeader:
  // ...
  // Header:
  //  LoopBody
  //  If (cond') goto Header
  // Exit:
  // InsertTop:
  //   LoopBody
  //   If (!cond) goto FinalBot
  // InsertBot:
  //   LoopBody
  //   If (!cond) goto FinalBot
  // InsertBot.next:
  // FinalBot:

  llvm::BasicBlock *InsertTop = llvm::SplitEdge(Exit, ExitSucc, DT, LI);
  llvm::BasicBlock *InsertBot =
      llvm::SplitBlock(InsertTop, InsertTop->getTerminator(), DT, LI);
  llvm::BasicBlock *FinalBot =
      llvm::SplitBlock(InsertBot, InsertBot->getTerminator(), DT, LI);

  InsertTop->setName(Exit->getName() + ".peel.begin");
  InsertBot->setName(Exit->getName() + ".peel.next");
  FinalBot->setName(Exit->getName() + ".peel.final");

  // Collect all the newly created blocks for future use
  llvm::SmallVector<llvm::BasicBlock *, 20> AllNewBlocks;
  AllNewBlocks.push_back(InsertTop);
  AllNewBlocks.push_back(InsertBot);
  AllNewBlocks.push_back(FinalBot);

  // For each peeled-off iteration, make a copy of the loop.
  for (unsigned Iter = 0; Iter < PeelCount; ++Iter) {
    llvm::SmallVector<llvm::BasicBlock *, 8> NewBlocks;
    llvm::ValueToValueMapTy VMap;

    // Clone the blocks in the loop and attach them after the loop
    cloneLoopBlocksAtEnd(L, Iter, InsertTop, InsertBot, FinalBot,
                         NewBlocks, LoopBlocks, VMap, LVMap, DT, LI);

    if(NewBlocks.empty())
      tiler_error("Cloning", "List of cloned blocks is empty");

    // Remap to use values from the current iteration instead of the loop
    // iteration. VMap populated in above function call of cloneLoopBlocksAtEnd.
    llvm::remapInstructionsInBlocks(NewBlocks, VMap);

    // Collect all the newly created blocks for future use
    for(llvm::BasicBlock* b : NewBlocks)
      AllNewBlocks.push_back(b);

    InsertTop = InsertBot;
    if(PeelCount > 1) { // Subsequent splits only if more iterations are to be peeled
      InsertBot = llvm::SplitBlock(InsertBot, InsertBot->getTerminator(), DT, LI);
      InsertBot->setName(InsertTop->getName() + ".next");
      AllNewBlocks.push_back(InsertBot);
    } else {
      /*
      llvm::BasicBlock *IBPred = InsertBot->getUniquePredecessor();
      if(IBPred != NULL) {
        llvm::Instruction* term = IBPred->getTerminator();
        if (llvm::BranchInst* bi = llvm::dyn_cast<llvm::BranchInst>(term)) {
          if(bi->isConditional()) {
            // Remove the conditional branch and add a unconditional branch
            llvm::IRBuilder<> b(term);
            b.CreateBr(FinalBot);
            llvm::Value *loopCond = bi->getCondition();
            bi->eraseFromParent();
            if( auto icmp = llvm::dyn_cast<llvm::ICmpInst>(loopCond) )
              icmp->eraseFromParent();
            // Detach the InsertBot Block
            InsertBot->getTerminator()->setSuccessor(0, NULL);
          } else {}
        } else {}
      } else {}
      auto position = std::find(AllNewBlocks.begin(), AllNewBlocks.end(), InsertBot);
      if (position != AllNewBlocks.end()) // == myVector.end() means the element was not found
        AllNewBlocks.erase(position);
      */
    }

    // Attach the new cloned blocks in the basicblock list of the function
    F->getBasicBlockList().splice(InsertTop->getIterator(),
                                  F->getBasicBlockList(),
                                  NewBlocks[0]->getIterator(), F->end());

    // Replace the loop counter in the peeled blocks with the approprite value
    llvm::PHINode *PHI = getInductionVariable(L, SE);
    llvm::Value *LatchVal = PHI->getIncomingValueForBlock(Latch);
    llvm::Instruction *LatchInst = llvm::dyn_cast<llvm::Instruction>(LatchVal);
    if (Iter > 0) {
      LatchVal = LVMap[LatchInst];     // To be mapped to NS later
    }
    llvm::ValueToValueMapTy VVMap;
    llvm::BranchInst *LatchBR = llvm::cast<llvm::BranchInst>(Latch->getTerminator());
    if(!LatchBR) tiler_error("llvmUtils", "Latch block does not have unique term inst");
    llvm::Value *loopCond = LatchBR->getCondition();
    if( llvm::ICmpInst* icmp = llvm::dyn_cast<llvm::ICmpInst>(loopCond) ) {
      llvm::ValueToValueMapTy ExitVMap;
      if( PeelCount == 1 && llvm::isa<llvm::LoadInst>(exitVal) ) {
        llvm::LoadInst* load = llvm::dyn_cast<llvm::LoadInst>(exitVal);
        if(N == load->getOperand(0))
          VVMap[LatchVal] = NS;
      } else if (N == exitVal && PeelCount == 1) {
        VVMap[LatchVal] = NS;
      } else {
        llvm::ValueToValueMapTy::iterator I;
        I = LVMap.find(icmp);
        if (I == LVMap.end()) tiler_error("Cloning","Value not in map");
        llvm::ICmpInst* clIcmp = llvm::dyn_cast<llvm::ICmpInst>(I->second);
        llvm::Instruction* clExitVal = llvm::dyn_cast<llvm::Instruction>(exitVal);
        if (L->contains(clExitVal->getParent())) {
            I = LVMap.find(exitVal);
            if (I == LVMap.end()) tiler_error("Cloning","Value not in map");
            if((clExitVal = llvm::dyn_cast<llvm::Instruction>(I->second))) {
            } else { assert(clExitVal && "Clone of exit value is not a instruction"); }
        }
        llvm::Instruction* clExitValp = NULL;
        llvm::Type *constValTy = clExitVal->getType();
        if(constValTy->isPointerTy())
          constValTy = constValTy->getPointerElementType();
        llvm::Value* constVal = llvm::ConstantInt::get(constValTy, PeelCount-Iter);
        if(stepCnt > 0)
          clExitValp = llvm::BinaryOperator::Create(llvm::Instruction::Sub,
                                                    clExitVal, constVal, "NewBound.peel");
        else
          clExitValp = llvm::BinaryOperator::Create(llvm::Instruction::Add,
                                                    clExitVal, constVal, "NewBound.peel");
        clExitValp->insertBefore(clIcmp);
        VVMap[LatchVal] = clExitValp;
      }
    } else {}
    llvm::remapInstructionsInBlocks(AllNewBlocks, VVMap);
  }

  // Change the loop terminating condition in accordance with the peel count
  llvm::BranchInst *LatchBR = llvm::cast<llvm::BranchInst>(Latch->getTerminator());
  if(!LatchBR) tiler_error("llvmUtils", "Latch block does not have unique term inst");
  llvm::Value *loopCond = LatchBR->getCondition();
  if( llvm::ICmpInst* icmp = llvm::dyn_cast<llvm::ICmpInst>(loopCond) ) {
    llvm::ValueToValueMapTy ExitVMap;
    if( llvm::LoadInst* load = llvm::dyn_cast<llvm::LoadInst>(exitVal) ) {
      if(N == load->getOperand(0))
        ExitVMap[load] = NS;
    } else if (N == exitVal) {
      ExitVMap[N] = NS;
    } else {
      llvm::Type *constValTy = exitVal->getType();
      if(constValTy->isPointerTy())
        constValTy = constValTy->getPointerElementType();
      llvm::Value* constVal = llvm::ConstantInt::get(constValTy, PeelCount);
      llvm::Instruction* exitValp = NULL;
      if(stepCnt > 0)
        exitValp = llvm::BinaryOperator::Create(llvm::Instruction::Sub,
                                                exitVal, constVal, "NewBound");
      else
        exitValp = llvm::BinaryOperator::Create(llvm::Instruction::Add,
                                                exitVal, constVal, "NewBound");
      exitValp->insertBefore(icmp);
      ExitVMap[exitVal] = exitValp;
    }
    llvm::RemapInstruction(icmp, ExitVMap, llvm::RF_IgnoreMissingLocals
                           | llvm::RF_NoModuleLevelChanges);
  } else {}

  /*
  // Replace the loop counter in the peeled blocks with the approprite value
  // Multiple nodes will have to be mapped if PeelCount > 1 and not all such
  // nodes use the latch value but the value coming from the previous peel
  llvm::PHINode *PHI = getInductionVariable(L, SE);
  llvm::Value *LatchVal = PHI->getIncomingValueForBlock(Latch);
  llvm::ValueToValueMapTy VVMap;
  VVMap[LatchVal] = NS;     // LatchVal will not work if PeelCount > 1
  llvm::remapInstructionsInBlocks(AllNewBlocks, VVMap);
  */

  // If the loop is nested, we changed the parent loop, update SE.
  if (llvm::Loop *ParentLoop = L->getParentLoop()) {
    SE->forgetLoop(ParentLoop);
    // FIXME: Incrementally update loop-simplify
    llvm::simplifyLoop(ParentLoop, DT, LI, SE, AC, PreserveLCSSA);
  } else {
    // FIXME: Incrementally update loop-simplify
    llvm::simplifyLoop(L, DT, LI, SE, AC, PreserveLCSSA);
  }

  // Collect all the newly created blocks for future use
  for(llvm::BasicBlock* b : AllNewBlocks)
    peeledBlocks.push_back(b);

  if(peeledBlocks.empty())
    tiler_error("Cloning", "List of cloned blocks is empty");

  return true;
}

// Following function is adapted from the llvm code since
// llvm v 6.0 does not have this function in the LoopInfo.cpp
llvm::BranchInst *getLoopGuardBR(llvm::Loop *L, bool isPeeled) {
  if (!L->isLoopSimplifyForm()) return NULL;

  llvm::BasicBlock *Preheader = L->getLoopPreheader();
  assert(Preheader && L->getLoopLatch() &&
         "Expecting a loop with valid preheader and latch");

  // Loop should be in rotate form.
  // if (!L->isRotatedForm()) return NULL;
  // Above line commented since the call is not available in llvm v 6.0

  // Disallow loops with more than one unique exit block, as we do not verify
  // that GuardOtherSucc post dominates all exit blocks.
  llvm::BasicBlock *ExitFromLatch = L->getUniqueExitBlock();
  if (!ExitFromLatch) return NULL;

  llvm::BasicBlock *ExitFromLatchSucc = ExitFromLatch->getUniqueSuccessor();
  if (!ExitFromLatchSucc) return NULL;

  llvm::BasicBlock *GuardBB = Preheader->getUniquePredecessor();
  if (!GuardBB) return NULL;

  assert(GuardBB->getTerminator() && "Expecting valid guard terminator");

  llvm::BranchInst *GuardBI = llvm::dyn_cast<llvm::BranchInst>(GuardBB->getTerminator());
  if (!GuardBI || GuardBI->isUnconditional()) return NULL;

  llvm::BasicBlock *GuardOtherSucc =
    (GuardBI->getSuccessor(0) == Preheader) ? GuardBI->getSuccessor(1)
    : GuardBI->getSuccessor(0);

  // The following condition will not hold when loops are peeled
  if(isPeeled)
    return GuardBI;
  else
    return (GuardOtherSucc == ExitFromLatchSucc) ? GuardBI : NULL;

}

llvm::Value* getSingularNRef(llvm::Value* N, llvm::BasicBlock* entry_bb) {
  if(N == NULL || entry_bb == NULL) return NULL;
  llvm::Value* SingularNRef = NULL;
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
  if(SingularNRef == NULL) tiler_error("llvmUtils","Program Parameter is NULL");
  return SingularNRef;
}

llvm::Instruction* generateNSVal(llvm::Value* N, llvm::Value* SingularNRef,
                                 llvm::BasicBlock* entry_bb) {
  if(N==NULL) return NULL;
  if(llvm::isa<llvm::GlobalVariable>(N)) {
    llvm::Type *oneTy = N->getType();
    oneTy = oneTy->getPointerElementType();
    llvm::Value *one = llvm::ConstantInt::get(oneTy, 1);
    if(SingularNRef == NULL) return NULL;
    llvm::LoadInst *Nload = llvm::dyn_cast<llvm::LoadInst>(SingularNRef);
    llvm::Instruction *NS = llvm::BinaryOperator::Create(llvm::Instruction::Sub, Nload, one, "NS");
    NS->insertAfter(Nload);
    return NS;
  } else {
    if(entry_bb == NULL) return NULL;
    llvm::Type *oneTy = N->getType();
    llvm::Value *one = llvm::ConstantInt::get(oneTy, 1);
    llvm::Instruction *NS = llvm::BinaryOperator::Create(llvm::Instruction::Sub, N, one, "NS");
    NS->insertBefore(entry_bb->getFirstNonPHI());
    return NS;
  }
  return NULL;
}

//----------------------------------------------------------------
// Cloning APIs

llvm::BasicBlock*
create_cloned_remapped_bb( const llvm::BasicBlock* basicBlock,
                           const llvm::Twine& Name = "_aggr",
                           llvm::Function* F = 0) {
  llvm::ValueToValueMapTy VMap;
  bb * clonedBB = llvm::CloneBasicBlock(basicBlock, VMap, Name, F);
  llvm::SmallVector<llvm::BasicBlock*,2> bbVec;
  bbVec.push_back(clonedBB);
  llvm::remapInstructionsInBlocks(bbVec, VMap);  //  remaps instructions
  return clonedBB;
}

// Pass p must declare it requires LoopInfoWrapperPass and
// DominatorTreeWrapperPass
std::vector<llvm::BasicBlock*>
collect_cloned_loop_blocks(llvm::Pass *p, llvm::Loop *L, loopdata *ld) {
  if (!L->getExitingBlock()    ||
      !L->getUniqueExitBlock() ||
      L->getLoopLatch() != L->getExitingBlock())
    tiler_error("llvmUtils", "Loop does not have a unique exiting latch");
  bb* preheader = L->getLoopPreheader();
  bb* header = L->getHeader();
  bb* latch = L->getLoopLatch();
  bb* block_after_exit = NULL;
  llvm::Instruction* term = latch->getTerminator();
  if(!term) tiler_error("llvmUtils", "Loop does not have unique term inst");
  if (llvm::BranchInst *bi = llvm::dyn_cast<llvm::BranchInst>(term)) {
    const int NS = bi->getNumSuccessors();
    for(int i=0; i<NS; i++) {
      bb* s = bi->getSuccessor(i);
      if(s != header)
        block_after_exit = s;
    }
  } else
    tiler_error("llvmUtils",  "Cast of term inst to branch inst failed");
  if(!block_after_exit) tiler_error("llvmUtils", "Block after the exiting loop not found");
  llvm::ValueToValueMapTy VMap;
  auto &LIWP = p->getAnalysis<llvm::LoopInfoWrapperPass>();
  auto LI = &LIWP.getLoopInfo();
  auto &DTWP = p->getAnalysis<llvm::DominatorTreeWrapperPass>();
  llvm::DominatorTree* DT = &DTWP.getDomTree();
  llvm::SmallVector<llvm::BasicBlock *, 20> cloned_blocks;
  llvm::Loop* cl =
    cloneLoopWithPreheader(block_after_exit, preheader, L, VMap,
                           "_clone", LI, DT, cloned_blocks);
  if(!cl) tiler_error("llvmUtils", "Unable to clone the preheader of the loop");
  return to_std_vec(cloned_blocks);
}

llvm::AllocaInst* create_new_alloca(llvm::AllocaInst* arry_alloca) {
  llvm::IRBuilder<> alloca_builder(arry_alloca);
  llvm::AllocaInst* new_arry_alloca =
    alloca_builder.CreateAlloca(arry_alloca->getAllocatedType(),
                                arry_alloca ->getArraySize());
  return new_arry_alloca;
}

void remap_store(llvm::StoreInst* store,
                 llvm::AllocaInst* arry_alloca,
                 llvm::AllocaInst* new_arry_alloca) {
  auto addr = store->getOperand(1);
  llvm::BitCastInst* addr_bc = NULL;
  if( (addr_bc = llvm::dyn_cast<llvm::BitCastInst>(addr)) )
    addr = addr_bc->getOperand(0);
  if( auto elemPtr = llvm::dyn_cast<llvm::GetElementPtrInst>(addr) ) {
    elemPtr->setOperand(0, new_arry_alloca);
    // llvm::ValueToValueMapTy VMap;
    // VMap[arry_alloca] = new_arry_alloca;
    /*
    auto idx = elemPtr->getOperand(1);
    if(elemPtr->getNumIndices() == 2) idx = elemPtr->getOperand(2);
    llvm::IRBuilder<> builder(elemPtr);
    llvm::GetElementPtrInst* new_elem_ptr =
      llvm::dyn_cast<llvm::GetElementPtrInst>(builder.CreateGEP(new_arry_alloca, idx));
    elemPtr->removeFromParent();
    */
  }
}

void remap_load(llvm::LoadInst* load,
                llvm::AllocaInst* arry_alloca,
                llvm::AllocaInst* new_arry_alloca) {
  auto addr = load->getOperand(0);
  llvm::BitCastInst* addr_bc = NULL;
  if( (addr_bc = llvm::dyn_cast<llvm::BitCastInst>(addr)) )
    addr = addr_bc->getOperand(0);
  if( auto elemPtr = llvm::dyn_cast<llvm::GetElementPtrInst>(addr) ) {
    elemPtr->setOperand(0, new_arry_alloca);
    /*
    llvm::ValueToValueMapTy VMap;
    VMap[arry_alloca] = new_arry_alloca;
    RemapInstruction(elemPtr, VMap);
    auto idx = elemPtr->getOperand(1);
    if(elemPtr->getNumIndices() == 2) idx = elemPtr->getOperand(2);
    llvm::IRBuilder<> builder(elemPtr);
    builder.CreateGEP(new_arry_alloca, idx);
    elemPtr->removeFromParent();
    */
  }
}

void
add_equality_stmt(llvm::BasicBlock* bb,
                  llvm::AllocaInst* prev_arry_alloca,
                  llvm::AllocaInst* new_arry_alloca,
                  llvm::LLVMContext& globalContext) {
  llvm::Instruction* term = bb->getTerminator();
  if(!term) tiler_error("llvmUtils", "Preheader block does not have unique term inst");
  llvm::IRBuilder<> b(term);
  llvm::Type *i32_ptr_type = llvm::IntegerType::getInt32PtrTy(globalContext);
  llvm::Value* constZero = llvm::ConstantInt::get(globalContext, llvm::APInt(64, 0, true));
  llvm::GetElementPtrInst* prev_elem_ptr =
    llvm::dyn_cast<llvm::GetElementPtrInst>(b.CreateInBoundsGEP(prev_arry_alloca, constZero));
  auto prev_elem_ptr_bc = b.CreateBitCast(prev_elem_ptr, i32_ptr_type);
  llvm::LoadInst* load =
    llvm::dyn_cast<llvm::LoadInst>(b.CreateLoad(prev_elem_ptr_bc));
  llvm::GetElementPtrInst* new_elem_ptr =
    llvm::dyn_cast<llvm::GetElementPtrInst>(b.CreateInBoundsGEP(new_arry_alloca, constZero));
  auto new_elem_ptr_bc = b.CreateBitCast(new_elem_ptr, i32_ptr_type);
  b.CreateStore(load, new_elem_ptr_bc);
}

//----------------------------------------------------------------------
// value_expr_map

void value_expr_map::insert_term_map( const llvm::Value* op, z3::expr e ) {
  auto it = versions.find(op);
  if( it == versions.end() ) {
    insert_term_map( op, 0, e );
  }else{
    insert_term_map( op, (it->second).back() + 1, e );
  }
}

void value_expr_map::insert_term_map( const llvm::Value* op, unsigned c_count,
                                      z3::expr e ) {
  auto it = versions.find(op);
  if( it == versions.end() ) {
    // assert( c_count == 0 );
  }else{
    assert( (it->second).back() < c_count);
  }
  versions[op].push_back( c_count );
  auto pair = std::make_pair( std::make_pair( op, c_count ), e );
  vmap.insert( pair );
}

//insert_new_def with 2 param is alias of get_term
z3::expr value_expr_map::insert_new_def( const llvm::Value* op,
                                         unsigned c_count ) {
  return get_term( op, c_count );
}

z3::expr value_expr_map::insert_new_def( const llvm::Value* op ) {
  unsigned count = versions.find(op) == versions.end() ? 0 : versions[op].back() + 1;
  return insert_new_def( op, count );
}

z3::expr value_expr_map::get_term( const llvm::Value* op, std::string op_name ) {
  z3::expr c = read_constant( op );
  if( c ) return c;
  auto it = versions.find(op);
  if( it == versions.end() ) {
    // tiler_error("bmc", "call insert_new_def instead of get_term !!");
    return get_term( op, 0, op_name );
  }else{
    return get_term(op, (it->second).back(), op_name );
  }
}

z3::expr value_expr_map::get_term( const llvm::Value* op ) {
  z3::expr c = read_constant( op );
  if( c ) return c;
  auto it = versions.find(op);
  if( it == versions.end() ) {
    // tiler_error("bmc", "call insert_new_def instead of get_term !!");
    return get_term( op, 0 );
  }else{
    return get_term(op, (it->second).back() );
  }
}

z3::expr value_expr_map::get_term( const llvm::Value* op, unsigned c_count, std::string op_name ) {
  z3::expr e = read_term( op, c_count );
  if( e ) return e;
  // create new name
  z3::expr name = create_fresh_name( op, op_name );
  insert_term_map( op, c_count, name );
  return name;
}

z3::expr value_expr_map::get_term( const llvm::Value* op, unsigned c_count ) {
  z3::expr e = read_term( op, c_count );
  if( e ) return e;
  // create new name
  z3::expr name = create_fresh_name( op );
  insert_term_map( op, c_count, name );
  return name;
}

z3::expr
value_expr_map::get_earlier_term( const llvm::Value* op, unsigned c_count ) {
  z3::expr c = read_constant( op );
  if( c ) return c;
  auto it = versions.find(op);
  if( it != versions.end() ) {
    auto vs = it->second;
    unsigned i = vs.size();
    while( i > 0  ) {
      i--;
      if( vs[i] <= c_count ) {
        c_count = vs[i];
        break;
      }
    }
    return read_term( op, c_count);
  }
  tiler_error( "llvmUtils", "versions of an LLVM value not found !!");
}

z3::expr value_expr_map::create_fresh_name( const llvm::Value* op, std::string op_name ) {
  llvm::Type* ty = op->getType();
  if( auto i_ty = llvm::dyn_cast<llvm::IntegerType>(ty) ) {
    int bw = i_ty->getBitWidth();
    if(bw == 16 || bw == 32 || bw == 64 ) {
      z3::expr i =  get_fresh_int(ctx, op_name, true);
      return i;
    }else if(bw == 1 || bw == 8) {
      z3::expr bit =  get_fresh_bool(ctx, op_name, true);
      return bit;
    }
  }
  ty->print( llvm::errs() ); llvm::errs() << "\n";
  tiler_error("llvmUtils", "unsupported type!!");
  z3::expr e(ctx);
  return e;
}

z3::expr value_expr_map::create_fresh_name( const llvm::Value* op  ) {
  llvm::Type* ty = op->getType();
  if( auto i_ty = llvm::dyn_cast<llvm::IntegerType>(ty) ) {
    int bw = i_ty->getBitWidth();
    if(bw == 16 || bw == 32 || bw == 64 ) {
      z3::expr i =  get_fresh_int(ctx, op->getName().str());
      return i;
    }else if(bw == 1 || bw == 8) {
      z3::expr bit =  get_fresh_bool(ctx, op->getName().str());
      return bit;
    }
  }
  ty->print( llvm::errs() ); llvm::errs() << "\n";
  tiler_error("llvmUtils", "unsupported type!!");
  z3::expr e(ctx);
  return e;
}

z3::expr value_expr_map::read_term( const llvm::Value* op, unsigned c_count ) {
  auto it = vmap.find( {op,c_count} );
  if( it != vmap.end() ) {
    return it->second;
  }else{
    z3::expr e(ctx);
    return e; // contains no expression;
  }
}

z3::expr value_expr_map::read_constant( const llvm::Value* op ) {
  return read_const(op, ctx);
}

unsigned value_expr_map::get_max_version( const llvm::Value* op ) {
  z3::expr c = read_constant( op );
  if( c ) return 0;
  auto it = versions.find(op);
  if( it == versions.end() ) {
    // tiler_error("bmc", "call insert_new_def instead of get_term !!");
    return 0;
  }else{
    return (it->second).back();
  }
}

const std::vector<unsigned>&
value_expr_map::get_versions( const llvm::Value* op ) {
  z3::expr c = read_constant( op );
  if( c ) return dummy_empty_versions;
  auto it = versions.find(op);
  if( it == versions.end() ) {
    // tiler_error("bmc", "call insert_new_def instead of get_term !!");
    return dummy_empty_versions;
  }else{
    return (it->second);
  }
}

// todo: violates invaiant of the system of multiple copies
void value_expr_map::copy_values(value_expr_map& m) {
  for(auto it = vmap.begin(); it != vmap.end(); ++it) {
    m.insert_term_map(it->first.first, it->first.second, it->second);
  }
}

void value_expr_map::print( std::ostream& o ) {
  for(auto it = vmap.begin(); it != vmap.end(); ++it) {
    const llvm::Value* I = it->first.first;
    unsigned version = it->first.second;
    z3::expr val = it->second;
    LLVM_DUMP(I);
    o << I << " " << version << "~~>" << val << "\n";
  }
}

inline void value_expr_map::dump() {
  print( std::cout );
}

std::list<z3::expr> value_expr_map::get_expr_list() {
  std::list<z3::expr> l;
  for(auto it = vmap.begin(); it != vmap.end(); ++it) {
    l.push_back(it->second);
  }
  return l;
}

//----------------------------------------------------------------------

char fun_clonner_mod_pass::ID = 0;

fun_clonner_mod_pass::
fun_clonner_mod_pass(options& o_,
                     std::string ns,
                     std::map<llvm::Function*, std::string>& fn_h_map,
                     std::map<std::string, llvm::ValueToValueMapTy>& fwd_v2v_map,
                     std::map<llvm::Function*, std::map<const llvm::Value*,
                       const llvm::Value*>>& rev_v2v_map,
                     std::map<const bb*, std::pair<std::vector<std::string>,
                       std::vector<std::string> > >& bb_cmt_map_,
                     bool mk_tgt
                     ) : llvm::ModulePass(ID)
  , o(o_)
  , fn_hash_map(fn_h_map)
  , fwd_fn_v2v_map(fwd_v2v_map)
  , rev_fn_v2v_map(rev_v2v_map)
  , bb_cmt_map(bb_cmt_map_)
{
  name_suffix = ns;
  make_target = mk_tgt;
}

bool fun_clonner_mod_pass::runOnModule(llvm::Module &M) {
  llvm::Function *F = M.getFunction(o.funcName);
  if(!F) {
    tiler_error("Cloning", "Function under analysis not present in the module");
  }
  std::string hash_str = random_string();
  llvm::ValueToValueMapTy& VMap = fwd_fn_v2v_map[hash_str];
  llvm::Function* C_F = llvm::CloneFunction(F, VMap);
  assert(C_F);
  /*
  // Copy all pairs from the the temporary vmap to the permanent one
  for (auto &KV : VMap)
    fwd_fn_v2v_map[C_F][KV.first] = KV.second;
  */
  fn_hash_map[C_F] = hash_str;
  C_F->setName(F->getName().str() + name_suffix);
  if(make_target) {
    // Clone is the new function under analysis
    o.funcName = F->getName().str() + name_suffix;
  } else {} // Clone is not immediately under analysis
  create_alloca_map(F, VMap, rev_fn_v2v_map[C_F]);
  if(make_target) {
    adjust_bb_cmt_map(VMap);
  } else {} // Pointers to blocks with comments are not shifted
  return true;
}

void fun_clonner_mod_pass::
create_alloca_map(llvm::Function* F,
                  llvm::ValueToValueMapTy& fn_vmap,
                  std::map<const llvm::Value*, const llvm::Value*>& alloca_map) {
  std::set<llvm::AllocaInst*> arr_set;
  collect_arr(*F, arr_set);
  for(auto arr_alloc : arr_set) {
    llvm::Value* mapped_alloc = fn_vmap[arr_alloc];
    alloca_map[mapped_alloc] = arr_alloc;
    // arr_alloc->print( llvm::outs() );
    // mapped_alloc->print( llvm::outs() );
  }
}

void fun_clonner_mod_pass::
adjust_bb_cmt_map(llvm::ValueToValueMapTy& fn_vmap) {
  std::map<const bb*,
           std::pair<std::vector<std::string>,
                     std::vector<std::string> > > cmt_map;
  for(auto it = bb_cmt_map.begin(); it!= bb_cmt_map.end(); it++) {
    llvm::WeakTrackingVH &c_b = fn_vmap[it->first];
    bb* clonedBB;
    if ((clonedBB = llvm::dyn_cast<llvm::BasicBlock>(c_b))) {
      cmt_map[clonedBB] = it->second;
      bb_cmt_map.erase(it->first);
    } else {
      tiler_error("llvmUtils", "Casting of the block failed");
    }
  }
  assert(bb_cmt_map.empty());
  bb_cmt_map.insert(cmt_map.begin(), cmt_map.end());
}

std::string fun_clonner_mod_pass::random_string() {
  std::string str("0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz");
  std::random_device rd;
  std::mt19937 generator(rd());
  std::shuffle(str.begin(), str.end(), generator);
  return str.substr(0, 32);
}

llvm::StringRef fun_clonner_mod_pass::getPassName() const {
  return "Create a clone of the function with given name. Attach the suffix \
          to the name of clone. Make this name as the target function name.";
}

//----------------------------------------------------------------------

char preprocess::ID = 0;

preprocess::preprocess(std::unique_ptr<llvm::Module>& m,
                       options& o_)
  : llvm::FunctionPass(ID)
  , module(m)
  , o(o_)
{}

preprocess::~preprocess() {}

bool preprocess::runOnFunction( llvm::Function &f ) {
  if(f.getName() != o.funcName)
    return false;

  target_f = &f;

  // Get entry block
  entry_bb = &target_f->getEntryBlock();

  // Identify  the program parameter, error if a suitable value is not found
  N = find_ind_param_N(module, target_f, entry_bb);
  assert(N);

  // Global N create multiple load instruction, we remove those
  // and map thier uses to the load instruction in the entry block
  if(llvm::isa<llvm::GlobalVariable>(N)) {
    remove_glb_N_loads();
    remove_unwanted_assume();
  }

  check_nesting_depth();

  return true;
}

void preprocess::remove_glb_N_loads() {
  for( auto it = entry_bb->begin(), e = entry_bb->end(); it != e; ++it) {
    llvm::Instruction *I = &(*it);
    if( llvm::LoadInst* load = llvm::dyn_cast<llvm::LoadInst>(I) ) {
      if(N == load->getOperand(0)) {
        N_load = load;
      } else {}
    } else {}
  }
  assert(N_load);
  if(N_load == NULL) tiler_error("preprocess",
                                 "First load of program parameter not found");
  llvm::ValueToValueMapTy VMap;
  llvm::SmallVector<llvm::BasicBlock*, 20> blocks;
  llvm::SmallVector<llvm::LoadInst*, 20> removeLoads;
  for( auto& b : target_f->getBasicBlockList() ) {
    if( &b == entry_bb) continue;
    for( auto it = b.begin(), e = b.end(); it != e; ++it) {
      llvm::Instruction *I = &(*it);
      if( llvm::LoadInst* load = llvm::dyn_cast<llvm::LoadInst>(I) ) {
        if(N == load->getOperand(0)) {
          VMap[load] = N_load;
          blocks.push_back(&b);
          removeLoads.push_back(load);
        } else {}
      } else {}
    }
  }
  llvm::remapInstructionsInBlocks(blocks, VMap);
  for(auto load : removeLoads)
    load->eraseFromParent();
}

void preprocess::remove_unwanted_assume() {
  llvm::Value* SingularNRef = NULL;
  if(llvm::isa<llvm::GlobalVariable>(N))
    SingularNRef = N_load;
  else
    SingularNRef = N;
  for( auto& b : target_f->getBasicBlockList() ) {
    for( auto it = b.begin(), e = b.end(); it != e; ++it) {
      llvm::Instruction *I = &(*it);
      if(llvm::CallInst* call = llvm::dyn_cast<llvm::CallInst>(I)) {
        if( llvm::isa<llvm::IntrinsicInst>(call) ) continue;
        if( call->getNumArgOperands() <= 0 ) continue;
        if( is_assume_call(call) ) {
          auto op = llvm::dyn_cast<llvm::Instruction>(call->getArgOperand(0));
          llvm::Instruction* cast1 = NULL;
          llvm::Instruction* cast2 = NULL;
          if( (cast1 = llvm::dyn_cast<llvm::CastInst>(op)) )
            op = cast1;
          if(llvm::ICmpInst* icmp = llvm::dyn_cast<llvm::ICmpInst>(op->getOperand(0))) {
            bool erase = false;
            if(llvm::isa<llvm::Constant>(icmp->getOperand(1))) {
              auto icmpop = llvm::dyn_cast<llvm::Instruction>(icmp->getOperand(0));
              if( (cast2 = llvm::dyn_cast<llvm::CastInst>(icmpop)) )
                icmpop = llvm::dyn_cast<llvm::Instruction>(cast2->getOperand(0));
              if(icmpop == SingularNRef)
                erase = true;
            }
            if(llvm::isa<llvm::Constant>(icmp->getOperand(0))) {
              auto icmpop = llvm::dyn_cast<llvm::Instruction>(icmp->getOperand(1));
              if( (cast2 = llvm::dyn_cast<llvm::CastInst>(icmpop)) )
                icmpop = llvm::dyn_cast<llvm::Instruction>(cast2->getOperand(0));
              if(icmpop == SingularNRef)
                erase = true;
            }
            if(erase) {
              call->eraseFromParent();
              cast1->eraseFromParent();
              icmp->eraseFromParent();
              cast2->eraseFromParent();
              return;
            }
          } else {}
        } else {}
      } else {}
    }
  }
}

void preprocess::check_nesting_depth() {
  llvm::LoopInfo &LI = getAnalysis<llvm::LoopInfoWrapperPass>().getLoopInfo();
  for (auto L = LI.rbegin(), E = LI.rend(); L != E; ++L) {
    // llvm::Loop *Lp = *L;
    for (llvm::Loop *SL : (*L)->getSubLoops()) {
      for (llvm::Loop *SSL : SL->getSubLoops()) {
        if(SSL)
          tiler_error("Preprocess", "Nesting depth greater than 2 is not supported");
      }
    }
  }
}

llvm::StringRef preprocess::getPassName() const {
  return "Identify the program parameter, check input program \
sanity, remove unnecessary stmts, and so on";
}

void preprocess::getAnalysisUsage(llvm::AnalysisUsage &au) const {
  au.addRequired<llvm::LoopInfoWrapperPass>();
}

//--------------------------------------------------------------------------

char count_loops::ID = 0;

count_loops::count_loops(std::unique_ptr<llvm::Module>& m,
                       options& o_)
  : llvm::FunctionPass(ID)
  , module(m)
  , o(o_)
{}

count_loops::~count_loops() {}

bool count_loops::runOnFunction( llvm::Function &f ) {
  if(f.getName() != o.funcName)
    return false;

  llvm::LoopInfo &LI = getAnalysis<llvm::LoopInfoWrapperPass>().getLoopInfo();
  for (auto L = LI.rbegin(), E = LI.rend(); L != E; ++L)
    loop_count++;

  return false;
}


llvm::StringRef count_loops::getPassName() const {
  return "Counts the number of top level loops in the program";
}

void count_loops::getAnalysisUsage(llvm::AnalysisUsage &au) const {
  au.addRequired<llvm::LoopInfoWrapperPass>();
}

//--------------------------------------------------------------------------

void merge_mono( const llvm_mono& m1, const llvm_mono& m2, llvm_mono& m ) {
  auto it1 = m1.begin();
  auto it2 = m2.begin();
  while( it1 != m1.end() && it2 != m2.end() ) {
    const auto& v1 = it1->first;
    const auto& v2 = it2->first;
    if( v1 == v2 ) {
      int c = it1->second + it2->second;
      if( c ) m[v1] = c;
      it1++;
      it2++;
    }else if( v1 < v2 ) {
      m[v1] = it1->second;
      it2++;
    }else{
      m[v2] = it2->second;
      it1++;
    }
  }
  while( it1 != m1.end() ) {
    m.insert( *it1 );
    it1++;
  }
  while( it2 != m2.end() ) {
    m.insert( *it2 );
    it2++;
  }
}

const llvm::DataLayout& getDataLayout( llvm::Instruction* I ) {
  return I->getParent()->getModule()->getDataLayout();
}

llvm::Constant* make_one_constant( llvm::LLVMContext& ctx ) {
  auto* intType = llvm::IntegerType::get (ctx, 32);
  return llvm::ConstantInt::getSigned( intType , 1);
}

llvm::Constant* add_constants( llvm::Constant* c1, llvm::Constant* c2
                               , const llvm::DataLayout& DL ) {
  return llvm::ConstantFoldBinaryOpOperands( llvm::Instruction::Add, c1,c2,DL);
}

llvm::Constant* sub_constants( llvm::Constant* c1, llvm::Constant* c2
                               , const llvm::DataLayout& DL ) {
  return llvm::ConstantFoldBinaryOpOperands( llvm::Instruction::Sub, c1,c2,DL);
}

llvm::Constant* mul_constants( llvm::Constant* c1, llvm::Constant* c2
                               , const llvm::DataLayout& DL ) {
  return llvm::ConstantFoldBinaryOpOperands( llvm::Instruction::Mul, c1,c2,DL);
}

llvm::Constant* min_constant( llvm::Constant* c1, const llvm::DataLayout& DL) {
  auto* intType = llvm::IntegerType::get (c1->getContext(), 32);
  llvm::Constant* m1 = llvm::ConstantInt::getSigned( intType , -1);
  return llvm::ConstantFoldBinaryOpOperands( llvm::Instruction::Mul, m1,c1,DL);
}

bool is_zero( llvm::Constant* c1 ) {
  assert(c1);
  return c1->isZeroValue();
}

bool is_eq( const llvm_mono& lhs, const llvm_mono& rhs ) {
  auto it1 = lhs.begin();
  auto it2 = rhs.begin();
  while( it1 != lhs.end() && it2 != rhs.end() ) {
    llvm::Value* i1 = it1->first;
    llvm::Value* i2 = it2->first;
    int c1 = it1->second;
    int c2 = it2->second;
    if( i1 == i2 && c1 == c2 ) {
      it1++;
      it2++;
    }else return false;
  }
  if( it1 != lhs.end() ) return false;
  if( it2 != rhs.end() ) return false;
  return true;
}

void
sum_poly( llvm_poly& poly1, llvm_poly& poly2, llvm_poly& poly,
          const llvm::DataLayout& DL) {
  auto it1 = poly1.begin();
  auto it2 = poly2.begin();
  while( it1 != poly1.end() && it2 != poly2.end() ) {
    const llvm_mono& m1 = it1->first;
    const llvm_mono& m2 = it2->first;
    if( is_eq( m1, m2 ) ) {
      llvm::Constant* c = add_constants( it1->second, it2->second, DL);
      if( !is_zero(c) ) poly[m1] = c;
      it1++;
      it2++;
    }else if( compare_mono()(m1,m2) ) {
      poly[m1] = it1->second;
      it2++;
    }else{
      poly[m2] = it2->second;
      it1++;
    }
  }
  while( it1 != poly1.end() ) {
    poly.insert( *it1 );
    it1++;
  }
  while( it2 != poly2.end() ) {
    poly.insert( *it2 );
    it2++;
  }
}

void
sub_poly( llvm_poly& poly1, llvm_poly& poly2, llvm_poly& poly,
          const llvm::DataLayout& DL ) {
  auto it1 = poly1.begin();
  auto it2 = poly2.begin();
  while( it1 != poly1.end() && it2 != poly2.end() ) {
    const llvm_mono& m1 = it1->first;
    const llvm_mono& m2 = it2->first;
    if( is_eq( m1, m2 ) ) {
      llvm::Constant* c = sub_constants( it1->second, it2->second, DL);
      if( !is_zero(c) ) poly[m1] = c;
      it1++;
      it2++;
    }else if( compare_mono()(m1,m2) ) {
      poly[m1] = it1->second;
      it2++;
    }else{
      poly[m2] = min_constant( it2->second, DL);
      it1++;
    }
  }
  while( it1 != poly1.end() ) {
    poly.insert( *it1 );
    it1++;
  }
  while( it2 != poly2.end() ) {
    const llvm_mono& m2 = it2->first;
    poly[m2] = min_constant( it2->second, DL );
    it2++;
  }
}


void
mul_poly( llvm_poly& poly1, llvm_poly& poly2, llvm_poly& poly
          , const llvm::DataLayout& DL ) {
  for( auto it1 = poly1.begin(); it1 != poly1.end(); it1++ ) {
    const llvm_mono& m1 = it1->first;
    for( auto it2 = poly2.begin(); it2 != poly2.end(); it2++ ) {
      const llvm_mono& m2 = it2->first;
      llvm_mono m;
      merge_mono(m1,m2,m);
      llvm::Constant* c = mul_constants( it1->second, it2->second, DL);
      if( exists( poly, m) ) {
        llvm::Constant* c1 = add_constants( c, poly[m], DL );
        if( is_zero(c1) ) {
          poly.erase(m);
        }else{
          poly[m] = c1;
        }
      }else{
        poly[m] = c;
      }
    }
  }
}

llvm_poly&
get_simplified_polynomial( llvm::Value* I,
                           std::map<llvm::Value*,llvm_poly>& poly_map,
                           const llvm::DataLayout& DL ) {
  if( exists( poly_map, I ) ) return poly_map[I];

  if(auto bop = llvm::dyn_cast<llvm::BinaryOperator>(I) ) {
      assert( bop );
      auto op0 = bop->getOperand( 0 );
      auto op1 = bop->getOperand( 1 );
      llvm_poly& poly1 = get_simplified_polynomial( op0, poly_map, DL );
      llvm_poly& poly2 = get_simplified_polynomial( op1, poly_map, DL );
      llvm_poly& poly = poly_map[I];// construct the empty poly map
      unsigned op = bop->getOpcode();
      switch( op ) {
      case llvm::Instruction::Add : sum_poly( poly1, poly2, poly, DL ); break;
      case llvm::Instruction::Sub : sub_poly( poly1, poly2, poly, DL ); break;
      case llvm::Instruction::Mul : mul_poly( poly1, poly2, poly, DL ); break;
      // case llvm::Instruction::And :
      // case llvm::Instruction::Or  :
      // case llvm::Instruction::Xor :
      // case llvm::Instruction::SDiv:
      // case llvm::Instruction::UDiv:
      // case llvm::Instruction::SRem:
      // case llvm::Instruction::URem:
      // case llvm::Instruction::FAdd:
      // case llvm::Instruction::FSub:
      // case llvm::Instruction::FMul:
      // case llvm::Instruction::FDiv:
      // case llvm::Instruction::FRem:
      default: {
        const char* opName = bop->getOpcodeName();
        tiler_error("simplfy polynomial", "unsupported instruction \"" << opName << "\" occurred!!");
      }
      }
  }else if(auto cint = llvm::dyn_cast<llvm::ConstantInt>(I) ) {
    llvm_poly& p = poly_map[I];
    llvm_mono m;
    p[m] = cint;
  } else if(auto load = llvm::dyn_cast<llvm::LoadInst>(I) ) {
    auto addr = load->getOperand(0);
    if( auto addr_bc = llvm::dyn_cast<llvm::BitCastInst>(addr) )
      addr = addr_bc->getOperand(0);
    if( llvm::isa<llvm::GetElementPtrInst>(addr) )
      tiler_error("simplfy polynomial", "array instructions unsupporetd supported in polys!!");
    llvm_poly& p = poly_map[addr];
    llvm_mono m;
    p[m] = make_one_constant( I->getContext() );
  } else {
    LLVM_DUMP_MARKED( I );
    tiler_error("simplfy polynomial", "unsupported instruction!!");
  }
  return poly_map[I];
}

// llvm_poly
void
get_simplified_polynomial( llvm::Value* I,
                           const std::vector<llvm::Value*>& vars,
                           const llvm::DataLayout& DL,
                           llvm_poly& p ) {
  // const llvm::DataLayout& DL = getDataLayout(I);
  std::map<llvm::Value*,llvm_poly> poly_map;
  for( llvm::Value* v : vars ) {
    llvm_mono m;
    m[v] = 1;
    llvm_poly& poly = poly_map[v];
    poly[m] = make_one_constant( I->getContext() );
  }
  p = get_simplified_polynomial( I, poly_map, DL );
  dump( p );
  // return p;
}

void dump( const llvm_mono& m ) {
  for( auto pair : m ) {
    auto& v = pair.first;
    auto& power = pair.second;
    v->print( llvm::errs() );;
    std::cerr << power << "\n";
  }
}

void dump( const llvm_poly& p ) {
  for( auto& pair : p ) {
    auto& m = pair.first;
    auto& coeff = pair.second;
    dump( m );
    std::cerr << "\n";
    coeff->print( llvm::errs() );
    std::cerr << "\n";
  }
}

