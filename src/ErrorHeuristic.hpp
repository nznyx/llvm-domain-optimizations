#pragma once

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/IR/PassManager.h"
#include <cstdint>
#include <optional>

namespace llvm {
class CallBase;
}

struct ErrorHeuristicOptions {
  bool Names = true;
  bool BeastErrors = true;
};

class ErrorHeuristic : public llvm::PassInfoMixin<ErrorHeuristic> {
public:
  explicit ErrorHeuristic(ErrorHeuristicOptions Options = {})
      : Options(Options) {}
  llvm::PreservedAnalyses run(llvm::Function &F,
                              llvm::FunctionAnalysisManager &AM);

private:
  enum class Outcome { Unknown, Failure, Service };
  ErrorHeuristicOptions Options;
  llvm::DenseSet<const llvm::Value *> Errors;
  llvm::DenseMap<int64_t, Outcome> HttpCodes;
  const llvm::Module *ScannedModule = nullptr;

  void collectHttpCodes(llvm::Module &M);
  std::optional<Outcome> assignmentOutcome(llvm::CallBase &Call) const;
  Outcome edgeOutcome(llvm::BasicBlock *BB) const;
  bool errorValue(llvm::Value *V, unsigned Depth = 0) const;
  std::optional<bool> errorPolarity(llvm::Value *V, unsigned Depth = 0) const;
};
