#ifndef BMC_UTILS_H
#define BMC_UTILS_H

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
// pragam'ed to aviod warnings due to llvm included files
#include "llvm/IR/Instructions.h"
#pragma GCC diagnostic pop

// contains the the list of function names that has special interprations

bool is_assert( const llvm::CallInst* call );
bool is_assume( const llvm::CallInst* call );
bool is_nondet( const llvm::CallInst* call );
bool is_error( const llvm::CallInst* call );
bool ignore_special_functions( const llvm::CallInst* fp );

#endif // BMC_UTILS_H
