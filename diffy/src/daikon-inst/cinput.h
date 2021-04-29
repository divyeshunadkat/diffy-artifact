#ifndef CINPUT_H
#define CINPUT_H

#include "utils/options.h"
#include "utils/llvmUtils.h"
#include "daikon-inst/loopdata.h"
#include "daikon-inst/comments.h"
#include "z3++.h"
#include "bmc/bmc.h"

//todo: collected comments should be part of a class
//      to enable concurrent thread executions

void parse_cpp_file( std::unique_ptr<llvm::Module>& module,
                     options& o, std::vector< comment >& );

void parse_cpp_file( std::unique_ptr<llvm::Module>&, options& );

void daikonInstNTileGen( std::unique_ptr<llvm::Module>& module,
                         options& o, z3::context& z3_,
                         value_expr_map& m_,
                         std::map<llvm::Loop*, loopdata*>& ld_map,
                         name_map& lMap,
                         std::map<std::string, llvm::Value*>& evMap);

void slicedPostCondition( std::unique_ptr<llvm::Module>& module,
                          options& o,
                          z3::context& z3_);

void printCfgDot( std::unique_ptr<llvm::Module>& module );

void run_bmc( std::unique_ptr<llvm::Module>& module,
              std::vector< comment >&,
              options& o, z3::context& z3_ctx,
              value_expr_map& m_,
              std::map<llvm::Loop*, loopdata*>& ld_map,
              name_map& lMap,
              std::map<std::string, llvm::Value*>& evMap);

void run_loop_peeling( std::unique_ptr<llvm::Module>& module,
                       std::vector<comment>& comments,
                       options& o, z3::context& z3_ctx,
                       value_expr_map& def_map_,
                       std::map<llvm::Loop*, loopdata*>& ld_map,
                       name_map& lMap,
                       std::map<std::string, llvm::Value*>& evMap);

llvm::Value* run_preprocess_pass(std::unique_ptr<llvm::Module>& module, options& o);

void run_simplify_nl_pass( std::unique_ptr<llvm::Module>& module,
                           std::vector<comment>& comments,
                           options& o, z3::context& z3_ctx,
                           value_expr_map& def_map_,
                           std::map<llvm::Loop*, loopdata*>& ld_map,
                           name_map& lMap,
                           std::map<std::string, llvm::Value*>& evMap,
                           bmc& b, llvm::Value* N
                           );

void run_array_ssa_pass( std::unique_ptr<llvm::Module>& module,
                         std::vector<comment>& comments,
                         options& o, z3::context& z3_ctx,
                         value_expr_map& def_map_,
                         std::map<llvm::Loop*, loopdata*>& ld_map,
                         name_map& lMap,
                         std::map<std::string, llvm::Value*>& evMap,
                         std::map<llvm::Function*, std::string>&,
                         std::map<std::string, llvm::ValueToValueMapTy>&,
                         std::map<llvm::Function*, std::map<const llvm::Value*,
                         const llvm::Value*>>&,
                         std::map<const bb*, std::pair<std::vector<std::string>,
                         std::vector<std::string>>>& bb_cmt_map,
                         bmc& b
                         );

void run_FPI( std::unique_ptr<llvm::Module>& module,
              std::vector<comment>& comments,
              options& o, z3::context& z3_ctx,
              value_expr_map& def_map_,
              std::map<llvm::Loop*, loopdata*>& ld_map,
              name_map& lMap,
              std::map<std::string, llvm::Value*>& evMap,
              std::map<llvm::Function*, std::string>&,
              std::map<std::string, llvm::ValueToValueMapTy>&,
              std::map<llvm::Function*,
                std::map<const llvm::Value*, const llvm::Value*>>&);

void run_create_pnm1_pass( std::unique_ptr<llvm::Module>& module,
                           options& o,
                           std::map<llvm::Function*, std::string>&,
                           std::map<std::string, llvm::ValueToValueMapTy>&,
                           std::map<llvm::Function*, std::map<const llvm::Value*,
                           const llvm::Value*>>&,
                           std::map<const bb*, std::pair<std::vector<std::string>,
                           std::vector<std::string>>>& bb_cmt_map,
                           llvm::Value* N
                           );

void run_create_qnm1_pass( std::unique_ptr<llvm::Module>& module,
                            options& o,
                            std::map<llvm::Function*, std::string>&,
                            std::map<std::string, llvm::ValueToValueMapTy>&,
                            std::map<llvm::Function*, std::map<const llvm::Value*,
                            const llvm::Value*>>&,
                            std::map<const bb*, std::pair<std::vector<std::string>,
                            std::vector<std::string>>>& bb_cmt_map,
                            llvm::Value* N
                            );

void run_peel_pass( std::unique_ptr<llvm::Module>& module,
                    std::vector<comment>& comments,
                    options& o, z3::context& z3_ctx,
                    value_expr_map& def_map_,
                    std::map<llvm::Loop*, loopdata*>& ld_map,
                    name_map& lMap,
                    std::map<std::string, llvm::Value*>& evMap,
                    std::map<llvm::Function*, std::string>&,
                    std::map<std::string, llvm::ValueToValueMapTy>&,
                    std::map<llvm::Function*, std::map<const llvm::Value*,
                    const llvm::Value*>>&,
                    std::map<const bb*, std::pair<std::vector<std::string>,
                    std::vector<std::string>>>& bb_cmt_map,
                    bmc& b, llvm::Value* N, int& unsupported_val
                    );

void run_diff_pass( std::unique_ptr<llvm::Module>& module,
                    std::vector<comment>& comments,
                    options& o, z3::context& z3_ctx,
                    value_expr_map& def_map_,
                    std::map<llvm::Loop*, loopdata*>& ld_map,
                    name_map& lMap,
                    std::map<std::string, llvm::Value*>& evMap,
                    std::map<llvm::Function*, std::string>& fn_hash_map,
                    std::map<std::string, llvm::ValueToValueMapTy>& fwd_fn_v2v_map,
                    std::map<llvm::Function*, std::map<const llvm::Value*,
                    const llvm::Value*>>& rev_fn_v2v_map,
                    std::map<const bb*, std::pair<std::vector<std::string>,
                    std::vector<std::string>>>& bb_cmt_map,
                    bmc& b, llvm::Value* N, int& unsupported_val
                    );

void run_diff_preds( std::unique_ptr<llvm::Module>& module,
                     std::vector<comment>& comments,
                     options& o, z3::context& z3_ctx,
                     value_expr_map& def_map_,
                     std::map<llvm::Loop*, loopdata*>& ld_map,
                     name_map& lMap,
                     std::map<std::string, llvm::Value*>& evMap,
                     std::map<llvm::Function*, std::string>&,
                     std::map<std::string, llvm::ValueToValueMapTy>&,
                     std::map<llvm::Function*,
                       std::map<const llvm::Value*, const llvm::Value*>>&);

#endif // CINPUT_H
