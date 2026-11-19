#include "ErrorHeuristic.hpp"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/Demangle/Demangle.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/DebugProgramInstruction.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/Support/raw_ostream.h"
#include <cctype>
#include <optional>

using namespace llvm;

namespace {
// Match complete snake_case/camelCase words, including uppercase ERROR.
bool errorName(StringRef Name) {
  std::string Word;
  auto Match = [&] {
    return Word == "error" || Word == "errors" || Word == "err" ||
           Word == "ec" || Word == "errno" || Word == "failed" ||
           Word == "failure";
  };
  for (size_t I = 0; I < Name.size(); ++I) {
    unsigned char C = Name[I];
    unsigned char Previous = I ? Name[I - 1] : 0;
    unsigned char Next = I + 1 < Name.size() ? Name[I + 1] : 0;
    bool Boundary = std::isupper(C) && !Word.empty() &&
                    (std::islower(Previous) || std::isdigit(Previous) ||
                     (std::isupper(Previous) && std::islower(Next)));
    if (!std::isalnum(C) || Boundary) {
      if (Match())
        return true;
      Word.clear();
    }
    if (std::isalnum(C))
      Word += std::tolower(C);
  }
  return Match();
}
} // namespace

void ErrorHeuristic::collectHttpCodes(Module &M) {
  if (ScannedModule == &M)
    return;
  ScannedModule = &M;
  HttpCodes.clear();
  DebugInfoFinder Finder;
  Finder.processModule(M);
  for (auto *T : Finder.types()) {
    auto *Enum = dyn_cast<DICompositeType>(T);
    if (!Enum || Enum->getIdentifier() != "_ZTSN5boost5beast4http5errorE")
      continue;
    for (auto *Element : Enum->getElements()) {
      auto *E = dyn_cast<DIEnumerator>(Element);
      if (!E)
        continue;
      StringRef N = E->getName();
      Outcome O = Outcome::Unknown;
      if (N == "need_more" || N == "need_buffer" || N == "end_of_chunk" ||
          N == "end_of_stream")
        O = Outcome::Service;
      else if (N.starts_with("bad_") || N == "partial_message" ||
               N == "unexpected_body" || N == "buffer_overflow" ||
               N == "header_limit" || N == "body_limit" ||
               N == "multiple_content_length" || N == "stale_parser" ||
               N == "short_read" || N == "header_field_name_too_large" ||
               N == "header_field_value_too_large")
        O = Outcome::Failure;
      HttpCodes[E->getValue().getSExtValue()] = O;
    }
  }
}

std::optional<ErrorHeuristic::Outcome>
ErrorHeuristic::assignmentOutcome(CallBase &Call) const {
  auto *F = Call.getCalledFunction();
  if (!F)
    return std::nullopt;
  std::string N = demangle(F->getName().str());
  if (StringRef(N).contains(
          "boost::system::error_code::operator=<boost::beast::http::error>(")) {
    if (Call.arg_size() < 2)
      return Outcome::Unknown;
    auto *Code = dyn_cast<ConstantInt>(Call.getArgOperand(1));
    return Code ? HttpCodes.lookup(Code->getSExtValue()) : Outcome::Unknown;
  }
  if (!StringRef(N).starts_with("boost::system::error_code::assign(") &&
      !StringRef(N).starts_with("boost::system::error_code::operator=(") &&
      !StringRef(N).starts_with("boost::system::error_code::clear("))
    return std::nullopt;
  if (Call.arg_size() < 2)
    return Outcome::Unknown;
  Value *Source = Call.getArgOperand(1)->stripPointerCasts();
  for (auto It = Call.getIterator(); It != Call.getParent()->begin();) {
    auto *Ctor = dyn_cast<CallBase>(&*--It);
    if (!Ctor || !Ctor->getCalledFunction() || Ctor->arg_size() < 2)
      continue;
    if (Ctor->getArgOperand(0)->stripPointerCasts() != Source)
      continue;
    StringRef Name = Ctor->getCalledFunction()->getName();
    std::string Demangled = demangle(Name.str());
    if (!StringRef(Demangled).starts_with(
            "boost::system::error_code::error_code<boost::beast::http::error>"))
      return Outcome::Unknown;
    auto *Code = dyn_cast<ConstantInt>(Ctor->getArgOperand(1));
    return Code ? HttpCodes.lookup(Code->getSExtValue()) : Outcome::Unknown;
  }
  return Outcome::Unknown;
}

ErrorHeuristic::Outcome ErrorHeuristic::edgeOutcome(BasicBlock *BB) const {
  Outcome Result = Outcome::Unknown;
  // shortcut: only straight paths to a return; extend with dataflow if needed.
  for (unsigned Depth = 0; Depth < 8; ++Depth) {
    for (auto &I : *BB)
      if (auto *Call = dyn_cast<CallBase>(&I))
        if (auto O = assignmentOutcome(*Call))
          Result = *O;
    if (isa<ReturnInst>(BB->getTerminator()))
      return Result;
    auto *B = dyn_cast<UncondBrInst>(BB->getTerminator());
    if (!B)
      return Outcome::Unknown;
    BB = B->getSuccessor(0);
  }
  return Outcome::Unknown;
}

bool ErrorHeuristic::errorValue(Value *V, unsigned Depth) const {
  if (Depth > 8)
    return false;
  if (Errors.contains(V) || errorName(V->getName()))
    return true;
  if (auto *L = dyn_cast<LoadInst>(V))
    return errorValue(L->getPointerOperand(), Depth + 1);
  if (auto *C = dyn_cast<CastInst>(V)) {
    if (isa<ZExtInst>(C) || isa<SExtInst>(C) || isa<BitCastInst>(C))
      return errorValue(C->getOperand(0), Depth + 1);
    return false;
  }
  if (isa<CallBase>(V))
    return errorPolarity(V, Depth + 1) == true;
  // Do not follow arbitrary GEPs: fields of an error object can be categories,
  // flags, or pointers rather than the error code itself.
  return false;
}

