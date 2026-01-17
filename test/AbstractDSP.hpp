#pragma once

/**
 * Abstract interface for Faust DSP modules
 *
 * Allows polymorphic access to different Faust-generated DSP classes.
 */
class AbstractDSP {
public:
    virtual ~AbstractDSP() = default;
    virtual void init(int sampleRate) = 0;
    virtual void compute(int count, float** inputs, float** outputs) = 0;
    virtual int getNumInputs() const = 0;
    virtual int getNumOutputs() const = 0;
    virtual int getNumParams() const = 0;
    virtual void setParamValue(int index, float value) = 0;
    virtual float getParamValue(int index) const = 0;
    virtual const char* getParamPath(int index) const = 0;
    virtual float getParamMin(int index) const = 0;
    virtual float getParamMax(int index) const = 0;
    virtual float getParamInit(int index) const = 0;
    virtual int getParamIndex(const char* name) const = 0;
};
