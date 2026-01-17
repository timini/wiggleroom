// Test for BigReverb DSP with Zita Rev1 algorithm
#include <iostream>
#include <cmath>

#define FAUST_MODULE_NAME TestBigReverb
#include "../build/src/modules/BigReverb/faust_gen/big_reverb.hpp"

using namespace FaustGenerated::NS_TestBigReverb;

// Faust params (alphabetical): damping=0, decay_high=1, decay_low=2, mix=3, predelay=4, xover=5
bool testWithParams(float damping, float decay_high, float decay_low,
                    float mix, float predelay, float xover, int max_samples = 100000) {
    VCVRackDSP dsp;
    dsp.init(48000);

    dsp.setParamValue(0, damping);      // damping (Hz)
    dsp.setParamValue(1, decay_high);   // decay_high (s)
    dsp.setParamValue(2, decay_low);    // decay_low (s)
    dsp.setParamValue(3, mix);          // mix (0-1)
    dsp.setParamValue(4, predelay);     // predelay (ms)
    dsp.setParamValue(5, xover);        // xover (Hz)

    float inputL = 1.0f, inputR = 1.0f;
    float outputL = 0.0f, outputR = 0.0f;
    float* inputs[2] = { &inputL, &inputR };
    float* outputs[2] = { &outputL, &outputR };

    for (int i = 0; i < max_samples; i++) {
        dsp.compute(1, inputs, outputs);

        if (std::isnan(outputL) || std::isnan(outputR) ||
            std::isinf(outputL) || std::isinf(outputR)) {
            std::cout << "  FAIL: NaN/Inf at sample " << i
                      << " (L=" << outputL << ", R=" << outputR << ")" << std::endl;
            return false;
        }

        if (i == 0) {
            inputL = 0.0f;
            inputR = 0.0f;
        }
    }
    std::cout << "  PASS (" << max_samples << " samples)" << std::endl;
    return true;
}

int main() {
    std::cout << "=== BigReverb Zita Rev1 Test ===" << std::endl;

    // Print parameter info
    VCVRackDSP dsp;
    dsp.init(48000);
    std::cout << "\nParameters:" << std::endl;
    for (int i = 0; i < dsp.getNumParams(); i++) {
        std::cout << "  [" << i << "] " << dsp.getParamPath(i)
                  << " = " << dsp.getParamValue(i)
                  << " (range: " << dsp.getParamMin(i)
                  << " - " << dsp.getParamMax(i) << ")" << std::endl;
    }

    std::cout << "\n=== Testing various parameter combinations ===" << std::endl;
    int passed = 0, total = 0;

    // Default values
    std::cout << "\n1. Default values:" << std::endl;
    std::cout << "   damping=5000Hz, decay_high=2s, decay_low=3s, mix=0.5, pre=20ms, xover=200Hz" << std::endl;
    if (testWithParams(5000, 2, 3, 0.5, 20, 200)) passed++; total++;

    // Test low decay values (these would cause NaN without the max(0.1) clamp)
    std::cout << "\n2. Minimum decay values (0.1s each) - the critical test:" << std::endl;
    if (testWithParams(5000, 0.1, 0.1, 0.5, 20, 200)) passed++; total++;

    std::cout << "\n3. Very short treble decay (0.1s), long bass (60s):" << std::endl;
    if (testWithParams(5000, 0.1, 60, 0.5, 20, 200)) passed++; total++;

    std::cout << "\n4. Long treble decay (20s), short bass (0.1s):" << std::endl;
    if (testWithParams(5000, 20, 0.1, 0.5, 20, 200)) passed++; total++;

    std::cout << "\n5. Maximum decay values (infinite ambience):" << std::endl;
    if (testWithParams(5000, 20, 60, 0.5, 20, 200)) passed++; total++;

    // Test damping extremes
    std::cout << "\n6. Minimum damping (100Hz - very dark):" << std::endl;
    if (testWithParams(100, 2, 3, 0.5, 20, 200)) passed++; total++;

    std::cout << "\n7. Maximum damping (15000Hz - very bright):" << std::endl;
    if (testWithParams(15000, 2, 3, 0.5, 20, 200)) passed++; total++;

    // Test predelay extremes
    std::cout << "\n8. No predelay (0ms):" << std::endl;
    if (testWithParams(5000, 2, 3, 0.5, 0, 200)) passed++; total++;

    std::cout << "\n9. Maximum predelay (200ms):" << std::endl;
    if (testWithParams(5000, 2, 3, 0.5, 200, 200)) passed++; total++;

    // Test crossover extremes
    std::cout << "\n10. Minimum crossover (50Hz):" << std::endl;
    if (testWithParams(5000, 2, 3, 0.5, 20, 50)) passed++; total++;

    std::cout << "\n11. Maximum crossover (6000Hz):" << std::endl;
    if (testWithParams(5000, 2, 3, 0.5, 20, 6000)) passed++; total++;

    // Test mix extremes
    std::cout << "\n12. All dry (mix=0):" << std::endl;
    if (testWithParams(5000, 2, 3, 0, 20, 200)) passed++; total++;

    std::cout << "\n13. All wet (mix=1):" << std::endl;
    if (testWithParams(5000, 2, 3, 1, 20, 200)) passed++; total++;

    // Stress test with all minimum/maximum combinations
    std::cout << "\n14. Stress test - all extremes combined:" << std::endl;
    if (testWithParams(100, 0.1, 0.1, 1.0, 200, 50)) passed++; total++;

    std::cout << "\n=== Results: " << passed << "/" << total << " tests passed ===" << std::endl;

    if (passed == total) {
        std::cout << "\nSUCCESS: Zita Rev1 is working correctly with no NaN/Inf issues!" << std::endl;
        return 0;
    } else {
        std::cout << "\nFAILURE: Some tests produced NaN/Inf values." << std::endl;
        return 1;
    }
}