std::optional<bool> ErrorHeuristic::errorPolarity(Value *V,
                                                  unsigned Depth) const {
  if (Depth > 8)
    return std::nullopt;
  if (auto *Cmp = dyn_cast<ICmpInst>(V)) {
    if (!Cmp->isEquality())
      return std::nullopt;
    Value *X = Cmp->getOperand(0), *Y = Cmp->getOperand(1);
    if (isa<Constant>(X) && !isa<Constant>(Y))
      std::swap(X, Y);
    if (auto *C = dyn_cast<Constant>(Y); C && C->isNullValue()) {
      auto P = errorPolarity(X, Depth + 1);
      if (P)
        return Cmp->getPredicate() == ICmpInst::ICMP_NE ? *P : !*P;
    }
    if (X->getType()->isIntegerTy(1)) {
      if (auto *C = dyn_cast<ConstantInt>(Y); C && C->isOne()) {
        auto P = errorPolarity(X, Depth + 1);
        if (P)
          return Cmp->getPredicate() == ICmpInst::ICMP_EQ ? *P : !*P;
      }
    }
    // Comparing a named error sentinel/code with another value: equality
    // denotes the rare outcome. Two error-like operands are ambiguous.
    if (errorValue(X) != errorValue(Y))
      return Cmp->getPredicate() == ICmpInst::ICMP_EQ;
    return std::nullopt;
  }
  if (auto *B = dyn_cast<BinaryOperator>(V);
      B && B->getType()->isIntegerTy(1) && B->getOpcode() == Instruction::Xor) {
    Value *X = B->getOperand(0), *Y = B->getOperand(1);
    if (isa<ConstantInt>(X))
      std::swap(X, Y);
    if (auto *C = dyn_cast<ConstantInt>(Y)) {
      auto P = errorPolarity(X, Depth + 1);
      if (P)
        return C->isOne() ? !*P : *P;
    }
    return std::nullopt;
  }
  if (auto *C = dyn_cast<CastInst>(V)) {
    if (isa<ZExtInst>(C) || isa<SExtInst>(C) || isa<BitCastInst>(C))
      return errorPolarity(C->getOperand(0), Depth + 1);
    return std::nullopt;
  }
  if (auto *Call = dyn_cast<CallBase>(V)) {
    auto *F = Call->getCalledFunction();
    if (!F)
      return std::nullopt;
    std::string Demangled = demangle(F->getName().str());
    StringRef N(Demangled);
    // Only inspect the qualified function name, not parameter names/types.
    N = N.take_until([](char C) { return C == '('; });
    bool ErrorType =
        N.contains("error_code::") || N.contains("error_condition::");
    if (ErrorType && N.ends_with("operator bool"))
      return true;
    if (ErrorType && N.ends_with("operator!"))
      return false;
    // shortcut: names encode nonzero-as-error, not arbitrary API semantics.
    // Restrict to scalar getters/predicates, never parse(..., error_code&).
    StringRef Method =
        N.substr(N.rfind("::") == StringRef::npos ? 0 : N.rfind("::") + 2);
    if (errorName(Method) &&
        (Call->getType()->isIntegerTy() || Call->getType()->isPointerTy()))
      return true;
    return std::nullopt;
  }
  if (errorValue(V))
    return true;
  return std::nullopt;
}

PreservedAnalyses ErrorHeuristic::run(Function &F, FunctionAnalysisManager &) {
  if (Options.BeastErrors)
    collectHttpCodes(*F.getParent());
  Errors.clear();
  for (auto &BB : F)
    for (auto &I : BB) {
      for (auto &Record : I.getDbgRecordRange()) {
        auto *D = dyn_cast<DbgVariableRecord>(&Record);
        if (!D || (!D->isDbgDeclare() && !D->isDbgValue()) ||
            !errorName(D->getVariable()->getName()) ||
            D->getNumVariableLocationOps() != 1 || D->isKillLocation() ||
            D->getExpression()->getNumElements() != 0)
          continue;
        auto *V = D->getVariableLocationOp(0);
        if (V && !isa<Constant>(V))
          Errors.insert(V);
      }
    }
  unsigned Count = 0, AssignmentCount = 0;
  for (auto &BB : F) {
    auto *B = dyn_cast<CondBrInst>(BB.getTerminator());
    if (!B || B->getMetadata(LLVMContext::MD_prof))
      continue;
    std::optional<bool> P;
    if (Options.BeastErrors) {
      Outcome T = edgeOutcome(B->getSuccessor(0));
      Outcome E = edgeOutcome(B->getSuccessor(1));
      if ((T == Outcome::Failure) != (E == Outcome::Failure)) {
        P = T == Outcome::Failure;
        ++AssignmentCount;
      } else if (T != Outcome::Unknown || E != Outcome::Unknown) {
        continue;
      }
    }
    if (!P && Options.Names)
      P = errorPolarity(B->getCondition());
    if (!P)
      continue;
    B->setMetadata(LLVMContext::MD_prof,
                   MDBuilder(F.getContext())
                       .createBranchWeights(*P ? 1 : 2000, *P ? 2000 : 1));
    ++Count;
  }
  if (Count)
    errs() << "error-heuristic: " << F.getName() << ": " << Count
           << " branches\n";
  if (AssignmentCount)
    errs() << "error-assignments: " << F.getName() << ": " << AssignmentCount
           << " branches\n";
  return Count ? PreservedAnalyses::none() : PreservedAnalyses::all();
}
