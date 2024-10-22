#include "nvdec.h"

namespace skyline::soc::host1x {
    NvDecClass::NvDecClass(std::function<void()> opDoneCallback)
        : opDoneCallback(std::move(opDoneCallback)), decoderInitialized(false) {}

    void NvDecClass::CallMethod(u32 method, u32 argument) {
        switch (method) {
            case 0x00:
                InitializeDecoder();
                break;
            case 0x01:
                DecodeFrame({static_cast<u8>(argument)});
                break;
            case 0x02:
                FinalizeDecoder();
                break;
            default:
                LOGW("Unknown NVDEC class method called: 0x{:X} argument: 0x{:X}", method, argument);
                break;
        }
    }

    void NvDecClass::InitializeDecoder() {
        decoderInitialized = true;
        LOGD("Decoder initialized");
    }

    void NvDecClass::DecodeFrame(const std::vector<u8>& frameData) {
        if (!decoderInitialized) {
            LOGE("Decoder not initialized");
            return;
        }

        decodedFrames.push_back(frameData);
        LOGD("Frame decoded successfully, total decoded frames: {}", decodedFrames.size());
    }

    void NvDecClass::FinalizeDecoder() {
        decoderInitialized = false;
        decodedFrames.clear();
        LOGD("Decoder finalized");
    }
}
