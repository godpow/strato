// SPDX-License-Identifier: MPL-2.0
// Copyright © 2023 Strato Team and Contributors (https://github.com/strato-emu/)

#include "cpu.h"

namespace skyline::cpu {
    CPU::CPU(DeviceState &state)
        : state{state},
          jit_cores{jit::Jit32(state, 0), jit::Jit32(state, 1), jit::Jit32(state, 2), jit::Jit32(state, 3)} {}
}
