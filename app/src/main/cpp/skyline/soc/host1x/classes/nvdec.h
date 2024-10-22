#pragma once

#include <common.h>
#include <vector>

namespace skyline::soc::host1x {
    /**
     * @brief The NVDEC Host1x class implements hardware accelerated video decoding for the VP9/VP8/H264/VC1 codecs
     */
    class NvDecClass {
      private:
        std::function<void()> opDoneCallback;
        bool decoderInitialized;
        std::vector<std::vector<u8>> decodedFrames;

      public:
        NvDecClass(std::function<void()> opDoneCallback);

        void CallMethod(u32 method, u32 argument);

        void InitializeDecoder();
        void DecodeFrame(const std::vector<u8>& frameData);
        void FinalizeDecoder();
    };
}
