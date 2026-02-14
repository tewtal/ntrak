#include "ntrak/emulation/SpcDsp.hpp"

#include <gtest/gtest.h>

namespace ntrak::emulation {
namespace {

TEST(SpcDspPreviewTest, RunDspOnlyForSamplesKeepsCpuStateFrozen) {
    SpcDsp dsp;
    dsp.reset();
    dsp.clearSampleBuffer();
    dsp.setPC(0x0200);

    const uint16_t initialPc = dsp.pc();
    const uint8_t initialA = dsp.a();
    const uint8_t initialX = dsp.x();
    const uint8_t initialY = dsp.y();
    const uint8_t initialSp = dsp.sp();
    const uint8_t initialPs = dsp.ps();
    const uint64_t initialCycle = dsp.cycleCount();

    constexpr uint32_t kSamples = 64;
    dsp.runDspOnlyForSamples(kSamples);

    EXPECT_EQ(dsp.pc(), initialPc);
    EXPECT_EQ(dsp.a(), initialA);
    EXPECT_EQ(dsp.x(), initialX);
    EXPECT_EQ(dsp.y(), initialY);
    EXPECT_EQ(dsp.sp(), initialSp);
    EXPECT_EQ(dsp.ps(), initialPs);
    EXPECT_EQ(dsp.cycleCount(), initialCycle);
    EXPECT_EQ(dsp.sampleCount(), kSamples);
}

TEST(SpcDspPreviewTest, RunForSamplesStillAdvancesCpuState) {
    SpcDsp dsp;
    dsp.reset();
    dsp.setPC(0x0200);

    const uint16_t initialPc = dsp.pc();
    const uint64_t initialCycle = dsp.cycleCount();

    dsp.runForSamples(64);

    EXPECT_NE(dsp.pc(), initialPc);
    EXPECT_GT(dsp.cycleCount(), initialCycle);
}

}  // namespace
}  // namespace ntrak::emulation
