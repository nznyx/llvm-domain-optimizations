// Лексер читает байты и передаёт парсеру токены, например имена, числа и знаки.
// Flex генерирует код лексера по правилам вроде «имя начинается с буквы
// и продолжается буквами или цифрами».
//
// TransitionSplit рассматривает лексер как конечный автомат. У него есть текущий
// номер состояния, например 7 может означать «внутри имени». Состояние и входной
// байт определяют следующий переход. Ради экономии памяти лексер может объединять
// байты с одинаковым поведением в классы и хранить переходы в сжатом виде.
// Тогда для каждого байта приходится искать ответ через запасные состояния или
// среди диапазонов символов. Именно этот повторяющийся поиск нужен проходу.
// Он находит его в LLVM IR (промежуточном коде компилятора), подставляет состояния
// и символы в исходные инструкции и записывает вычисленные ответы в таблицу.
//
// При разборе входа горячий путь (hot path) получает ответ одним чтением по индексу.
// Для пары без готового ответа холодный путь (cold path) вызывает исходный поиск.
// Проход рассчитывает, что первый случай встречается чаще. Если парсер уже получает
// переход одним чтением или редко использует известные пары, ожидать выигрыша оснований мало.
//
// Рассмотрим «abc;» в состоянии чтения имени. Назначение состояния проходу
// неизвестно. Он проверяет по инструкциям, что чтение конкретной буквы возвращает
// автомат в то же состояние и сдвигает указатель ровно на байт. Такие буквы
// можно читать в коротком цикле. Первый байт с другим поведением, здесь ';',
// остаётся исходному лексеру. Если на букве надо сохранить позицию последнего
// принятого токена, быстрый цикл обязан повторить и эту запись.

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/ConstantFolding.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/EHPersonalities.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Plugins/PassPlugin.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/SSAUpdater.h"
#include "llvm/Transforms/Utils/ValueMapper.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>

using namespace llvm;

