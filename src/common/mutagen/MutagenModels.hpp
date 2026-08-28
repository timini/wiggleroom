#pragma once
/******************************************************************************
 * MUTAGEN MODELS
 * Neighbour identity for the expander chain.
 *
 * Expander links in this plugin are raw Model* pointer comparisons, never
 * dynamic_cast and never slug strings, so each module that participates
 * needs its global visible to the others.
 ******************************************************************************/

#include "rack.hpp"

using namespace rack;

extern Model* modelMutagen;
extern Model* modelMutagenX;
extern Model* modelIntersect;
