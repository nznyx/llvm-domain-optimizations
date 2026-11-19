#include "ErrorHeuristic.hpp"
#include "llvm/Passes/PassBuilder.h"
#if __has_include("llvm/Plugins/PassPlugin.h")
#include "llvm/Plugins/PassPlugin.h"
#else
#include "llvm/Passes/PassPlugin.h"
#endif

using namespace llvm;

#ifndef ERROR_NAMES
#define ERROR_NAMES 1
#endif
#ifndef BEAST_ERRORS
#define BEAST_ERRORS 1
#endif

extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo llvmGetPassPluginInfo() {
  return {
      LLVM_PLUGIN_API_VERSION, "ErrorHeuristic", "0.1", [](PassBuilder &PB) {
        PB.registerPipelineParsingCallback(
            [](StringRef Name, FunctionPassManager &PM,
               ArrayRef<PassBuilder::PipelineElement>) {
              if (Name == "error-names")
                PM.addPass(ErrorHeuristic({true, false}));
              else if (Name == "beast-errors")
                PM.addPass(ErrorHeuristic({false, true}));
              else if (Name == "error-heuristic")
                PM.addPass(ErrorHeuristic({true, true}));
              else
                return false;
              return true;
            });
        PB.registerPipelineStartEPCallback([](ModulePassManager &PM,
                                              OptimizationLevel) {
          FunctionPassManager FPM;
          FPM.addPass(ErrorHeuristic({ERROR_NAMES != 0, BEAST_ERRORS != 0}));
          PM.addPass(createModuleToFunctionPassAdaptor(std::move(FPM)));
        });
      }};
}