namespace {

// Заранее читать можно только таблицу с известным содержимым, которое не меняется.
GlobalVariable *immutable(Value *v) {
    auto *g = dyn_cast<GlobalVariable>(getUnderlyingObject(v));
    return g && g->isConstant() && g->hasDefinitiveInitializer() && !g->isExternallyInitialized()
               ? g
               : nullptr;
}

// Исключаем функции без тела и функции, из которых нельзя безопасно вынести поиск.
bool eligible(const Function &caller) {
    return !caller.isDeclaration() && !caller.hasFnAttribute(Attribute::Cold) &&
           !caller.hasFnAttribute(Attribute::Naked) &&
           (!caller.hasPersonalityFn() ||
            !isFuncletEHPersonality(classifyEHPersonality(caller.getPersonalityFn())));
}

// Создаём отдельную функцию для исходного поиска. Атрибут cold помогает LLVM разместить
// её вдали от основного пути, а noinline сохраняет границу при дальнейшей оптимизации.
Function *createColdFallback(Function &caller, FunctionType *type) {
    auto *fn = Function::Create(type, GlobalValue::InternalLinkage, caller.getAddressSpace(),
                                caller.getName() + ".transition.cold", caller.getParent());
    fn->addFnAttr(Attribute::NoInline);
    fn->addFnAttr(Attribute::Cold);
    // Вынесенный код должен работать с теми же настройками процессора, памяти и санитайзеров.
    for (auto name : {"target-cpu", "target-features", "tune-cpu"})
        if (caller.hasFnAttribute(name))
            fn->addFnAttr(caller.getFnAttribute(name));
    for (auto attr :
         {Attribute::NullPointerIsValid, Attribute::NoRedZone, Attribute::SanitizeAddress,
          Attribute::SanitizeThread, Attribute::SanitizeMemory, Attribute::SanitizeHWAddress})
        if (caller.hasFnAttribute(attr))
            fn->addFnAttr(attr);
    return fn;
}

constexpr StringLiteral StateTableTag = "parser.transition.rows";

// Оба вида поиска оставляют позднему этапу число состояний в построенной таблице.
void markStateTable(GlobalVariable *table, unsigned rows) {
    auto &ctx = table->getContext();
    auto *count = ConstantInt::get(Type::getInt32Ty(ctx), rows);
    table->setMetadata(StateTableTag, MDNode::get(ctx, ConstantAsMetadata::get(count)));
}

// Помещаем вычисленные при компиляции числа в неизменяемую внутреннюю таблицу модуля.
GlobalVariable *createTable(Module &module, Constant *data, StringRef name) {
    auto *table =
        new GlobalVariable(module, data->getType(), true, GlobalValue::PrivateLinkage, data, name);
    table->setUnnamedAddr(GlobalValue::UnnamedAddr::Global);
    return table;
}

// Узнаём операцию table[index] по вычислению адреса элемента и чтению по этому адресу.
// Размер и тип элементов берём из самой таблицы.
GlobalVariable *loadedConstantArray(Value *v) {
    auto *load = dyn_cast<LoadInst>(v);
    auto *gep = load ? dyn_cast<GetElementPtrInst>(load->getPointerOperand()) : nullptr;
    auto *g = gep ? dyn_cast<GlobalVariable>(gep->getPointerOperand()) : nullptr;
    if (!load || !load->isSimple() || !g || !g->isConstant() || !g->hasDefinitiveInitializer() ||
        g->isExternallyInitialized() || !isa<ArrayType>(g->getValueType()) ||
        gep->getSourceElementType() != g->getValueType() || gep->getNumIndices() != 2 ||
        !PatternMatch::match(gep->getOperand(1), PatternMatch::m_Zero()) ||
        cast<ArrayType>(g->getValueType())->getElementType() != load->getType())
        return nullptr;
    return g;
}

// Прослеживаем целое число до смены его разрядности, чтобы найти источник индекса.
Value *stripIntegerCasts(Value *v) {
    while (isa<TruncInst, SExtInst, ZExtInst>(v))
        v = cast<CastInst>(v)->getOperand(0);
    return v;
}

struct DefaultSearch {
    BasicBlock *entry, *header, *exit;
    SmallVector<BasicBlock *> blocks;
    SmallVector<Instruction *> code;
    PHINode *state, *symbol;
    Value *stateIn, *symbolIn;
    LoadInst *result;
    unsigned states;
    SmallVector<unsigned> alphabet;
};

// Перебираем 256 значений классификатора. Если 'a' и 'b' дают один класс, в таблице
// переходов им понадобится одна ячейка. Преобразования разрядности повторяем как в лексере.
bool collectByteClasses(Value *in, GlobalVariable *table, SmallVectorImpl<unsigned> &alphabet) {
    SmallVector<CastInst *> casts;
    for (Value *v = in; isa<TruncInst, SExtInst, ZExtInst>(v);) {
        auto *cast = llvm::cast<CastInst>(v);
        casts.push_back(cast);
        v = cast->getOperand(0);
    }
    for (unsigned byte = 0; byte < 256; ++byte) {
        Constant *c = table->getInitializer()->getAggregateElement(byte);
        if (!isa_and_nonnull<ConstantInt>(c))
            return false;
        for (auto *cast : reverse(casts))
            c = ConstantFoldInstOperands(cast, {c}, table->getDataLayout());
        auto *n = dyn_cast_or_null<ConstantInt>(c);
        if (!n || n->getValue().ugt(255))
            return false;
        unsigned sym = n->getZExtValue();
        if (!llvm::is_contained(alphabet, sym))
            alphabet.push_back(sym);
    }
    return true;
}

// Ищем внутренний цикл поиска через запасные состояния. На входе у него два меняющихся
// числа. LLVM представляет их инструкциями PHI. Такая инструкция выбирает значение
// по тому, откуда пришло управление. Число из классификатора байтов играет роль символа,
// второе служит индексом таблицы состояний. Из поиска наружу должен выходить один результат.
std::optional<DefaultSearch> findDefaultSearch(Loop &loop) {
    DefaultSearch search{};
    search.entry = loop.getLoopPreheader();
    search.header = loop.getHeader();
    search.exit = loop.getExitBlock();
    if (!search.entry || !search.exit || !search.exit->getSinglePredecessor() ||
        loop.getNumBlocks() > 8 || !loop.getSubLoops().empty() ||
        std::distance(search.header->phis().begin(), search.header->phis().end()) != 2)
        return {};
    for (PHINode &p : search.header->phis()) {
        if (!p.getType()->isIntegerTy() || p.getType()->getIntegerBitWidth() > 64)
            return {};
        Value *in = p.getIncomingValueForBlock(search.entry);
        auto *table = loadedConstantArray(stripIntegerCasts(in));
        if (table && cast<ArrayType>(table->getValueType())->getNumElements() == 256) {
            search.symbol = &p;
            search.symbolIn = in;
            if (!collectByteClasses(in, table, search.alphabet))
                return {};
        } else {
            search.state = &p;
            search.stateIn = in;
        }
    }
    if (!search.state || !search.symbol || isa<Constant>(search.stateIn) ||
        search.stateIn == search.symbolIn || search.alphabet.size() < 3)
        return {};
    search.blocks.assign(loop.block_begin(), loop.block_end());
    search.blocks.push_back(search.exit);
    for (auto *bb : search.blocks) {
        if (bb->hasAddressTaken() || bb->isEHPad())
            return {};
        for (auto &i : *bb) {
            if (i.isDebugOrPseudoInst())
                return {};
            if (auto *table = loadedConstantArray(&i)) {
                auto *idx = cast<GetElementPtrInst>(i.getOperand(0))->getOperand(2);
                if (stripIntegerCasts(idx) == search.state)
                    search.states = cast<ArrayType>(table->getValueType())->getNumElements();
                if (bb == search.exit)
                    search.result = cast<LoadInst>(&i);
            } else if (!isa<PHINode, GetElementPtrInst, ICmpInst, BinaryOperator, SelectInst,
                            TruncInst, SExtInst, ZExtInst, CondBrInst, UncondBrInst>(i))
                return {};
            if (bb == search.exit && i.isTerminator())
                return {};
            search.code.push_back(&i);
            if (search.result)
                break;
        }
    }
    if (!search.result || !search.result->getType()->isIntegerTy() ||
        search.result->getType()->getIntegerBitWidth() > 32 || search.states < 4 ||
        search.states > 8192)
        return {};
    SmallPtrSet<Instruction *, 32> inside(search.code.begin(), search.code.end());
    for (auto *i : search.code) {
        for (Value *op : i->operands())
            if (!isa<BasicBlock, Constant>(op) &&
                !(isa<Instruction>(op) && inside.contains(cast<Instruction>(op))) &&
                !(isa<PHINode>(i) && i->getParent() == search.header &&
                  (op == search.stateIn || op == search.symbolIn)))
                return {};
        if (i != search.result)
            for (User *u : i->users())
                if (!isa<Instruction>(u) || !inside.contains(cast<Instruction>(u)))
                    return {};
    }
    return search;
}

// Подставляем одну пару «состояние, класс» и идём по тем же ветвям, что и исходный цикл.
// LLVM вычисляет арифметику и чтения констант. Если ответ неизвестен или поиск затянулся,
// оставляем эту пару исходному коду.
std::optional<uint32_t> evaluateDefaultSearch(const DefaultSearch &search, unsigned state,
                                              unsigned symbol) {
    const DataLayout &dl = search.header->getDataLayout();
    DenseMap<Value *, Constant *> values;
    auto value = [&](Value *v) -> Constant * {
        if (v == search.stateIn)
            return ConstantInt::get(v->getType(), state);
        if (v == search.symbolIn)
            return ConstantInt::get(v->getType(), symbol);
        if (auto *c = dyn_cast<Constant>(v))
            return c;
        return values.lookup(v);
    };
    BasicBlock *prev = search.entry, *bb = search.header;
    for (unsigned step = 0; step < 64; ++step) {
        SmallVector<std::pair<PHINode *, Constant *>> phis;
        for (auto &p : bb->phis())
            phis.emplace_back(&p, value(p.getIncomingValueForBlock(prev)));
        for (auto [p, c] : phis)
            values[p] = c;
        for (auto &i : *bb) {
            if (isa<PHINode>(i))
                continue;
            if (i.isTerminator()) {
                unsigned edge = 0;
                if (auto *b = dyn_cast<CondBrInst>(&i)) {
                    auto *c = dyn_cast_or_null<ConstantInt>(value(b->getCondition()));
                    if (!c)
                        return {};
                    edge = c->isZero();
                }
                prev = bb;
                bb = i.getSuccessor(edge);
                break;
            }
            SmallVector<Constant *, 4> ops;
            for (Value *op : i.operands()) {
                auto *c = value(op);
                if (!c || isa<UndefValue>(c))
                    return {};
                ops.push_back(c);
            }
            Constant *c = isa<LoadInst>(i) ? ConstantFoldLoadFromConstPtr(ops[0], i.getType(), dl)
                                           : ConstantFoldInstOperands(&i, ops, dl, nullptr, false);
            if (!c || isa<UndefValue>(c))
                return {};
            values[&i] = c;
            if (&i == search.result) {
                auto *n = dyn_cast<ConstantInt>(c);
                if (!n)
                    return {};
                return n->getZExtValue();
            }
        }
    }
    return {};
}

struct BranchTest {
    Value *root = nullptr;
    SmallVector<Instruction *> operations;
};

// Иногда сразу после поиска лексер по новому состоянию решает, продолжать ли токен.
// Собираем инструкции этой проверки, если она зависит только от результата и констант.
BranchTest findBranchTest(const DefaultSearch &search) {
    BranchTest p;
    auto *branch = dyn_cast<CondBrInst>(search.exit->getTerminator());
    if (!branch)
        return {};
    for (Instruction *i = search.result->getNextNode(); i != branch; i = i->getNextNode())
        if (i->mayHaveSideEffects() || (i->mayReadFromMemory() && !loadedConstantArray(i)))
            return {};
    SmallPtrSet<Value *, 16> seen;
    auto visit = [&](auto &&self, Value *v) -> bool {
        if (v == search.result || isa<ConstantInt, GlobalVariable>(v))
            return true;
        auto *i = dyn_cast<Instruction>(v);
        if (!i || i->getParent() != search.exit ||
            (!loadedConstantArray(i) &&
             !isa<ICmpInst, TruncInst, ZExtInst, SExtInst, BinaryOperator, GetElementPtrInst>(i)))
            return false;
        if (!seen.insert(i).second)
            return true;
        for (Value *op : i->operands())
            if (!self(self, op))
                return false;
        p.operations.push_back(i);
        return true;
    };
    if (!visit(visit, branch->getCondition()) || p.operations.empty())
        return {};
    p.root = branch->getCondition();
    return p;
}

// Вычисляем исход проверки для известного перехода. Его можно хранить одним битом рядом с ответом.
std::optional<bool> evaluateBranchTest(const DefaultSearch &search, const BranchTest &p,
                                       unsigned next) {
    DenseMap<Value *, Constant *> values;
    values[search.result] = ConstantInt::get(search.result->getType(), next);
    const auto &dl = search.exit->getDataLayout();
    for (auto *i : p.operations) {
        SmallVector<Constant *, 4> ops;
        for (Value *op : i->operands()) {
            auto *c = isa<Constant>(op) ? cast<Constant>(op) : values.lookup(op);
            if (!c || isa<UndefValue>(c))
                return {};
            ops.push_back(c);
        }
        auto *c = isa<LoadInst>(i) ? ConstantFoldLoadFromConstPtr(ops[0], i->getType(), dl)
                                   : ConstantFoldInstOperands(i, ops, dl, nullptr, false);
        if (!c || isa<UndefValue>(c))
            return {};
        values[i] = c;
    }
    auto *flag = dyn_cast_or_null<ConstantInt>(values.lookup(p.root));
    if (!flag)
        return {};
    return !flag->isZero();
}

// На запасном пути результат поиска известен только при разборе. Здесь восстанавливаем исходную проверку.
Value *emitOriginalBranchTest(IRBuilder<> &b, const DefaultSearch &search, const BranchTest &p,
                              Value *next) {
    DenseMap<Value *, Value *> values;
    values[search.result] = next;
    for (auto *i : p.operations) {
        auto *copy = i->clone();
        for (unsigned j = 0; j < i->getNumOperands(); ++j)
            if (auto *v = values.lookup(i->getOperand(j)))
                copy->setOperand(j, v);
        copy->setDebugLoc({});
        copy->dropUnknownNonDebugMetadata();
        b.Insert(copy);
        values[i] = copy;
    }
    return values.lookup(p.root);
}

// Для состояния заполняем строку целиком. Тогда на горячем пути хватит одной проверки
// номера состояния. Лимит 128 КиБ ограничивает рост таблицы в скомпилированной программе.
SmallVector<uint32_t> computeDefaultRows(const DefaultSearch &search, unsigned stride,
                                         unsigned bits) {
    unsigned limit = std::min(search.states, (128u * 1024) / (stride * (bits / 8)));
    SmallVector<uint32_t> rows(stride, 0); // Состояние 0 всегда обрабатывает исходный поиск.
    for (unsigned state = 1; state < limit; ++state) {
        SmallVector<uint32_t> row(stride, 0);
        for (unsigned symbol : search.alphabet) {
            auto next = evaluateDefaultSearch(search, state, symbol);
            if (!next)
                return rows;
            row[symbol] = *next;
        }
        rows.append(row);
    }
    return rows;
}

// Переносим исходные инструкции в холодную функцию, принимающую состояние и класс символа.
// Клонируем также переходы между блоками, чтобы сохранить весь порядок поиска.
Function *outlineDefaultSearch(const DefaultSearch &search) {
    Function &f = *search.header->getParent();
    auto &ctx = f.getContext();
    auto *ty = search.result->getType();
    auto *fnTy =
        FunctionType::get(ty, {search.stateIn->getType(), search.symbolIn->getType()}, false);
    auto *slow = createColdFallback(f, fnTy);
    auto *start = BasicBlock::Create(ctx, "entry", slow);
    ValueToValueMapTy map;
    map[search.stateIn] = slow->getArg(0);
    map[search.symbolIn] = slow->getArg(1);
    map[search.entry] = start;
    for (auto *bb : search.blocks)
        map[bb] = BasicBlock::Create(ctx, "", slow);
    IRBuilder<>(start).CreateBr(cast<BasicBlock>(map[search.header]));
    for (auto *i : search.code) {
        auto *copy = i->clone();
        copy->setDebugLoc({});
        copy->dropUnknownNonDebugMetadata();
        copy->insertInto(cast<BasicBlock>(map[i->getParent()]),
                         cast<BasicBlock>(map[i->getParent()])->end());
        map[i] = copy;
    }
    for (auto *i : search.code)
        RemapInstruction(cast<Instruction>(map[i]), map, RF_NoModuleLevelChanges);
    IRBuilder<>(cast<BasicBlock>(map[search.exit])).CreateRet(map[search.result]);
    return slow;
}

// Если результат следующей развилки тоже известен, записываем его в свободный бит ячейки.
// Сначала проверяем всю таблицу, чтобы при отказе в ней остались обычные номера переходов.
bool encodeBranchTest(const DefaultSearch &search, const BranchTest &test,
                      MutableArrayRef<uint32_t> rows, uint64_t tag) {
    if (!test.root || tag >= (uint64_t{1} << search.result->getType()->getIntegerBitWidth()))
        return false;
    DenseMap<unsigned, bool> flags;
    for (auto next : rows) {
        if (flags.contains(next))
            continue;
        auto flag = evaluateBranchTest(search, test, next);
        if (!flag)
            return false;
        flags[next] = *flag;
    }
    for (auto &next : rows)
        if (flags.lookup(next))
            next |= tag;
    return true;
}

// Теперь можно заменить поиск. Внутри известных строк читаем fast[state * stride + symbol],
// за их пределами вызываем холодную функцию. Обе ветви передают результат в прежнее место.
bool replaceDefaultSearch(DefaultSearch &search) {
    unsigned stride =
        PowerOf2Ceil(*std::max_element(search.alphabet.begin(), search.alphabet.end()) + 1);
    unsigned bits = search.result->getType()->getIntegerBitWidth();
    if (bits != 8 && bits != 16 && bits != 32)
        return false;
    auto rows = computeDefaultRows(search, stride, bits);
    unsigned nstates = rows.size() / stride;
    if (nstates <= 4)
        return false;
    Function &f = *search.header->getParent();
    Module &m = *f.getParent();
    auto &ctx = m.getContext();
    auto *ty = search.result->getType();
    BranchTest pred = findBranchTest(search);
    uint64_t tag = PowerOf2Ceil(uint64_t(*std::max_element(rows.begin(), rows.end())) + 1);
    if (!encodeBranchTest(search, pred, rows, tag))
        pred = {};
    SmallVector<Constant *> data;
    for (auto n : rows)
        data.push_back(ConstantInt::get(ty, n));
    auto *arr = ArrayType::get(ty, data.size());
    auto *table = createTable(m, ConstantArray::get(arr, data), "transition.fast");
    markStateTable(table, nstates);
    table->setAlignment(Align(64));
    auto *slow = outlineDefaultSearch(search);

    auto *join = search.exit->splitBasicBlock(search.result->getNextNode()->getIterator(),
                                              "transition.join");
    auto *fast = BasicBlock::Create(ctx, "transition.lookup", &f, join);
    auto *fallback = BasicBlock::Create(ctx, "transition.fallback", &f, join);
    IRBuilder<> b(search.entry->getTerminator());
    Value *state = b.CreateFreeze(search.stateIn);
    Value *symbol = b.CreateFreeze(search.symbolIn);
    Value *inRange = b.CreateICmpULT(b.CreateSub(state, ConstantInt::get(state->getType(), 1)),
                                     ConstantInt::get(state->getType(), nstates - 1));
    // Вес 2000 к 1 подсказывает LLVM, какую ветвь разместить на основном пути.
    // При любом весе границы таблицы проверяются во время выполнения.
    b.CreateCondBr(inRange, fast, fallback, MDBuilder(ctx).createBranchWeights(2000, 1));
    search.header->removePredecessor(search.entry);
    search.entry->getTerminator()->eraseFromParent();
    b.SetInsertPoint(fast);
    Value *idx =
        b.CreateAdd(b.CreateMul(b.CreateZExtOrTrunc(state, b.getInt64Ty()), b.getInt64(stride)),
                    b.CreateZExtOrTrunc(symbol, b.getInt64Ty()));
    Value *quick = b.CreateLoad(ty, b.CreateInBoundsGEP(arr, table, {b.getInt64(0), idx}));
    Value *fastFlag = nullptr;
    if (pred.root) {
        fastFlag =
            b.CreateICmpNE(b.CreateAnd(quick, ConstantInt::get(ty, tag)), ConstantInt::get(ty, 0));
        quick = b.CreateAnd(quick, ConstantInt::get(ty, tag - 1));
    }
    b.CreateBr(join);
    b.SetInsertPoint(fallback);
    auto *call = b.CreateCall(slow, {state, symbol});
    Value *fallbackFlag = pred.root ? emitOriginalBranchTest(b, search, pred, call) : nullptr;
    b.CreateBr(join);
    b.SetInsertPoint(&*join->begin());
    auto *result = b.CreatePHI(ty, 3);
    search.result->replaceAllUsesWith(result);
    result->addIncoming(search.result, search.exit);
    result->addIncoming(quick, fast);
    result->addIncoming(call, fallback);
    if (pred.root) {
        auto *flag = b.CreatePHI(b.getInt1Ty(), 3);
        pred.root->replaceAllUsesWith(flag);
        flag->addIncoming(PoisonValue::get(b.getInt1Ty()), search.exit);
        flag->addIncoming(fastFlag, fast);
        flag->addIncoming(fallbackFlag, fallback);
    }
    EliminateUnreachableBlocks(f);
    errs() << "transition-split\t" << f.getName() << "\t" << (nstates - 1) << "\t" << stride << "\t"
           << unsigned(bool(pred.root)) << "\n";
    return true;
}

// После замены структура функции меняется. Заново находим циклы перед поиском следующего участка.
struct DefaultTransitions : PassInfoMixin<DefaultTransitions> {
    PreservedAnalyses run(Module &m, ModuleAnalysisManager &) {
        SmallVector<Function *> originals;
        for (auto &f : m)
            if (eligible(f))
                originals.push_back(&f);
        bool changed = false;
        for (auto *f : originals) {
            bool again;
            do {
                again = false;
                DominatorTree dt(*f);
                LoopInfo li(dt);
                for (auto *loop : li.getLoopsInPreorder()) {
                    auto search = findDefaultSearch(*loop);
                    if (search && replaceDefaultSearch(*search)) {
                        again = changed = true;
                        break;
                    }
                }
            } while (again);
        }
        return changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
    }
};

// У автомата с диапазонами результат поиска может выбирать и состояние, и действие лексера.
// Чтобы обойти выбор действия на обычном байте, придётся восстановить всё, что ждёт продолжение.
struct ActionPlan {
    SmallVector<BasicBlock *> blocks;
    SmallVector<Instruction *> outputs;
    SmallVector<Value *> fixed;
    SmallVector<unsigned> widths, offsets;
    BasicBlock *ordinary = nullptr;
    Instruction *root = nullptr;
    DenseMap<uint16_t, uint16_t> cached;
};

// Значение, которое читает код за пределами выбора действия, должно прийти и по новому пути.
bool usedOutsideAction(Use &use, const ActionPlan &plan) {
    auto *i = dyn_cast<Instruction>(use.getUser());
    if (!i || !llvm::is_contained(plan.blocks, i->getParent()))
        return true;
    auto *phi = dyn_cast<PHINode>(i);
    return phi && !llvm::is_contained(plan.blocks, phi->getIncomingBlock(use));
}

// От результата перехода идём до первой развилки через чистые вычисления и чтения констант.
// Запоминаем значения, которые используются после неё. При вызове или записи сокращать нечего.
bool findActionBranch(PHINode *result, ActionPlan &plan) {
    BasicBlock *bb = result->getParent();
    SmallPtrSet<Instruction *, 32> inside;
    for (unsigned j = 0; j != 4; ++j) {
        if (llvm::is_contained(plan.blocks, bb) || bb->isEHPad())
            return false;
        plan.blocks.push_back(bb);
        for (auto &i : *bb) {
            if (i.isTerminator())
                break;
            if (auto *ld = dyn_cast<LoadInst>(&i)) {
                if (!ld->isSimple() || !immutable(ld->getPointerOperand()))
                    return false;
            } else if (!isa<PHINode, ICmpInst, BinaryOperator, GetElementPtrInst, TruncInst,
                            SExtInst, ZExtInst, SelectInst>(i))
                return false;
            inside.insert(&i);
        }
        if (isa<CondBrInst>(bb->getTerminator()))
            break;
        if (!isa<UncondBrInst>(bb->getTerminator()))
            return false;
        bb = bb->getTerminator()->getSuccessor(0);
    }
    if (!isa<CondBrInst>(plan.blocks.back()->getTerminator()))
        return false;
    plan.root = result;
    for (auto *b : plan.blocks)
        for (auto &i : *b)
            if (inside.contains(&i) &&
                llvm::any_of(i.uses(), [&](Use &u) { return usedOutsideAction(u, plan); }))
                plan.outputs.push_back(&i);
    return !plan.outputs.empty() && plan.outputs.size() <= 16;
}

struct EventRow {
    BasicBlock *next = nullptr;
    SmallVector<Value *> values;
};

// Для одного номера перехода вычисляем выбранную ветвь и значения, которые она получает.
EventRow evaluateAction(const ActionPlan &plan, unsigned result, const DataLayout &dl) {
    DenseMap<Value *, Value *> values;
    auto get = [&](Value *v) -> Value * {
        auto it = values.find(v);
        return it == values.end() ? v : it->second;
    };
    BasicBlock *prev = nullptr;
    for (auto *bb : plan.blocks) {
        for (auto &i : *bb) {
            if (i.isTerminator())
                break;
            if (&i == plan.root) {
                values[&i] = ConstantInt::get(i.getType(), result);
                continue;
            }
            if (auto *phi = dyn_cast<PHINode>(&i)) {
                if (!prev)
                    return {};
                values[&i] = get(phi->getIncomingValueForBlock(prev));
                continue;
            }
            SmallVector<Constant *, 4> args;
            for (auto &op : i.operands()) {
                auto *c = dyn_cast<Constant>(get(op.get()));
                if (!c || isa<UndefValue>(c))
                    return {};
                args.push_back(c);
            }
            Constant *value = isa<LoadInst>(i)
                                  ? ConstantFoldLoadFromConstPtr(args[0], i.getType(), dl)
                                  : ConstantFoldInstOperands(&i, args, dl, nullptr, false);
            if (!value || isa<UndefValue>(value))
                return {};
            values[&i] = value;
        }
        prev = bb;
    }
    auto *br = cast<CondBrInst>(prev->getTerminator());
    auto *flag = dyn_cast<ConstantInt>(get(br->getCondition()));
    if (!flag)
        return {};
    EventRow row;
    row.next = br->getSuccessor(flag->isZero());
    for (auto *v : plan.outputs)
        row.values.push_back(get(v));
    return row;
}

// Считаем, к какой ветви ведёт больше известных ячеек таблицы. Это предположение о частом пути,
// без профиля входов. Готовим сокращение лишь тогда, когда необходимые значения можно восстановить.
bool planActions(Instruction *cut, PHINode *result, ArrayRef<BasicBlock *> region,
                 ArrayRef<uint16_t> cells, ActionPlan &plan) {
    if (!findActionBranch(result, plan))
        return false;
    constexpr unsigned eventTag = 0x8000, miss = 0xffff;
    DenseMap<unsigned, EventRow> decoded;
    DenseMap<BasicBlock *, unsigned> counts;
    for (auto n : cells) {
        if (n == miss)
            continue;
        if (n >= eventTag - 1)
            return false;
        auto it = decoded.find(n);
        if (it == decoded.end())
            it = decoded.try_emplace(n, evaluateAction(plan, n, cut->getDataLayout())).first;
        ++counts[it->second.next];
    }
    auto *br = cast<CondBrInst>(plan.blocks.back()->getTerminator());
    plan.ordinary = counts[br->getSuccessor(0)] >= counts[br->getSuccessor(1)]
                        ? br->getSuccessor(0)
                        : br->getSuccessor(1);
    if (br->getSuccessor(0) == br->getSuccessor(1) ||
        llvm::is_contained(plan.blocks, plan.ordinary) || plan.ordinary->isEHPad() ||
        counts[plan.ordinary] == 0)
        return false;
    DominatorTree dt(*cut->getFunction());
    if (llvm::is_contained(region, plan.ordinary))
        return false;
    for (auto &phi : plan.ordinary->phis()) {
        auto *v = phi.getIncomingValueForBlock(plan.blocks.back());
        if (!dt.dominates(v, cut) && !llvm::is_contained(plan.outputs, v))
            return false;
    }
    unsigned width = 0;
    for (unsigned j = 0; j < plan.outputs.size(); ++j) {
        Value *first = nullptr;
        bool same = true;
        unsigned bits = 0;
        for (auto &entry : decoded) {
            if (entry.second.next != plan.ordinary)
                continue;
            Value *v = entry.second.values[j];
            if (!first)
                first = v;
            same &= v == first;
            auto *c = dyn_cast<ConstantInt>(v);
            bits = std::max(bits, c ? unsigned(c->getValue().getActiveBits()) : 64u);
        }
        if (same && (isa<Constant>(first) || dt.dominates(first, cut))) {
            plan.fixed.push_back(first);
            bits = 0;
        } else {
            if (bits > 15 || !plan.outputs[j]->getType()->isIntegerTy())
                return false;
            plan.fixed.push_back(nullptr);
        }
        plan.widths.push_back(bits);
        plan.offsets.push_back(width);
        width += bits;
        if (width > 15)
            return false;
    }
    for (auto &entry : decoded) {
        unsigned word = entry.first | eventTag;
        if (entry.second.next == plan.ordinary) {
            word = 0;
            for (unsigned j = 0; j < plan.outputs.size(); ++j)
                if (plan.widths[j])
                    word |= cast<ConstantInt>(entry.second.values[j])->getZExtValue()
                            << plan.offsets[j];
        }
        plan.cached[entry.first] = word;
    }
    return true;
}

// Для обычной ветви извлекаем готовые значения из ячейки. На остальных ветвях лексер
// по-прежнему сам выбирает и вызывает обработчик, в том числе при завершении ввода.
void emitActionShortcut(const ActionPlan &plan, PHINode *result, BasicBlock *fast, Value *cell) {
    auto &f = *fast->getParent();
    auto &ctx = f.getContext();
    auto *normal = BasicBlock::Create(ctx, "transition.ordinary", &f, result->getParent());
    auto *event = BasicBlock::Create(ctx, "transition.action", &f, result->getParent());
    IRBuilder<> b(fast);
    b.CreateCondBr(b.CreateICmpSLT(cell, b.getInt16(0)), event, normal);
    b.SetInsertPoint(event);
    result->addIncoming(
        b.CreateZExtOrTrunc(b.CreateAnd(cell, b.getInt16(0x7fff)), result->getType()), event);
    b.CreateBr(result->getParent());
    b.SetInsertPoint(normal);
    DenseMap<Value *, Value *> values;
    for (unsigned j = 0; j < plan.outputs.size(); ++j) {
        Value *v = plan.fixed[j];
        if (!v) {
            v = b.CreateLShr(cell, b.getInt16(plan.offsets[j]));
            v = b.CreateAnd(v, b.getInt16((1u << plan.widths[j]) - 1));
            v = b.CreateZExtOrTrunc(v, plan.outputs[j]->getType());
        }
        values[plan.outputs[j]] = v;
    }
    // Запоминаем потребителей до добавления нового пути, чтобы перепривязать только исходные чтения.
    SmallVector<SmallVector<std::pair<User *, unsigned>>> external(plan.outputs.size());
    for (unsigned j = 0; j < plan.outputs.size(); ++j)
        for (auto &use : plan.outputs[j]->uses()) {
            if (usedOutsideAction(use, plan))
                external[j].emplace_back(use.getUser(), use.getOperandNo());
        }
    for (auto &phi : plan.ordinary->phis()) {
        auto *v = phi.getIncomingValueForBlock(plan.blocks.back());
        phi.addIncoming(values.contains(v) ? values.lookup(v) : v, normal);
    }
    b.CreateBr(plan.ordinary);
    for (unsigned j = 0; j < plan.outputs.size(); ++j) {
        auto *old = plan.outputs[j];
        SSAUpdater resolver;
        resolver.Initialize(old->getType(), "transition.merge");
        resolver.AddAvailableValue(old->getParent(), old);
        resolver.AddAvailableValue(normal, values.lookup(old));
        for (auto [user, n] : external[j])
            resolver.RewriteUse(user->getOperandUse(n));
    }
}

// Вторая форма поиска встречается в автоматах, хранящих диапазоны символов.
// Индекс часто расширяют перед адресной арифметикой. Снимаем расширение, чтобы узнать его источник.
Value *stripExtensions(Value *v) {
    while (isa<ZExtInst, SExtInst>(v))
        v = cast<Instruction>(v)->getOperand(0);
    return v;
}

// Проверяем, что результат поиска используется как индекс другой постоянной таблицы.
// Так находим границу между выбором перехода и его применением.
bool indexesConstantTable(PHINode &phi) {
    SmallVector<Value *> todo{&phi};
    for (unsigned j = 0; j < todo.size() && j < 20; ++j)
        for (User *u : todo[j]->users()) {
            if (auto *g = dyn_cast<GetElementPtrInst>(u); g && immutable(g->getPointerOperand()))
                return true;
            if (isa<SExtInst, ZExtInst, TruncInst>(u))
                todo.push_back(u);
        }
    return false;
}

struct SparseSearch {
    Instruction *cut;
    PHINode *result;
    Value *state;
    unsigned states;
    SmallVector<BasicBlock *> region;
    SmallPtrSet<Instruction *, 32> code;
    Value *stateAddress = nullptr;
};

// От обращения к таблице по номеру состояния прослеживаем все пути к общему результату.
// Допускаем внутренний цикл поиска по диапазонам. Вызов, запись или второй используемый
// снаружи результат означают, что вынести этот участок по нашей модели нельзя.
std::optional<SparseSearch> findSparseSearch(Instruction *cut, PHINode *phi, Value *state,
                                             unsigned n, DominatorTree &dt) {
    SparseSearch search{cut, phi, state, n, {}, {}};
    auto *begin = cut->getParent(), *end = phi->getParent();
    if (auto *load = dyn_cast<LoadInst>(state);
        load && load->isSimple() && load->getParent() == begin && load->comesBefore(cut)) {
        bool available = true;
        for (auto *i = load->getNextNode(); i != cut; i = i->getNextNode())
            if (i->mayWriteToMemory() || i->isAtomic() || isa<FenceInst>(i)) {
                available = false;
                break;
            }
        if (available)
            search.stateAddress = load->getPointerOperand();
    }
    search.region.push_back(begin);
    SmallPtrSet<BasicBlock *, 32> seen{begin};
    bool nested = false;
    for (unsigned j = 0; j < search.region.size(); ++j) {
        auto *bb = search.region[j];
        if (bb->hasAddressTaken() || bb->isEHPad() || search.region.size() > 48)
            return {};
        for (Instruction *i = bb == begin ? cut : &bb->front(); i; i = i->getNextNode()) {
            if (!isa<LoadInst, BinaryOperator, SelectInst, PHINode, GetElementPtrInst, ICmpInst,
                     PtrToIntInst, SExtInst, ZExtInst, TruncInst, CondBrInst, UncondBrInst,
                     SwitchInst>(i))
                return {};
            if (auto *ld = dyn_cast<LoadInst>(i); ld && !ld->isSimple())
                return {};
            if (i->getType()->isVectorTy())
                return {};
            search.code.insert(i);
        }
        for (auto *succ : successors(bb)) {
            if (succ == end || (succ->size() == 1 && isa<UnreachableInst>(succ->getTerminator())))
                continue;
            if (succ == begin || !dt.dominates(begin, succ))
                return {};
            nested |= dt.dominates(succ, bb);
            if (seen.insert(succ).second)
                search.region.push_back(succ);
        }
    }
    if (!nested || search.code.size() > 320)
        return {};
    for (auto *bb : search.region) {
        if (bb != begin)
            for (auto *pred : predecessors(bb))
                if (!seen.contains(pred))
                    return {};
        for (Instruction *i = bb == begin ? cut : &bb->front(); i; i = i->getNextNode())
            for (User *u : i->users())
                if (u != phi &&
                    (!isa<Instruction>(u) || !search.code.contains(cast<Instruction>(u))))
                    return {};
    }
    return search;
}

struct SymbolInput {
    Value *value;
    bool isAddress;
};

// Символ может уже находиться в целом числе или читаться из входного буфера. Собираем
// кандидатов из сравнений и чтений байта. Их пригодность дальше проверяет сам поиск.
SmallVector<SymbolInput> findSymbolInputs(const SparseSearch &search, DominatorTree &dt) {
    SmallVector<SymbolInput> result;
    auto add = [&](Value *v, bool ptr) {
        if (!llvm::any_of(result,
                          [&](SymbolInput i) { return i.value == v && i.isAddress == ptr; }))
            result.push_back({v, ptr});
    };
    for (auto *bb : search.region)
        for (auto &i : *bb) {
            if (!search.code.contains(&i))
                continue;
            if (auto *ld = dyn_cast<LoadInst>(&i); ld && ld->getType()->isIntegerTy(8)) {
                auto *p = ld->getPointerOperand();
                if (!immutable(p) && dt.dominates(p, search.cut))
                    add(p, true);
            }
            if (auto *cmp = dyn_cast<ICmpInst>(&i))
                for (Value *op : cmp->operands()) {
                    auto *v = stripExtensions(op);
                    if (v != search.state && v->getType()->isIntegerTy() &&
                        v->getType()->getIntegerBitWidth() >= 8 &&
                        v->getType()->getIntegerBitWidth() <= 32 && !isa<Constant>(v) &&
                        dt.dominates(v, search.cut))
                        add(v, false);
                }
        }
    return result;
}

// Чтобы узнать ответ для конкретного байта, исполняем инструкции поиска во время компиляции.
// Без заданного байта тот же разбор показывает, дойдёт ли лексер до чтения входа в этом состоянии.
struct SparseInterpreter {
    const SparseSearch &site;
    SymbolInput input;
    const DataLayout &dl;
    unsigned state;
    std::optional<unsigned> symbol;
    bool reachedInputRead = false;
    DenseMap<Value *, Constant *> values;

