// При разборе документа одни и те же обработчики вызываются для многих полей.
// Находим в IR чтение байта с последующей проверкой или выбор по нескольким
// вариантам токена. Вокруг этих функций разрешаем дороже заплатить размером
// кода за инлайнинг горячего вызова. Крупный обработчик на редкой ветви
// остаётся отдельной функцией. Маленький LLVM по-прежнему может встроить:
// иногда после подстановки аргументов от него остаётся одна инструкция.
//
// Правило выполняется дважды. Второй раз LLVM уже упростил код после обычного
// инлайнинга, поэтому видны другие вызовы и их стоимость. Для решения хватает
// IR программы и статических оценок LLVM. Обучающий запуск не требуется.

#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/BlockFrequencyInfo.h"
#include "llvm/Analysis/InlineCost.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Plugins/PassPlugin.h"
#include "llvm/Transforms/IPO/AlwaysInliner.h"

using namespace llvm;

namespace {

struct CallEdge {
    CallBase *call;
    Function *callee;
    double frequency;
};

using CallGraph = MapVector<Function *, SmallVector<CallEdge>>;

// Короткие преобразования между загрузкой байта и проверкой встречаются,
// например, когда байт расширили до int или взяли из него несколько бит.
bool readsToken(Function &function) {
    for (Instruction &inst : instructions(function)) {
        if (auto *dispatch = dyn_cast<SwitchInst>(&inst);
            dispatch && dispatch->getNumCases() >= 3)
            return true;

        auto *load = dyn_cast<LoadInst>(&inst);
        if (!load || !load->isSimple() || !load->getType()->isIntegerTy(8))
            continue;
        for (User *use : load->users()) {
            if (isa<ICmpInst, SwitchInst>(use))
                return true;
            if (isa<CastInst, BinaryOperator>(use))
                for (User *next : use->users())
                    if (isa<ICmpInst, SwitchInst, GetElementPtrInst>(next))
                        return true;
        }
    }
    return false;
}

// Чтение, выделение узла и обновление курсора часто живут в разных функциях.
// Добавляем вызывающих и вызываемых по достаточно частым рёбрам.
// Частота 1 означает один визит в блок на один вход в функцию.
DenseSet<Function *> nearbyFunctions(const CallGraph &graph) {
    DenseSet<Function *> nearby;
    for (const auto &[function, edges] : graph)
        if (readsToken(*function))
            nearby.insert(function);

    for (unsigned step = 0; step < 3; ++step) {
        auto next = nearby;
        for (const auto &[caller, edges] : graph) {
            for (const auto &[call, callee, frequency] : edges) {
                if (nearby.contains(caller) && frequency >= 0.25)
                    next.insert(callee);
                if (nearby.contains(callee) && frequency >= 0.5)
                    next.insert(caller);
            }
        }
        nearby = std::move(next);
    }
    return nearby;
}

class ParserHotInline : public PassInfoMixin<ParserHotInline> {
public:
    PreservedAnalyses run(Module &module, ModuleAnalysisManager &analyses) {
        if (module.getProfileSummary(false) || module.getProfileSummary(true))
            return PreservedAnalyses::all();

        auto &functions = analyses.getResult<FunctionAnalysisManagerModuleProxy>(module).getManager();
        CallGraph graph;
        for (Function &function : module) {
            if (function.isDeclaration() || function.hasOptNone())
                continue;
            auto &frequencies = functions.getResult<BlockFrequencyAnalysis>(function);
            auto &edges = graph[&function];
            for (Instruction &inst : instructions(function)) {
                auto *call = dyn_cast<CallBase>(&inst);
                auto *callee = call ? call->getCalledFunction() : nullptr;
                if (!callee || callee == &function || callee->isDeclaration() || callee->hasOptNone())
                    continue;
                double visits = frequencies.getBlockFreq(call->getParent()).getFrequency();
                edges.push_back({call, callee, visits / frequencies.getEntryFreq().getFrequency()});
            }
        }

        auto nearby = nearbyFunctions(graph);
        auto assumptions = [&](Function &f) -> AssumptionCache & {
            return functions.getResult<AssumptionAnalysis>(f);
        };
        auto library = [&](Function &f) -> const TargetLibraryInfo & {
            return functions.getResult<TargetLibraryAnalysis>(f);
        };

        bool changed = false;
        for (auto &[caller, edges] : graph) {
            if (!nearby.contains(caller))
                continue;
            for (auto [call, callee, rate] : edges) {
                if (call->isNoInline() || call->hasFnAttr(Attribute::AlwaysInline) ||
                    call->isMustTailCall() || callee->isInterposable() ||
                    callee->hasFnAttribute(Attribute::NoInline) ||
                    callee->hasFnAttribute(Attribute::AlwaysInline))
                    continue;

                if (rate < 1.0 / 80) {
                    if (callee->getInstructionCount() >= 32) {
                        call->addFnAttr(Attribute::NoInline);
                        changed = true;
                    }
                    continue;
                }
                if (rate < 1.0 / 8 || (!nearby.contains(callee) && rate < 1))
                    continue;

                auto &target = functions.getResult<TargetIRAnalysis>(*callee);
                auto cost = getInlineCost(*call, getInlineParams(3000), target, assumptions, library);
                if (cost && !cost.isAlways()) {
                    call->addFnAttr(Attribute::AlwaysInline);
                    changed = true;
                }
            }
        }
        return changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
    }
};

} // namespace

extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo llvmGetPassPluginInfo() {
    return {LLVM_PLUGIN_API_VERSION, "ParserHotInline", "1", [](PassBuilder &builder) {
        builder.registerPipelineEarlySimplificationEPCallback(
            [](ModulePassManager &passes, OptimizationLevel level, ThinOrFullLTOPhase) {
                if (level == OptimizationLevel::O3)
                    passes.addPass(ParserHotInline());
            });
        builder.registerOptimizerEarlyEPCallback(
            [](ModulePassManager &passes, OptimizationLevel level, ThinOrFullLTOPhase) {
                if (level == OptimizationLevel::O3) {
                    passes.addPass(ParserHotInline());
                    passes.addPass(AlwaysInlinerPass());
                }
            });
        builder.registerPipelineParsingCallback(
            [](StringRef name, ModulePassManager &passes, ArrayRef<PassBuilder::PipelineElement>) {
                if (name != "parser-hot-inline")
                    return false;
                passes.addPass(ParserHotInline());
                return true;
            });
    }};
}
