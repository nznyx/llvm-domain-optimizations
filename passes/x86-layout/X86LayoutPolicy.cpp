#include "llvm/TargetParser/Triple.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Plugins/PassPlugin.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorHandling.h"

#include <mutex>

using namespace llvm;

namespace {
void setBackendOption(StringRef name, StringRef value) {
  auto &options = cl::getRegisteredOptions();
  auto it = options.find(name);
  if (it == options.end())
    report_fatal_error(Twine("missing x86 backend option: ") + name);
  auto *option = it->second;
  if (option->getNumOccurrences() != 0 || option->addOccurrence(0, name, value))
    report_fatal_error(Twine("cannot set x86 backend option: ") + name);
}

struct X86LayoutPolicy : PassInfoMixin<X86LayoutPolicy> {
  PreservedAnalyses run(Module &module, ModuleAnalysisManager &) {
    if (Triple(module.getTargetTriple()).getArch() != Triple::x86_64)
      return PreservedAnalyses::all();

    // Both transformations belong to LLVM's x86 backend. This pass only
    // selects one fixed policy per Full LTO invocation.
    static std::once_flag configured;
    std::call_once(configured, [] {
      setBackendOption("x86-branches-within-32B-boundaries", "true");
      setBackendOption("x86-experimental-pref-innermost-loop-alignment", "6");
    });
    return PreservedAnalyses::all();
  }
};
}

extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo llvmGetPassPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "X86LayoutPolicy", "0.1", [](PassBuilder &builder) {
    builder.registerFullLinkTimeOptimizationEarlyEPCallback(
        [](ModulePassManager &passes, OptimizationLevel level) {
          if (level == OptimizationLevel::O3) passes.addPass(X86LayoutPolicy());
        });
  }};
}
