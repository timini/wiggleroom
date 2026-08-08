#pragma once

#include "rack.hpp"

using namespace rack;

// Forward declarations — all EucLogic model pointers
extern Model* modelEucSeq;
extern Model* modelLogicMangler;
extern Model* modelEucBank;
extern Model* modelEucMix;
extern Model* modelEucSeq2;
extern Model* modelLogicMangler2;
extern Model* modelEucBank2;
extern Model* modelEucMix2;
extern Model* modelEucSeq3;
extern Model* modelLogicMangler3;
extern Model* modelEucBank3;
extern Model* modelEucMix3;

namespace WiggleRoom {

template<int N>
struct EuclogicModels;

template<>
struct EuclogicModels<2> {
    static Model*& eucSeq()       { return modelEucSeq2; }
    static Model*& logicMangler() { return modelLogicMangler2; }
    static Model*& eucBank()      { return modelEucBank2; }
    static Model*& eucMix()       { return modelEucMix2; }
};

template<>
struct EuclogicModels<3> {
    static Model*& eucSeq()       { return modelEucSeq3; }
    static Model*& logicMangler() { return modelLogicMangler3; }
    static Model*& eucBank()      { return modelEucBank3; }
    static Model*& eucMix()       { return modelEucMix3; }
};

template<>
struct EuclogicModels<4> {
    static Model*& eucSeq()       { return modelEucSeq; }
    static Model*& logicMangler() { return modelLogicMangler; }
    static Model*& eucBank()      { return modelEucBank; }
    static Model*& eucMix()       { return modelEucMix; }
};

} // namespace WiggleRoom
