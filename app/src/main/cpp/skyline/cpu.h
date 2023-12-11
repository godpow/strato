// SPDX-License-Identifier: MPL-2.0
// Copyright © 2023 Strato Team and Contributors (https://github.com/strato-emu/)

#pragma once

#include <array>
#include <common.h>
#include <jit/jit32.h>
#include "kernel/scheduler.h"

namespace skyline::cpu {
    class CPU {
      public:
        CPU(DeviceState &state);

        std::array<jit::Jit32, constant::CoreCount> jit_cores;

      private:
        DeviceState &state;
    };
}
