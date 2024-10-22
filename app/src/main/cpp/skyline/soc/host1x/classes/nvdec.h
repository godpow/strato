#pragma once

#include <common.h>

namespace skyline::soc::host1x {
    /**
     * @brief The NVDEC Host1x class implements hardware accelerated video decoding for the VP9/VP8/H264/VC1 codecs
     */
    class NvDecClass {
      private:
        std::function<void()> opDoneCallback;

      public:
        NvDecClass(std::function<void()> opDoneCallback);

        void CallMethod(u32 method, u32 argument);

        // Add necessary methods and functionality to NvDecClass
        void Initialize();
        void Decode();
        void Finalize();
    };
}