    // Узнаём именно чтение байта по выбранному адресу. Чтения служебных таблиц имеют другую роль.
    bool readsInputByte(LoadInst *load) const {
        return site.code.contains(load) && input.isAddress && load->getType()->isIntegerTy(8) &&
               load->getPointerOperand() == input.value;
    }

    // Подставляем известное состояние и символ. Остальные чтения вычисляем только по неизменяемой памяти.
    Constant *evaluateInstruction(Instruction *i, unsigned depth) {
        if (auto *ld = dyn_cast<LoadInst>(i); ld && site.code.contains(ld) && site.stateAddress &&
                                              ld->getPointerOperand() == site.stateAddress &&
                                              ld->getType() == site.state->getType())
            return ConstantInt::get(ld->getType(), state);
        if (auto *ld = dyn_cast<LoadInst>(i); ld && readsInputByte(ld)) {
            reachedInputRead = true;
            return symbol ? ConstantInt::get(ld->getType(), *symbol) : nullptr;
        }
        if (!isa<LoadInst, BinaryOperator, SelectInst, GetElementPtrInst, ICmpInst, PtrToIntInst,
                 SExtInst, ZExtInst, TruncInst>(i))
            return nullptr;
        SmallVector<Constant *, 4> args;
        for (Value *op : i->operands()) {
            auto *c = value(op, depth + 1);
            if (!c || isa<UndefValue>(c))
                return nullptr;
            args.push_back(c);
        }
        if (auto *ld = dyn_cast<LoadInst>(i)) {
            if (!ld->isSimple() || !immutable(args[0]))
                return nullptr;
            return ConstantFoldLoadFromConstPtr(args[0], i->getType(), dl);
        }
        return ConstantFoldInstOperands(i, args, dl, nullptr, false);
    }

