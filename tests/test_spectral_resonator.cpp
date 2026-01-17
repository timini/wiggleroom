// Test for SpectralResonator DSP
#include <iostream>
#include <cmath>

#define FAUST_MODULE_NAME TestSpectralResonator
#include "../build/src/modules/SpectralResonator/faust_gen/spectral_resonator.hpp"

using namespace FaustGenerated::NS_TestSpectralResonator;

// Faust params (alphabetical): damp=0, q=1, spread=2, volts=3
bool testWithParams(float damp, float q, float spread, float volts, int max_samples = 100000) {
    VCVRackDSP dsp;
    dsp.init(48000);

    dsp.setParamValue(0, damp);    // damp (Hz)
    dsp.setParamValue(1, q);       // q (resonance)
    dsp.setParamValue(2, spread);  // spread (chord type)
    dsp.setParamValue(3, volts);   // volts (pitch)

    // Use an impulse as input (like hitting the resonator)
    float input = 1.0f;
    float outputL = 0.0f, outputR = 0.0f;
    float* inputs[1] = { &input };
    float* outputs[2] = { &outputL, &outputR };

    for (int i = 0; i < max_samples; i++) {
        dsp.compute(1, inputs, outputs);

        if (std::isnan(outputL) || std::isnan(outputR) ||
            std::isinf(outputL) || std::isinf(outputR)) {
            std::cout << "  FAIL: NaN/Inf at sample " << i
                      << " (L=" << outputL << ", R=" << outputR << ")" << std::endl;
            return false;
        }

        // After impulse, use silence to let it ring
        if (i == 0) {
            input = 0.0f;
        }
    }
    std::cout << "  PASS (" << max_samples << " samples)" << std::endl;
    return true;
}

int main() {
    std::cout << "=== SpectralResonator Test ===" << std::endl;

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

    std::cout << "\nInputs: " << dsp.getNumInputs() << ", Outputs: " << dsp.getNumOutputs() << std::endl;

    std::cout << "\n=== Testing various parameter combinations ===" << std::endl;
    int passed = 0, total = 0;

    // Default values
    std::cout << "\n1. Default values:" << std::endl;
    std::cout << "   damp=10000Hz, q=50, spread=1.5, volts=4" << std::endl;
    if (testWithParams(10000, 50, 1.5, 4)) passed++; total++;

    // Test spread extremes
    std::cout << "\n2. Minimum spread (unison):" << std::endl;
    if (testWithParams(10000, 50, 1.0, 4)) passed++; total++;

    std::cout << "\n3. Maximum spread (wide):" << std::endl;
    if (testWithParams(10000, 50, 3.0, 4)) passed++; total++;

    // Test Q extremes
    std::cout << "\n4. Minimum Q (1):" << std::endl;
    if (testWithParams(10000, 1, 1.5, 4)) passed++; total++;

    std::cout << "\n5. Maximum Q (100) - glass harp mode:" << std::endl;
    if (testWithParams(10000, 100, 1.5, 4)) passed++; total++;

    // Test pitch extremes
    std::cout << "\n6. Minimum pitch (0V = 110Hz):" << std::endl;
    if (testWithParams(10000, 50, 1.5, 0)) passed++; total++;

    std::cout << "\n7. Maximum pitch (10V = 110*1024Hz):" << std::endl;
    if (testWithParams(10000, 50, 1.5, 10)) passed++; total++;

    // Test damping extremes
    std::cout << "\n8. Minimum damping (1000Hz - very dark):" << std::endl;
    if (testWithParams(1000, 50, 1.5, 4)) passed++; total++;

    std::cout << "\n9. Maximum damping (20000Hz - very bright):" << std::endl;
    if (testWithParams(20000, 50, 1.5, 4)) passed++; total++;

    // Stress tests
    std::cout << "\n10. High Q + High pitch + Wide spread:" << std::endl;
    if (testWithParams(10000, 100, 3.0, 8)) passed++; total++;

    std::cout << "\n11. Low Q + Low pitch + Narrow spread:" << std::endl;
    if (testWithParams(5000, 10, 1.1, 1)) passed++; total++;

    std::cout << "\n12. All extremes:" << std::endl;
    if (testWithParams(1000, 100, 3.0, 10)) passed++; total++;

    std::cout << "\n=== Results: " << passed << "/" << total << " tests passed ===" << std::endl;

    if (passed == total) {
        std::cout << "\nSUCCESS: SpectralResonator is working correctly!" << std::endl;
        return 0;
    } else {
        std::cout << "\nFAILURE: Some tests produced NaN/Inf values." << std::endl;
        return 1;
    }
}
