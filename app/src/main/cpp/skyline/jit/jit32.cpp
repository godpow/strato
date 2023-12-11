// SPDX-License-Identifier: MPL-2.0
// Copyright © 2023 Strato Team and Contributors (https://github.com/strato-emu/)

#include <kernel/types/KProcess.h>
#include "jit32.h"

namespace skyline::jit {
    Jit32::Jit32(DeviceState &state, u32 core_id) : state{state}, core_id{core_id}, jit{MakeJit()} {}

    Dynarmic::A32::Jit Jit32::MakeJit() {
        using namespace Dynarmic::A32;
        UserConfig config;

        config.callbacks = this;
        config.processor_id = core_id;

        config.enable_cycle_counting = false;

        return Jit{config};
    }

    void Jit32::Run() {
        auto haltReason{jit.Run()};
        LOGE("JIT halted: {}", to_string(haltReason));
    }

    void Jit32::HaltExecution(HaltReason hr) {
        jit.HaltExecution(ToDynarmicHaltReason(hr));
    }

    void Jit32::ClearHalt(HaltReason hr) {
        jit.ClearHalt(ToDynarmicHaltReason(hr));
    }

    void Jit32::SaveContext(ThreadContext32 &context) {
        context.gpr = jit.Regs();
        context.fpr = jit.ExtRegs();
        context.cpsr = jit.Cpsr();
        context.fpscr = jit.Fpscr();
    }

    void Jit32::RestoreContext(ThreadContext32 &context) {
        jit.Regs() = context.gpr;
        jit.ExtRegs() = context.fpr;
        jit.SetCpsr(context.cpsr);
        jit.SetFpscr(context.fpscr);
    }

    void Jit32::SetThreadPointer(u32 thread_ptr) {
        // TODO: implement coprocessor 15
    }

    void Jit32::SetTlsPointer(u32 tls_ptr) {
        // TODO: implement coprocessor 15
    }

    u32 Jit32::GetPC() {
        return jit.Regs()[15];
    }

    void Jit32::SetPC(u32 pc) {
        jit.Regs()[15] = pc;
    }

    u32 Jit32::GetSP() {
        return jit.Regs()[13];
    }

    void Jit32::SetSP(u32 sp) {
        jit.Regs()[13] = sp;
    }

    u32 Jit32::GetRegister(u32 reg) {
        return jit.Regs()[reg];
    }

    void Jit32::SetRegister(u32 reg, u32 value) {
        jit.Regs()[reg] = value;
    }

    template<typename T>
    __attribute__((__always_inline__)) T ReadUnaligned(u8 *ptr) {
        T value;
        std::memcpy(&value, ptr, sizeof(T));
        return value;
    }

    template<typename T>
    __attribute__((__always_inline__)) void WriteUnaligned(u8 *ptr, T value) {
        std::memcpy(ptr, &value, sizeof(T));
    }

    template<typename T>
    __attribute__((__always_inline__)) T Jit32::MemoryRead(u32 vaddr) {
        // The number of bits needed to encode the size of T minus 1
        constexpr u32 bits = std::bit_width(sizeof(T)) - 1;
        // Compute the mask to have "bits" number of 1s (e.g. 0b111 for 3 bits)
        constexpr u32 mask{(1 << bits) - 1};

        if ((vaddr & mask) == 0) // Aligned access
            return state.process->memory.base.cast<T>()[vaddr >> bits];
        else
            return ReadUnaligned<T>(state.process->memory.base.data() + vaddr);
    }

    template<typename T>
    __attribute__((__always_inline__)) void Jit32::MemoryWrite(u32 vaddr, T value) {
        // The number of bits needed to encode the size of T minus 1
        constexpr u32 bits = std::bit_width(sizeof(T)) - 1;
        // Compute the mask to have "bits" number of 1s (e.g. 0b111 for 3 bits)
        constexpr u32 mask{(1 << bits) - 1};

        if ((vaddr & mask) == 0) // Aligned access
            state.process->memory.base.cast<T>()[vaddr >> bits] = value;
        else
            WriteUnaligned<T>(state.process->memory.base.data() + vaddr, value);
    }

    u8 Jit32::MemoryRead8(u32 vaddr) {
        return MemoryRead<u8>(vaddr);
    }

    u16 Jit32::MemoryRead16(u32 vaddr) {
        return MemoryRead<u16>(vaddr);
    }

    u32 Jit32::MemoryRead32(u32 vaddr) {
        return MemoryRead<u32>(vaddr);
    }

    u64 Jit32::MemoryRead64(u32 vaddr) {
        return MemoryRead<u64>(vaddr);
    }

    void Jit32::MemoryWrite8(u32 vaddr, u8 value) {
        MemoryWrite<u8>(vaddr, value);
    }

    void Jit32::MemoryWrite16(u32 vaddr, u16 value) {
        MemoryWrite<u16>(vaddr, value);
    }

    void Jit32::MemoryWrite32(u32 vaddr, u32 value) {
        MemoryWrite<u32>(vaddr, value);
    }

    void Jit32::MemoryWrite64(u32 vaddr, u64 value) {
        MemoryWrite<u64>(vaddr, value);
    }

    void Jit32::InterpreterFallback(u32 pc, size_t num_instructions) {
        // This is never called in practice.
        state.process->Kill(false, true);
    }

    void Jit32::CallSVC(u32 swi) {
        // Do something.
        throw exception("Performed SVC: {}", swi);
    }

    void Jit32::ExceptionRaised(u32 pc, Dynarmic::A32::Exception exception) {
        // Do something.
    }
}