    // Берём уже вычисленный операнд или восстанавливаем его из инструкций перед входом в поиск.
    Constant *value(Value *v, unsigned depth = 0) {
        if (v == site.state)
            return ConstantInt::get(v->getType(), state);
        if (!input.isAddress && v == input.value)
            return symbol ? ConstantInt::get(v->getType(), *symbol) : nullptr;
        if (auto *c = dyn_cast<Constant>(v))
            return c;
        if (auto it = values.find(v); it != values.end())
            return it->second;
        auto *i = dyn_cast<Instruction>(v);
        if (!i || depth > 12 || site.code.contains(i))
            return nullptr;
        return evaluateInstruction(i, depth);
    }

    // Следуем по вычислимым ветвям до общего выхода. Если ветвь зависит от неизвестного значения,
    // ячейка остаётся пустой и исходный поиск сохранит её поведение.
    std::optional<uint16_t> run() {
        auto *bb = site.cut->getParent();
        Instruction *start = site.cut;
        for (unsigned step = 0; step < 140; ++step) {
            for (Instruction *i = start; !i->isTerminator(); i = i->getNextNode()) {
                if (isa<PHINode>(i))
                    continue;
                if (auto *ld = dyn_cast<LoadInst>(i); ld && readsInputByte(ld) && !symbol) {
                    reachedInputRead = true;
                    return {};
                }
                values[i] = evaluateInstruction(i, 0);
            }
            auto *term = bb->getTerminator();
            BasicBlock *next = nullptr;
            if (auto *br = dyn_cast<CondBrInst>(term)) {
                auto *c = dyn_cast_or_null<ConstantInt>(value(br->getCondition()));
                if (!c)
                    return {};
                next = br->getSuccessor(c->isZero());
            } else if (auto *sw = dyn_cast<SwitchInst>(term)) {
                auto *c = dyn_cast_or_null<ConstantInt>(value(sw->getCondition()));
                if (!c)
                    return {};
                next = sw->findCaseValue(c)->getCaseSuccessor();
            } else
                next = term->getSuccessor(0);
            if (next == site.result->getParent()) {
                auto *n =
                    dyn_cast_or_null<ConstantInt>(value(site.result->getIncomingValueForBlock(bb)));
                if (!n || n->getValue().uge(65535))
                    return {};
                return n->getZExtValue();
            }
            if (isa<UnreachableInst>(next->getTerminator()))
                return {};
            SmallVector<std::pair<PHINode *, Constant *>> incoming;
            for (auto &phi : next->phis())
                incoming.emplace_back(&phi, value(phi.getIncomingValueForBlock(bb)));
            for (auto [phi, c] : incoming)
                values[phi] = c;
            bb = next;
            start = &*bb->getFirstNonPHIIt();
        }
        return {};
    }
};

// У поиска по диапазонам может быть несколько входных значений. Передаём их холодной
// функции и возвращаем единственный результат, индекс выбранного перехода.
Function *outlineSparseSearch(const SparseSearch &search, SmallVectorImpl<Value *> &args) {
    Module &m = *search.cut->getModule();
    Function &caller = *search.cut->getFunction();
    auto &ctx = m.getContext();
    SmallPtrSet<Value *, 16> seen;
    SmallVector<Type *> types;
    for (auto *bb : search.region)
        for (Instruction &i : *bb) {
            for (Value *v : i.operands()) {
                if (isa<Constant, BasicBlock>(v) ||
                    (isa<Instruction>(v) && search.code.contains(cast<Instruction>(v))))
                    continue;
                if (seen.insert(v).second) {
                    args.push_back(v);
                    types.push_back(v->getType());
                }
            }
        }
    for (auto *bb : search.region) {
        int i = search.result->getBasicBlockIndex(bb);
        if (i < 0)
            continue;
        Value *v = search.result->getIncomingValue(i);
        if (!isa<Constant>(v) &&
            !(isa<Instruction>(v) && search.code.contains(cast<Instruction>(v))) &&
            seen.insert(v).second) {
            args.push_back(v);
            types.push_back(v->getType());
        }
    }
    auto *type = FunctionType::get(search.result->getType(), types, false);
    auto *f = createColdFallback(caller, type);
    ValueToValueMapTy map;
    for (unsigned i = 0; i < args.size(); ++i)
        map[args[i]] = f->getArg(i);
    for (auto *bb : search.region)
        map[bb] = BasicBlock::Create(ctx, "", f);
    auto *exit = BasicBlock::Create(ctx, "exit", f);
    map[search.result->getParent()] = exit;
    auto *out = PHINode::Create(search.result->getType(), search.result->getNumIncomingValues(),
                                "next", exit);
    for (auto *bb : search.region) {
        for (auto *next : successors(bb))
            if (!map.count(next)) {
                assert(isa<UnreachableInst>(next->getTerminator()));
                auto *sink = BasicBlock::Create(ctx, "unreachable", f);
                new UnreachableInst(ctx, sink);
                map[next] = sink;
            }
        auto *copyBB = cast<BasicBlock>(map[bb]);
        for (Instruction &i : *bb) {
            auto *copy = i.clone();
            copy->setDebugLoc({});
            copy->dropUnknownNonDebugMetadata();
            copy->insertInto(copyBB, copyBB->end());
            map[&i] = copy;
        }
    }
    for (auto *bb : search.region) {
        for (Instruction &i : *bb)
            RemapInstruction(cast<Instruction>(map[&i]), map, RF_NoModuleLevelChanges);
        for (auto *next : successors(bb))
            if (next == search.result->getParent()) {
                auto *v = search.result->getIncomingValueForBlock(bb);
                assert(isa<Constant>(v) || map.count(v));
                out->addIncoming(isa<Constant>(v) ? v : static_cast<Value *>(map[v]),
                                 cast<BasicBlock>(map[bb]));
            }
    }
    ReturnInst::Create(ctx, out, exit);
    return f;
}

constexpr unsigned SparseWidth = 128;
constexpr uint16_t MissingTransition = 0xffff;

struct SparseTable {
    unsigned rows;
    SmallVector<uint16_t> cells;
    SmallVector<uint8_t> readable;
    unsigned filled = 0;
    unsigned variedRows = 0;
};

// На каждое состояние пробуем байты 0…127. Перед чтением через указатель отдельно проверяем,
// что прежний поиск сам дошёл бы до этого чтения. Иначе даже обращение к первому байту могло бы быть лишним.
std::optional<SparseTable> computeSparseRows(const SparseSearch &search, SymbolInput input) {
    unsigned nrows = std::min(search.states, 512u);
    SparseTable table{nrows, SmallVector<uint16_t>(nrows * SparseWidth, MissingTransition),
                      SmallVector<uint8_t>(nrows, 1)};
    const auto &dl = search.cut->getDataLayout();
    for (unsigned st = 0; st < nrows; ++st) {
        if (input.isAddress) {
            SparseInterpreter prefix{
                search, input, dl, st, {}, false, DenseMap<Value *, Constant *>()};
            prefix.run();
            if (!prefix.reachedInputRead) {
                table.readable[st] = 0;
                continue;
            }
        }
        uint16_t first = MissingTransition;
        bool changed = false;
        for (unsigned c = 0; c < SparseWidth; ++c) {
            SparseInterpreter ev{search, input, dl, st, c, false, DenseMap<Value *, Constant *>()};
            if (auto v = ev.run()) {
                table.cells[st * SparseWidth + c] = *v;
                ++table.filled;
                if (first == MissingTransition)
                    first = *v;
                changed |= *v != first;
            }
        }
        table.variedRows += changed;
    }
    if (table.variedRows < 2 || table.filled < SparseWidth * 2)
        return {};
    return table;
}

// Выпускаем проверки номера состояния, доступности байта и наличия ответа. Лишь затем
// передаём готовый переход основному циклу. Любой промах уходит в копию исходного поиска.
bool replaceSparseSearch(SparseSearch &search, SymbolInput input, bool splitActions) {
    auto proven = computeSparseRows(search, input);
    if (!proven)
        return false;
    auto &cells = proven->cells;
    const auto &canReadInput = proven->readable;
    const unsigned nrows = proven->rows;
    ActionPlan plan;
    const bool ordinary =
        splitActions && planActions(search.cut, search.result, search.region, cells, plan);
    if (ordinary) {
        for (auto &cell : cells)
            if (cell != MissingTransition)
                cell = plan.cached.lookup(cell);
        errs() << "event-transition\t" << search.cut->getFunction()->getName() << "\t"
               << plan.outputs.size() << "\n";
    }
    auto &f = *search.cut->getFunction();
    auto &m = *f.getParent();
    auto &ctx = m.getContext();
    auto *trans = createTable(m, ConstantDataArray::get(ctx, cells), "sparse.transition");
    markStateTable(trans, nrows);
    auto *head = search.cut->getParent();
    auto *slow = head->splitBasicBlock(search.cut->getIterator(), "transition.search");
    search.region[0] = slow;
    auto *index = BasicBlock::Create(ctx, "transition.index", &f, slow);
    auto *look = BasicBlock::Create(ctx, "transition.lookup", &f, slow);
    auto *fast = BasicBlock::Create(ctx, "transition.hit", &f, slow);
    IRBuilder<> b(head->getTerminator());
    Value *state = b.CreateZExtOrTrunc(b.CreateFreeze(search.state), b.getInt64Ty());
    auto weights = MDBuilder(ctx).createBranchWeights(1000, 1);
    b.CreateCondBr(b.CreateICmpULT(state, b.getInt64(nrows)), index, slow, weights);
    head->getTerminator()->eraseFromParent();
    b.SetInsertPoint(index);
    Value *symbol = input.value;
    if (input.isAddress) {
        if (!llvm::all_of(canReadInput, [](uint8_t v) { return v != 0; })) {
            auto *valid =
                createTable(m, ConstantDataArray::get(ctx, canReadInput), "sparse.readable");
            auto *canRead =
                b.CreateLoad(b.getInt8Ty(), b.CreateInBoundsGEP(valid->getValueType(), valid,
                                                                {b.getInt64(0), state}));
            auto *read = BasicBlock::Create(ctx, "transition.read", &f, slow);
            b.CreateCondBr(b.CreateIsNotNull(canRead), read, slow, weights);
            b.SetInsertPoint(read);
        }
        symbol = b.CreateAlignedLoad(b.getInt8Ty(), input.value, Align(1));
    }
    symbol = b.CreateZExtOrTrunc(b.CreateFreeze(symbol), b.getInt64Ty());
    b.CreateCondBr(b.CreateICmpULT(symbol, b.getInt64(SparseWidth)), look, slow, weights);
    b.SetInsertPoint(look);
    auto *off = b.CreateAdd(b.CreateMul(state, b.getInt64(SparseWidth)), symbol);
    auto *cell = b.CreateLoad(
        b.getInt16Ty(), b.CreateInBoundsGEP(trans->getValueType(), trans, {b.getInt64(0), off}));
    b.CreateCondBr(b.CreateICmpNE(cell, b.getInt16(MissingTransition)), fast, slow, weights);
    if (!ordinary) {
        b.SetInsertPoint(fast);
        search.result->addIncoming(b.CreateZExtOrTrunc(cell, search.result->getType()), fast);
        b.CreateBr(search.result->getParent());
    }
    SmallVector<Value *> args;
    auto *fn = outlineSparseSearch(search, args);
    auto *fall = BasicBlock::Create(ctx, "transition.fallback", &f, slow);
    IRBuilder<> cb(fall);
    auto *call = cb.CreateCall(fn, args);
    call->setCallingConv(fn->getCallingConv());
    search.result->addIncoming(call, fall);
    cb.CreateBr(search.result->getParent());
    SmallVector<BasicBlock *> preds(predecessors(slow));
    for (auto *pred : preds)
        pred->getTerminator()->replaceSuccessorWith(slow, fall);
    if (ordinary)
        emitActionShortcut(plan, search.result, fast, cell);
    EliminateUnreachableBlocks(f);
    errs() << "sparse-split\t" << f.getName() << "\t" << nrows << "\t" << proven->filled << "\t"
           << proven->variedRows << "\n";
    return true;
}

// Поиск начинается с обращения к таблице по номеру состояния и заканчивается общим индексом
// перехода. Для каждого такого участка пробуем построить достаточно заполненную таблицу.
bool findAndReplaceSparseSearch(Function &f, bool actions) {
    DominatorTree dt(f);
    SmallVector<BasicBlock *> joins;
    for (auto &end : f)
        if (std::distance(end.phis().begin(), end.phis().end()) == 1 &&
            end.phis().begin()->getType()->isIntegerTy() &&
            end.phis().begin()->getType()->getIntegerBitWidth() >= 8 &&
            indexesConstantTable(*end.phis().begin()))
            joins.push_back(&end);
    for (auto &bb : f) {
        for (auto &i : bb) {
            auto *g = dyn_cast<GetElementPtrInst>(&i);
            if (!g || g->getNumIndices() != 2 || !isa<GlobalVariable>(g->getPointerOperand()))
                continue;
            auto *tab = immutable(g->getPointerOperand());
            if (!tab || !isa<ArrayType>(tab->getValueType()) ||
                g->getSourceElementType() != tab->getValueType())
                continue;
            auto *state = stripExtensions(g->getOperand(2));
            if (isa<Constant>(state) || !state->getType()->isIntegerTy() ||
                state->getType()->getIntegerBitWidth() < 16 ||
                state->getType()->getIntegerBitWidth() > 64)
                continue;
            unsigned states = cast<ArrayType>(tab->getValueType())->getNumElements();
            if (states < 4 || states > 8192)
                continue;
            for (auto *end : joins) {
                if (end == &bb)
                    continue;
                auto search = findSparseSearch(g, &*end->phis().begin(), state, states, dt);
                if (!search)
                    continue;
                for (auto in : findSymbolInputs(*search, dt))
                    if (replaceSparseSearch(*search, in, actions))
                        return true;
            }
        }
    }
    return false;
}

// Обходим исходные функции модуля. Вынесенные в процессе обхода функции уже содержат резервный путь.
struct SparseTransition : PassInfoMixin<SparseTransition> {
    explicit SparseTransition(bool actions) : actions(actions) {}

