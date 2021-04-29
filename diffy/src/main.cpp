
#include "utils/options.h"
#include "daikon-inst/cinput.h"

int main(int argc, char** argv) {
  options o;
  boost::filesystem::path def_config("default.conf");
  if ( boost::filesystem::exists( def_config ) ) {
    o.parse_config(def_config);
  }

  if (!o.parse_cmdline(argc, argv)) return 0; // help was called

  std::unique_ptr<llvm::Module> module;
  std::vector< comment > comments;
  parse_cpp_file(module, o, comments );
  if( o.verbosity > 8 ) {
    module->print( llvm::outs(), nullptr );
  }

  z3::context z3_ctx;
  std::map<llvm::Loop*, loopdata*> ld_map;
  name_map local_name_map;
  std::map<std::string, llvm::Value*> expr_val_map;
  value_expr_map def_map(z3_ctx);
  std::map<llvm::Function*, std::string> fn_hash_map;
  std::map<std::string, llvm::ValueToValueMapTy> fwd_func_v2v_map;
  std::map<llvm::Function*,
           std::map<const llvm::Value*,
                    const llvm::Value*>> rev_func_v2v_map;

  if( o.mode == 1 ) {
    // Running verification by tiling for arrays
    // std::unique_ptr<llvm::Module> inst_module = llvm::CloneModule(module.get());
    daikonInstNTileGen(module, o, z3_ctx, def_map, ld_map, local_name_map, expr_val_map);
    // std::unique_ptr<llvm::Module> slice_module = llvm::CloneModule(module.get());
    // slicedPostCondition(slice_module, o, z3_ctx);
    // verifySlicedPost();
    // std::unique_ptr<llvm::Module> nonint_module = llvm::CloneModule(module.get());
    //    nonInterference(nonint_module, o);
    //    verifyNonInterference();
  } else if ( o.mode == 2 ) {
    printCfgDot(module);
  } else if ( o.mode == 3 ) {
    run_bmc( module, comments,
             o, z3_ctx, def_map, ld_map, local_name_map, expr_val_map);
  } else if ( o.mode == 4 ) {
    run_FPI( module, comments, o, z3_ctx, def_map, ld_map, local_name_map,
             expr_val_map, fn_hash_map, fwd_func_v2v_map, rev_func_v2v_map);
  } else if ( o.mode == 5 ) {
    run_loop_peeling( module, comments, o, z3_ctx,
                      def_map, ld_map, local_name_map, expr_val_map);
  } else if ( o.mode == 6 ) {
    run_diff_preds( module, comments, o, z3_ctx, def_map, ld_map, local_name_map,
                    expr_val_map, fn_hash_map, fwd_func_v2v_map, rev_func_v2v_map);
  } else {
    llvm::errs() << "Incorrect parameter options passed to the tool\n";
  }

  if( o.dump_cfg ) {
    printCfgDot(module);
  }

  return 0;
}
