///
/// Copyright (C) 2010-2016, Dependable Systems Laboratory, EPFL
/// Copyright (C) 2014-2016, Cyberhaven
/// All rights reserved.
///
/// Licensed under the Cyberhaven Research License Agreement.
///

#ifndef S2E_EXECUTOR_H
#define S2E_EXECUTOR_H

#include <unordered_map>

// Undefine cat from "compiler.h"
#undef cat
#include <klee/Executor.h>
#include <llvm/Support/raw_ostream.h>
#include <s2e/s2e_libcpu.h>
#include <timer.h>

#include "S2ETranslationBlock.h"

struct TranslationBlock;
CPUArchState;
class TCGLLVMTranslator;

namespace klee {
struct Query;
}

namespace s2e {

class S2E;
class S2EExecutionState;
struct S2ETranslationBlock;

class CpuExitException {};

typedef void (*StateManagerCb)(S2EExecutionState *s, bool killingState);

class S2EExecutor : public klee::Executor {
protected:
    S2E *m_s2e;
    TCGLLVMTranslator *m_llvmTranslator;

    klee::KFunction *m_dummyMain;

    /* Unused memory regions that should be unmapped.
       Copy-then-unmap is used in order to catch possible
       direct memory accesses from libcpu code. */
    std::vector<std::pair<uint64_t, uint64_t>> m_unusedMemoryDescs;

    std::vector<klee::ObjectKey> m_saveOnContextSwitch;

    std::vector<S2EExecutionState *> m_deletedStates;

    bool m_executeAlwaysKlee;

    bool m_forkProcTerminateCurrentState;

    bool m_inLoadBalancing;

    struct CPUTimer *m_stateSwitchTimer;

    // This is a set of TBs that are currently stored in libcpu's TB cache
    std::unordered_set<S2ETranslationBlockPtr, S2ETranslationBlockHash, S2ETranslationBlockEqual> m_s2eTbs;

public:
    S2EExecutor(S2E *s2e, TCGLLVMTranslator *translator, klee::InterpreterHandler *ie);
    virtual ~S2EExecutor();

    /** Called on fork, used to trace forks */
    StatePair fork(klee::ExecutionState &current, const klee::ref<klee::Expr> &condition,
                   bool keepConditionTrueInCurrentState = false);

    void flushTb();

    /** Create initial execution state */
    S2EExecutionState *createInitialState();

    void initializeExecution(S2EExecutionState *initialState, bool executeAlwaysKlee);

#if defined(TARGET_I386) || defined(TARGET_X86_64)
    void registerCpu(S2EExecutionState *initialState, CPUX86State *cpuEnv);
#elif defined(TARGET_ARM)
    void registerCpu(S2EExecutionState *initialState, CPUARMState *cpuEnv);
#else
#error Unsupported target architecture
#endif

    void registerRam(S2EExecutionState *initialState, struct MemoryDesc *region, uint64_t startAddress, uint64_t size,
                     uint64_t hostAddress, bool isSharedConcrete, bool saveOnContextSwitch = true,
                     const char *name = "");

    void registerSharedExternalObject(S2EExecutionState *state, void *address, unsigned size);

    void registerDirtyMask(S2EExecutionState *initial_state, uint64_t host_address, uint64_t size);

    void updateConcreteFastPath(S2EExecutionState *state);

    /* Execute llvm function in current context */
    klee::ref<klee::Expr>
    executeFunction(S2EExecutionState *state, llvm::Function *function,
                    const std::vector<klee::ref<klee::Expr>> &args = std::vector<klee::ref<klee::Expr>>());

    klee::ref<klee::Expr>
    executeFunction(S2EExecutionState *state, const std::string &functionName,
                    const std::vector<klee::ref<klee::Expr>> &args = std::vector<klee::ref<klee::Expr>>());

    uintptr_t executeTranslationBlock(S2EExecutionState *state, TranslationBlock *tb);

    static uintptr_t executeTranslationBlockSlow(CPUArchState *env1, struct TranslationBlock *tb);
    static uintptr_t executeTranslationBlockFast(CPUArchState *env1, struct TranslationBlock *tb);

    /* Returns true if the CPU loop must be exited */
    bool finalizeTranslationBlockExec(S2EExecutionState *state);

