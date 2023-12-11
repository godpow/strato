// SPDX-License-Identifier: MPL-2.0
// Copyright © 2023 Strato Team and Contributors (https://github.com/strato-emu/)

#pragma once

#include <string>
#include <type_traits>
#include <dynarmic/interface/halt_reason.h>

namespace skyline::jit {
    namespace {
        using DynarmicHaltReasonType = std::underlying_type_t<Dynarmic::HaltReason>;
    }

    /**
     * @brief The reason that the JIT has halted
     * @note The binary representation of this enum's values must match Dynarmic::HaltReason one
     */
    enum class HaltReason : DynarmicHaltReasonType {
        Step = static_cast<DynarmicHaltReasonType>(Dynarmic::HaltReason::Step),
        CacheInvalidation = static_cast<DynarmicHaltReasonType>(Dynarmic::HaltReason::CacheInvalidation),
        MemoryAbort = static_cast<DynarmicHaltReasonType>(Dynarmic::HaltReason::MemoryAbort),
        Preempted = static_cast<DynarmicHaltReasonType>(Dynarmic::HaltReason::UserDefined1)
    };

    inline std::string to_string(HaltReason hr) {
        switch (hr) {
            case HaltReason::Step:
                return "Step";
            case HaltReason::CacheInvalidation:
                return "CacheInvalidation";
            case HaltReason::MemoryAbort:
                return "MemoryAbort";
            case HaltReason::Preempted:
                return "Preempted";
            default:
                return "Unknown";
        }
    }

    inline std::string to_string(Dynarmic::HaltReason hr) {
        return to_string(static_cast<HaltReason>(hr));
    }

    /**
     * @brief Converts a HaltReason to a Dynarmic::HaltReason
     */
    inline Dynarmic::HaltReason ToDynarmicHaltReason(HaltReason reason) {
        return static_cast<Dynarmic::HaltReason>(reason);
    }
}