    bool actions;

    PreservedAnalyses run(Module &m, ModuleAnalysisManager &) {
        SmallVector<Function *> functions;
        for (auto &f : m)
            if (eligible(f))
                functions.push_back(&f);
        bool changed = false;
        for (auto *f : functions)
            changed |= findAndReplaceSparseSearch(*f, actions);
        return changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
    }
};

// После построения таблиц остаётся внешний цикл, который читает вход. В нём можно ускорить
// целую серию байтов, если каждый байт одинаково меняет наблюдаемое состояние лексера.
struct ByteScan {
    Loop &loop;
    PHINode *cursor, *state;
    LoadInst *byte;
    GetElementPtrInst *advance;
    Value *end;
    unsigned states;
};

// Ищем чтение одного байта по указателю, который сам цикл передаёт из итерации в итерацию.
LoadInst *findCursorRead(Loop &loop) {
    auto *header = loop.getHeader();
    for (auto *block : loop.blocks()) {
        for (auto &instruction : *block) {
            auto *load = dyn_cast<LoadInst>(&instruction);
            if (!load || !load->isSimple() || !load->getType()->isIntegerTy(8))
                continue;
            auto *cursor = dyn_cast<PHINode>(load->getPointerOperand());
            if (cursor && cursor->getParent() == header)
                return load;
        }
    }
    return nullptr;
}

// Прослеживаем, участвует ли число в вычислении адреса в нашей таблице переходов.
// Метка с числом строк отличает номер состояния от счётчика позиции или длины.
unsigned indexedStateCount(PHINode &candidate, Loop &loop) {
    SmallVector<Value *> work{&candidate};
    SmallPtrSet<Value *, 32> seen{&candidate};
    unsigned states = 0;
    for (unsigned i = 0; i < work.size() && work.size() < 96; ++i)
        for (auto *u : work[i]->users()) {
            auto *inst = dyn_cast<Instruction>(u);
            if (!inst || !loop.contains(inst))
                continue;
            if (auto *g = dyn_cast<GetElementPtrInst>(inst)) {
                auto *base = dyn_cast<GlobalVariable>(getUnderlyingObject(g->getPointerOperand()));
                if (auto *md = base ? base->getMetadata(StateTableTag) : nullptr;
                    md && md->getNumOperands() == 1) {
                    auto *rows = mdconst::dyn_extract<ConstantInt>(md->getOperand(0));
                    if (rows && !rows->isZero() && rows->getValue().ule(512)) {
                        states = rows->getZExtValue();
                    }
                }
            } else if (isa<CastInst, BinaryOperator, FreezeInst>(inst) && seen.insert(inst).second)
                work.push_back(inst);
        }
    return states;
}

// Связываем чтение, номер состояния и сдвиг указателя ровно на один байт. Если исходный
// цикл сравнивает следующую позицию с концом буфера, сохраняем эту границу для быстрого пути.
std::optional<ByteScan> findByteScan(Loop &loop, const DominatorTree &dt) {
    if (loop.getNumBlocks() > 600)
        return {};
    auto *h = loop.getHeader();
    auto *byte = findCursorRead(loop);
    if (!byte)
        return {};
    auto *cur = cast<PHINode>(byte->getPointerOperand());
    PHINode *state = nullptr;
    unsigned states = 0;
    for (auto &candidate : h->phis()) {
        if (!candidate.getType()->isIntegerTy())
            continue;
        unsigned width = candidate.getType()->getIntegerBitWidth();
        if (width < 8 || width > 64)
            continue;
        if (unsigned count = indexedStateCount(candidate, loop)) {
            if (state)
                return {};
            state = &candidate;
            states = count;
        }
    }
    if (!state)
        return {};
    GetElementPtrInst *unbounded = nullptr;
    for (auto *u : cur->users()) {
        auto *advance = dyn_cast<GetElementPtrInst>(u);
        if (!advance || advance->getPointerOperand() != cur ||
            !advance->getSourceElementType()->isIntegerTy(8) || advance->getNumIndices() != 1 ||
            !isa<ConstantInt>(advance->getOperand(1)) ||
            !cast<ConstantInt>(advance->getOperand(1))->isOne())
            continue;
        unbounded = advance;
        for (auto *v : advance->users()) {
            auto *cmp = dyn_cast<ICmpInst>(v);
            if (!cmp || !cmp->isEquality())
                continue;
            auto *end = cmp->getOperand(0) == advance ? cmp->getOperand(1) : cmp->getOperand(0);
            if (!loop.isLoopInvariant(end) || !dt.dominates(end, h->getTerminator()))
                continue;
            return ByteScan{loop, cur, state, byte, advance, end, states};
        }
    }
    // Бывают лексеры со специальным байтом в конце буфера. Тогда остановку определяет
    // сам переход на этом байте. Ниже в маску попадают только байты, возвращающие цикл в начало.
    if (unbounded)
        return ByteScan{loop, cur, state, byte, unbounded, nullptr, states};
    return {};
}

struct Write {
    Value *address, *value;
    Align alignment;
    bool operator==(const Write &) const = default;
};

using Writes = SmallVector<Write, 2>;

// Запись можно повторить на каждой букве, если адрес не меняется, а значение доступно
// в быстром цикле. Запоминаем также выравнивание исходной записи.
bool recordStore(const ByteScan &scan, StoreInst &store, Value *value, Value *state,
                 Writes &writes) {
    if (!store.isSimple() || writes.size() >= 3 ||
        !scan.loop.isLoopInvariant(store.getPointerOperand()))
        return false;
    if (value == state)
        value = scan.state;
    if (value != scan.cursor && value != scan.state && !scan.loop.isLoopInvariant(value))
        return false;
    writes.push_back({store.getPointerOperand(), value, store.getAlign()});
    return true;
}

// Между чтением байта и выбором перехода лексер обычно считает индекс и читает таблицы.
// Подставляем в такую инструкцию значения, известные для выбранной пары «состояние, байт».
Constant *foldScanInstruction(Instruction &instruction, function_ref<Value *(Value *)> valueOf) {
    const auto &layout = instruction.getDataLayout();
    if (auto *load = dyn_cast<LoadInst>(&instruction)) {
        auto *address = dyn_cast<Constant>(valueOf(load->getPointerOperand()));
        if (!load->isSimple() || !address || !immutable(address))
            return nullptr;
        return ConstantFoldLoadFromConstPtr(address, load->getType(), layout);
    }
    if (!isa<GetElementPtrInst, BinaryOperator, CastInst, ICmpInst, SelectInst>(instruction))
        return nullptr;
    SmallVector<Constant *, 4> operands;
    for (Value *operand : instruction.operands()) {
        auto *constant = dyn_cast<Constant>(valueOf(operand));
        if (!constant || isa<UndefValue>(constant))
            return nullptr;
        operands.push_back(constant);
    }
    return ConstantFoldInstOperands(&instruction, operands, layout, nullptr, false);
}

// Здесь и определяется, можно ли пропустить полный разбор буквы в «abc;». Подставляем байт
// и номер состояния в одну исходную итерацию. На возврате в начало курсор должен сдвинуться
// на байт, а остальные значения цикла остаться прежними. Допустимые записи в память запоминаем
// по порядку, чтобы затем их повторить. Неизвестная ветвь или вызов исключает байт из серии.
bool simulateByteStep(const ByteScan &scan, unsigned state, unsigned byte, Writes &writes) {
    auto *h = scan.loop.getHeader();
    DenseMap<Value *, Value *> known;
    known[scan.state] = ConstantInt::get(scan.state->getType(), state);
    auto val = [&](Value *v) -> Value * {
        auto it = known.find(v);
        return it == known.end() ? v : it->second;
    };
    BasicBlock *bb = h, *prev = nullptr;
    SmallPtrSet<Value *, 4> boundTests;
    bool observedBound = false, read = false;
    for (unsigned step = 0; step < 180; ++step) {
        if (!scan.loop.contains(bb))
            return false;
        if (prev) {
            SmallVector<std::pair<PHINode *, Value *>> phis;
            for (auto &phi : bb->phis())
                phis.emplace_back(&phi, val(phi.getIncomingValueForBlock(prev)));
            for (auto [phi, v] : phis)
                known[phi] = v;
        }
        for (auto &i : *bb) {
            if (i.isTerminator())
                break;
            if (isa<PHINode>(i) || i.isDebugOrPseudoInst())
                continue;
            if (&i == scan.byte) {
                if (read)
                    return false;
                read = true;
                known[&i] = ConstantInt::get(i.getType(), byte);
                continue;
            }
            if (&i == scan.advance) {
                known[&i] = scan.advance;
                continue;
            }
            if (auto *fr = dyn_cast<FreezeInst>(&i)) {
                auto *x = val(fr->getOperand(0));
                if (!isa<ConstantInt>(x))
                    return false;
                known[&i] = x;
                continue;
            }
            if (auto *cmp = dyn_cast<ICmpInst>(&i); cmp && cmp->isEquality()) {
                auto *a = val(cmp->getOperand(0)), *b = val(cmp->getOperand(1));
                if ((a == scan.advance && b == scan.end) || (b == scan.advance && a == scan.end)) {
                    if (!read)
                        return false;
                    known[&i] =
                        ConstantInt::get(cmp->getType(), cmp->getPredicate() == ICmpInst::ICMP_NE);
                    boundTests.insert(&i);
                    continue;
                }
            }
            if (auto *store = dyn_cast<StoreInst>(&i)) {
                if (!read || !recordStore(scan, *store, val(store->getValueOperand()),
                                          known[scan.state], writes))
                    return false;
                continue;
            }
            auto *computed = foldScanInstruction(i, val);
            if (!computed || isa<UndefValue>(computed))
                return false;
            known[&i] = computed;
        }
        auto *term = bb->getTerminator();
        BasicBlock *next = nullptr;
        if (auto *br = dyn_cast<CondBrInst>(term)) {
            auto *c = dyn_cast<ConstantInt>(val(br->getCondition()));
            if (!c)
                return false;
            observedBound |= boundTests.contains(br->getCondition());
            next = br->getSuccessor(c->isZero());
        } else if (auto *br = dyn_cast<UncondBrInst>(term))
            next = br->getSuccessor(0);
        else if (auto *sw = dyn_cast<SwitchInst>(term)) {
            auto *c = dyn_cast<ConstantInt>(val(sw->getCondition()));
            if (!c)
                return false;
            next = sw->findCaseValue(c)->getCaseSuccessor();
        } else
            return false;
        if (next == h) {
            // Возврат в тот же блок ещё не означает возврат в то же состояние автомата.
            // Сверяем каждое значение, с которым начнётся следующая итерация.
            if ((scan.end && !observedBound) || !read)
                return false;
            for (auto &phi : h->phis()) {
                Value *expected = &phi == scan.cursor ? scan.advance : val(&phi);
                if (val(phi.getIncomingValueForBlock(bb)) != expected)
                    return false;
            }
            return true;
        }
        prev = bb;
        bb = next;
    }
    return false;
}

struct RunMasks {
    SmallVector<std::array<uint64_t, 2>> masks{{0, 0}};
    SmallVector<uint16_t> rows;
    Writes layout;
    unsigned eligible = 0;
};

// Для каждого состояния проверяем байты 0…127. Успешному байту ставим один бит в маске.
// В одной серии допускаем только совместимые записи в память. Это может быть, например,
// запоминание позиции последнего распознанного токена.
RunMasks computeRunMasks(const ByteScan &scan) {
    RunMasks table;
    DominatorTree dom(*scan.byte->getFunction());
    for (unsigned st = 0; st < scan.states; ++st) {
        std::array<uint64_t, 2> mask{0, 0};
        std::optional<bool> doesWrite;
        for (unsigned c = 0; c < 128; ++c) {
            Writes writes;
            if (!simulateByteStep(scan, st, c, writes))
                continue;
            if (llvm::any_of(writes, [&](auto w) {
                    return !dom.dominates(w.address, scan.loop.getHeader()->getTerminator()) ||
                           (w.value != scan.cursor && w.value != scan.state &&
                            !dom.dominates(w.value, scan.loop.getHeader()->getTerminator()));
                }))
                continue;
            if (!writes.empty() && table.layout.empty())
                table.layout = writes;
            if ((!writes.empty() && writes != table.layout) ||
                (doesWrite && *doesWrite != !writes.empty()))
                continue;
            doesWrite = !writes.empty();
            mask[c / 64] |= uint64_t{1} << (c % 64);
        }
        auto it = llvm::find(table.masks, mask);
        unsigned row = it - table.masks.begin();
        if (it == table.masks.end())
            table.masks.push_back(mask);
        table.rows.push_back(row | (doesWrite.value_or(false) ? 0x8000 : 0));
        table.eligible += row != 0;
    }
    return table;
}

// Загружаем маску состояния один раз перед серией. Пока очередной байт входит в неё,
// повторяем необходимые записи и двигаем курсор. На первом неподходящем байте возвращаем
// исходному коду указатель на этот же байт, чтобы он обработал разделитель или конец ввода.
bool emitRunLoop(const ByteScan &scan) {
    auto table = computeRunMasks(scan);
    const auto &masks = table.masks;
    const auto &rows = table.rows;
    const auto &layout = table.layout;
    const unsigned eligible = table.eligible;
    if (!eligible)
        return false;
    auto &f = *scan.byte->getFunction();
    auto &m = *f.getParent();
    auto &ctx = m.getContext();
    SmallVector<uint64_t> bits;
    for (auto row : masks)
        bits.append(row.begin(), row.end());
    auto *rowTable = createTable(m, ConstantDataArray::get(ctx, rows), "run.state");
    auto *bitTable = createTable(m, ConstantDataArray::get(ctx, bits), "run.alphabet");
    auto *head = scan.loop.getHeader();
    auto *orig = head->splitBasicBlock(head->getFirstNonPHIIt(), "run.generic");
    auto *merged = PHINode::Create(scan.cursor->getType(), 3, "run.resume", orig->begin());
    scan.cursor->replaceAllUsesWith(merged);
    merged->addIncoming(scan.cursor, head);
    auto *init = BasicBlock::Create(ctx, "run.init", &f, orig);
    auto *loop = BasicBlock::Create(ctx, "run.scan", &f, orig);
    auto *advance = BasicBlock::Create(ctx, "run.next", &f, orig);
    IRBuilder<> b(head->getTerminator());
    auto *state = b.CreateFreeze(scan.state);
    auto *st = b.CreateZExtOrTrunc(state, b.getInt64Ty());
    b.CreateCondBr(b.CreateICmpULT(st, b.getInt64(scan.states)), init, orig);
    head->getTerminator()->eraseFromParent();
    b.SetInsertPoint(init);
    auto *row = b.CreateLoad(b.getInt16Ty(), b.CreateInBoundsGEP(rowTable->getValueType(), rowTable,
                                                                 {b.getInt64(0), st}));
    auto *rowIndex = layout.empty() ? row : b.CreateAnd(row, b.getInt16(0x7fff));
    auto *base = b.CreateShl(b.CreateZExt(rowIndex, b.getInt64Ty()), 1);
    auto *at = b.CreateInBoundsGEP(bitTable->getValueType(), bitTable, {b.getInt64(0), base});
    auto *low = b.CreateLoad(b.getInt64Ty(), at);
    auto *high =
        b.CreateLoad(b.getInt64Ty(), b.CreateInBoundsGEP(b.getInt64Ty(), at, b.getInt64(1)));
    b.CreateCondBr(b.CreateIsNotNull(row), loop, orig);
    merged->addIncoming(scan.cursor, init);
    b.SetInsertPoint(loop);
    auto *cur = b.CreatePHI(scan.cursor->getType(), 2);
    cur->addIncoming(scan.cursor, init);
    auto *c = b.CreateFreeze(b.CreateAlignedLoad(b.getInt8Ty(), cur, Align(1)));
    auto *group = b.CreateSelect(b.CreateICmpULT(c, b.getInt8(64)), low, high);
    auto *n = b.CreateZExt(b.CreateAnd(c, b.getInt8(63)), b.getInt64Ty());
    auto *bit = b.CreateTrunc(b.CreateLShr(group, n), b.getInt1Ty());
    b.CreateCondBr(b.CreateAnd(bit, b.CreateICmpULT(c, b.getInt8(128))), advance, orig);
    merged->addIncoming(cur, loop);
    // Проверяем конец только на ветви, которая в исходном цикле тоже дошла бы до проверки.
    // Если байт последний, исходный код получит его вместе с обработкой конца ввода.
    b.SetInsertPoint(advance);
    auto *next = b.CreateGEP(b.getInt8Ty(), cur, b.getInt64(1));
    auto *keep = advance;
    if (scan.end) {
        keep = BasicBlock::Create(ctx, "run.commit", &f, orig);
        b.CreateCondBr(b.CreateICmpNE(next, scan.end), keep, orig);
        merged->addIncoming(cur, advance);
        b.SetInsertPoint(keep);
    }
    if (!layout.empty()) {
        auto *perform = BasicBlock::Create(ctx, "run.write", &f, orig);
        b.CreateCondBr(b.CreateICmpSLT(row, b.getInt16(0)), perform, loop);
        cur->addIncoming(next, keep);
        b.SetInsertPoint(perform);
        for (auto w : layout)
            b.CreateAlignedStore(w.value == scan.cursor  ? cur
                                 : w.value == scan.state ? state
                                                         : w.value,
                                 w.address, w.alignment);
        keep = perform;
    }
    b.CreateBr(loop);
    cur->addIncoming(next, keep);
    errs() << "self-run\t" << f.getName() << "\t" << eligible << "\t" << masks.size() - 1 << "\t"
           << layout.size() << "\t" << (scan.end != nullptr) << "\n";
    return true;
}

// Поздний этап запускается после обычных оптимизаций LLVM. Ищем серии только в модулях,
// где ранний этап уже пометил таблицу. После добавления цикла перестраиваем сведения о циклах.
struct SelfRun : PassInfoMixin<SelfRun> {
    PreservedAnalyses run(Module &m, ModuleAnalysisManager &) {
        // Сам запрос нового имени метки меняет модуль LLVM. Ищем среди уже имеющихся имён.
        SmallVector<StringRef> kinds;
        m.getMDKindNames(kinds);
        auto kind = llvm::find(kinds, StateTableTag);
        if (kind == kinds.end() || llvm::none_of(m.globals(), [&](auto &g) {
                return g.getMetadata(unsigned(kind - kinds.begin())) != nullptr;
            }))
            return PreservedAnalyses::all();
        bool changed = false;
        for (auto &f : m) {
            if (f.isDeclaration() || f.hasFnAttribute(Attribute::Cold))
                continue;
            SmallPtrSet<BasicBlock *, 32> visited;
            for (;;) {
                DominatorTree dt(f);
                LoopInfo loops(dt);
                bool again = false;
                for (auto *l : loops.getLoopsInPreorder()) {
                    if (!visited.insert(l->getHeader()).second)
                        continue;
                    if (auto scan = findByteScan(*l, dt); scan && emitRunLoop(*scan)) {
                        again = changed = true;
                        break;
                    }
                }
                if (!again)
                    break;
            }
        }
        return changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
    }
};

// При -O2 и -O3 сначала заменяем поиски в таблицах, пока видна их исходная форма.
void addTables(ModulePassManager &passes, bool actions) {
    passes.addPass(DefaultTransitions());
    passes.addPass(SparseTransition(actions));
}

// При обычной компиляции запускаем преобразование на уровнях -O2 и -O3.
bool enabled(OptimizationLevel level) {
    return level == OptimizationLevel::O2 || level == OptimizationLevel::O3;
}

// opt позволяет отдельно проверить построение таблиц, выбор действий и чтение серий байтов.
bool parsePipeline(StringRef name, ModulePassManager &passes,
                   ArrayRef<PassBuilder::PipelineElement>) {
    if (name == "transition-table")
        addTables(passes, false);
    else if (name == "transition-actions")
        addTables(passes, true);
    else if (name == "transition-runs")
        passes.addPass(SelfRun());
    else if (name == "transition-split") {
        addTables(passes, true);
        passes.addPass(SelfRun());
    } else {
        return false;
    }
    return true;
}

// После упрощения внешнего цикла штатными проходами LLVM легче проследить шаг курсора.
// Поэтому построение цикла для серий байтов ставим в конец оптимизации.
void registerPasses(PassBuilder &builder) {
    builder.registerPipelineEarlySimplificationEPCallback(
        [](ModulePassManager &passes, OptimizationLevel level, ThinOrFullLTOPhase) {
            if (enabled(level))
                addTables(passes, true);
        });
    builder.registerOptimizerLastEPCallback(
        [](ModulePassManager &passes, OptimizationLevel level, ThinOrFullLTOPhase) {
            if (enabled(level))
                passes.addPass(SelfRun());
        });
    builder.registerPipelineParsingCallback(parsePipeline);
}
}

// Через эту точку входа clang и opt узнают, как подключить проход.
extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo llvmGetPassPluginInfo() {
    return {LLVM_PLUGIN_API_VERSION, "TransitionSplit", "0.3", registerPasses};
}