    void cleanupTranslationBlock(S2EExecutionState *state);

    S2EExecutionState *selectNextState(S2EExecutionState *state);
    klee::ExecutionState *selectSearcherState(S2EExecutionState *state);

    void updateStates(klee::ExecutionState *current);

    void setCCOpEflags(S2EExecutionState *state);
#if defined(TARGET_I386) || defined(TARGET_X86_64)
    void doInterrupt(S2EExecutionState *state, int intno, int is_int, int error_code, uint64_t next_eip, int is_hw);
    static void doInterruptAll(int intno, int is_int, int error_code, uintptr_t next_eip, int is_hw);
#elif defined(TARGET_ARM)
    void doInterrupt(S2EExecutionState *state);
    static void doInterruptARM(void);
    void setExternalInterrupt(int irq_num);
    void enableExternalInterruptAll(int serial);
    void disableSystickInterrupt(int mode);
    uint32_t getActiveExternalInterrupt(int serial);
#else
#error Unsupported target architecture
#endif

    /** Suspend the given state (does not kill it) */
    bool suspendState(S2EExecutionState *state);

    /** Puts back the previously suspended state in the queue */
    bool resumeState(S2EExecutionState *state);

    klee::Searcher *getSearcher() const {
        return searcher;
    }

    void setSearcher(klee::Searcher *s) {
        searcher = s;
    }

    StatePair forkCondition(S2EExecutionState *state, klee::ref<klee::Expr> condition,
                            bool keepConditionTrueInCurrentState = false);

    std::vector<klee::ExecutionState *> forkValues(S2EExecutionState *state, bool isSeedState,
                                                   klee::ref<klee::Expr> expr,
                                                   const std::vector<klee::ref<klee::Expr>> &values);

    bool merge(klee::ExecutionState &base, klee::ExecutionState &other);

    S2ETranslationBlock *allocateS2ETb();
    void flushS2ETBs();

    void initializeStatistics();

    void updateStats(S2EExecutionState *state);

    bool isLoadBalancing() const {
        return m_inLoadBalancing;
    }

    /** Kills the specified state and raises an exception to exit the cpu loop */
    virtual void terminateState(klee::ExecutionState &state);

    /** Kills the specified state and raises an exception to exit the cpu loop */
    virtual void terminateState(klee::ExecutionState &state, const std::string &message);

    void resetStateSwitchTimer();

    // Should be public because of manual forks in plugins
    void notifyFork(klee::ExecutionState &originalState, klee::ref<klee::Expr> &condition, StatePair &targets);

    /**
     * To be called by plugin code
     */
    klee::Executor::StatePair forkAndConcretize(S2EExecutionState *state, klee::ref<klee::Expr> &value_);

    static bool findFile(const std::string &dataDir, const std::string &name, std::string &ret);

protected:
    void updateClockScaling();

    void prepareFunctionExecution(S2EExecutionState *state, llvm::Function *function,
                                  const std::vector<klee::ref<klee::Expr>> &args);
    bool executeInstructions(S2EExecutionState *state, unsigned callerStackSize = 1);

    uintptr_t executeTranslationBlockKlee(S2EExecutionState *state, TranslationBlock *tb);

    uintptr_t executeTranslationBlockConcrete(S2EExecutionState *state, TranslationBlock *tb);

    void deleteState(klee::ExecutionState *state);

    void doStateSwitch(S2EExecutionState *oldState, S2EExecutionState *newState);

    void splitStates(const std::vector<S2EExecutionState *> &allStates, klee::StateSet &parentSet,
                     klee::StateSet &childSet);
    void computeNewStateGuids(std::unordered_map<klee::ExecutionState *, uint64_t> &newIds, klee::StateSet &parentSet,
                              klee::StateSet &childSet);

    void doLoadBalancing();

    void notifyBranch(klee::ExecutionState &state);

    void setupTimersHandler();
    void initializeStateSwitchTimer();
    static void stateSwitchTimerCallback(void *opaque);

    void registerFunctionHandlers(llvm::Module &module);

    void replaceExternalFunctionsWithSpecialHandlers();
    void disableConcreteLLVMHelpers();
};

} // namespace s2e

#endif // S2E_EXECUTOR_H
