// Stems - records audio, separates it into four beat-synced layers, and plays
// them three ways at once: as loops, as the source for a morphing wavetable
// voice, and as the pitch reference for a scale-detecting quantiser. The voice
// and the loop are then mixed into a granular texturiser.
//
// This file is the Rack wrapper and nothing else. Every algorithm lives in
// src/common/stems, which does not include rack.hpp and is covered by
// stems_test; keeping the split strict is what made those components testable
// at all. The rule to preserve: no DSP decisions here, only wiring, parameter
// mapping and the display.
//
// Threading. The separation worker is the only thread, and it is owned here.
// process() pins the published stem set for the whole call and never allocates
// or frees. See SeparationWorker for why reclamation uses hazard pointers.

#include "rack.hpp"

#include "LowpassGate.hpp"
#include "stems/Diffusion.hpp"
#include "stems/GrainEngine.hpp"
#include "stems/Quantizer.hpp"
#include "stems/RingBuffer.hpp"
#include "stems/ScaleDetect.hpp"
#include "stems/SeparationWorker.hpp"
#include "stems/StemMixer.hpp"
#include "stems/Transport.hpp"
#include "stems/WavetableExtract.hpp"
#include "stems/WavetableOsc.hpp"
#include "stems/Yin.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>
#include <vector>

using namespace rack;
extern Plugin* pluginInstance;

namespace WiggleRoom {

using namespace WiggleRoom::stems;

struct Stems : Module {
    enum ParamId {
        REC_ARM_PARAM, REC_MODE_PARAM, REC_THRESH_PARAM, BUF_LEN_PARAM,
        CLOCK_DIV_PARAM, LOOP_START_PARAM, LOOP_LEN_PARAM, SYNC_MODE_PARAM,
        STEM_1_LEVEL_PARAM, STEM_2_LEVEL_PARAM, STEM_3_LEVEL_PARAM, STEM_4_LEVEL_PARAM,
        STEM_1_MUTE_PARAM, STEM_2_MUTE_PARAM, STEM_3_MUTE_PARAM, STEM_4_MUTE_PARAM,
        STEM_SELECT_PARAM,
        SCALE_MODE_PARAM, ROOT_PARAM, SCALE_PARAM, QUANT_GLIDE_PARAM,
        WT_WINDOW_PARAM, WT_OFFSET_PARAM, WT_MORPH_PARAM,
        WT_COARSE_PARAM, WT_FINE_PARAM, WT_LEVEL_PARAM,
        LPG_DECAY_PARAM, LPG_COLOUR_PARAM, LPG_LEVEL_PARAM,
        GRAIN_BALANCE_PARAM, GRAIN_SIZE_PARAM, GRAIN_DENSITY_PARAM,
        GRAIN_PITCH_PARAM, GRAIN_TEXTURE_PARAM, GRAIN_SPREAD_PARAM,
        GRAIN_SPACE_PARAM, GRAIN_MIX_PARAM,
        PARAMS_LEN
    };
    enum InputId {
        AUDIO_L_INPUT, AUDIO_R_INPUT, CLOCK_INPUT, RESET_INPUT, REC_TRIG_INPUT,
        QUANT_CV_INPUT, LPG_TRIG_INPUT, GRAIN_DENSITY_CV_INPUT, GRAIN_PITCH_CV_INPUT,
        WT_OFFSET_CV_INPUT,
        INPUTS_LEN
    };
    enum OutputId {
        MAIN_L_OUTPUT, MAIN_R_OUTPUT, LOOP_L_OUTPUT, LOOP_R_OUTPUT,
        VOICE_OUTPUT, QUANT_CV_OUTPUT, DOWNBEAT_OUTPUT,
        OUTPUTS_LEN
    };
    enum LightId {
        REC_LIGHT, SEPARATING_LIGHT, ANALYSIS_ACTIVE_LIGHT, DOWNBEAT_LIGHT,
        STEM_1_MUTE_LIGHT, STEM_2_MUTE_LIGHT, STEM_3_MUTE_LIGHT, STEM_4_MUTE_LIGHT,
        LIGHTS_LEN
    };

    /** Buffer ceiling from the spec: 32 seconds regardless of tempo. */
    static constexpr float kMaxBufferSeconds = 32.f;

    Stems() {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

        configButton(REC_ARM_PARAM, "Record arm");
        configSwitch(REC_MODE_PARAM, 0.f, 2.f, 0.f, "Record mode",
                     {"Trigger", "Threshold", "Overdub"});
        configParam(REC_THRESH_PARAM, -60.f, 0.f, -30.f, "Record threshold", " dB");
        configParam(BUF_LEN_PARAM, 1.f, 16.f, 4.f, "Buffer length", " bars");

        configParam(CLOCK_DIV_PARAM, -4.f, 4.f, 0.f, "Clock division", " oct");
        configParam(LOOP_START_PARAM, 0.f, 1.f, 0.f, "Loop start");
        configParam(LOOP_LEN_PARAM, 0.f, 1.f, 1.f, "Loop length");
        // Repitch only until S20 lands; the other positions are disabled rather
        // than silently behaving as repitch.
        configSwitch(SYNC_MODE_PARAM, 0.f, 0.f, 0.f, "Sync mode", {"Repitch"});

        const char* stemNames[4] = {"Low", "Percussive", "Harmonic", "Residual"};
        for (int i = 0; i < 4; i++) {
            configParam(STEM_1_LEVEL_PARAM + i, 0.f, 1.f, 1.f,
                        std::string(stemNames[i]) + " level");
            configButton(STEM_1_MUTE_PARAM + i, std::string(stemNames[i]) + " mute");
        }
        configSwitch(STEM_SELECT_PARAM, 0.f, 3.f, 3.f, "Analysis source",
                     {"Low", "Percussive", "Harmonic", "Residual"});

        configSwitch(SCALE_MODE_PARAM, 0.f, 1.f, 0.f, "Scale mode", {"Auto", "Manual"});
        configSwitch(ROOT_PARAM, 0.f, 11.f, 0.f, "Root",
                     {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"});
        configParam(SCALE_PARAM, 0.f, (float)(Quantizer::kNumScales - 1), 1.f, "Scale");
        configParam(QUANT_GLIDE_PARAM, 0.f, 2.f, 0.f, "Glide", " s");

        configParam(WT_WINDOW_PARAM, (float)WavetableExtract::kMinWindow,
                    (float)WavetableExtract::kMaxWindow, 2048.f, "Window", " samples");
        configParam(WT_OFFSET_PARAM, -1.f, 1.f, 0.f, "Window offset");
        configParam(WT_MORPH_PARAM, 0.f, 1.f, 0.5f, "Morph");
        configParam(WT_COARSE_PARAM, -24.f, 24.f, 0.f, "Coarse", " semitones");
        configParam(WT_FINE_PARAM, -1.f, 1.f, 0.f, "Fine", " semitones");
        configParam(WT_LEVEL_PARAM, 0.f, 1.f, 0.7f, "Voice level");

        configParam(LPG_DECAY_PARAM, 0.02f, 4.f, 0.4f, "Decay", " s");
        configParam(LPG_COLOUR_PARAM, 0.f, 1.f, 0.5f, "Colour");
        configParam(LPG_LEVEL_PARAM, 0.f, 1.f, 0.f, "Resting level");

        configParam(GRAIN_BALANCE_PARAM, 0.f, 1.f, 0.5f, "Loop / voice");
        configParam(GRAIN_SIZE_PARAM, 0.001f, 0.5f, 0.08f, "Grain size", " s");
        configParam(GRAIN_DENSITY_PARAM, 0.1f, 100.f, 10.f, "Density", " Hz");
        configParam(GRAIN_PITCH_PARAM, -24.f, 24.f, 0.f, "Grain pitch", " semitones");
        configParam(GRAIN_TEXTURE_PARAM, 0.f, 1.f, 0.3f, "Texture");
        configParam(GRAIN_SPREAD_PARAM, 0.f, 1.f, 0.5f, "Spread");
        configParam(GRAIN_SPACE_PARAM, 0.f, 1.f, 0.3f, "Space");
        configParam(GRAIN_MIX_PARAM, 0.f, 1.f, 0.5f, "Grain mix");

        configInput(AUDIO_L_INPUT, "Audio left");
        configInput(AUDIO_R_INPUT, "Audio right");
        configInput(CLOCK_INPUT, "Clock");
        configInput(RESET_INPUT, "Reset");
        configInput(REC_TRIG_INPUT, "Record trigger");
        configInput(QUANT_CV_INPUT, "Quantiser CV");
        configInput(LPG_TRIG_INPUT, "Lowpass gate trigger");
        configInput(GRAIN_DENSITY_CV_INPUT, "Grain density CV");
        configInput(GRAIN_PITCH_CV_INPUT, "Grain pitch CV");
        configInput(WT_OFFSET_CV_INPUT, "Wavetable offset CV");

        configOutput(MAIN_L_OUTPUT, "Main left");
        configOutput(MAIN_R_OUTPUT, "Main right");
        configOutput(LOOP_L_OUTPUT, "Loop left");
        configOutput(LOOP_R_OUTPUT, "Loop right");
        configOutput(VOICE_OUTPUT, "Voice");
        configOutput(QUANT_CV_OUTPUT, "Quantised CV");
        configOutput(DOWNBEAT_OUTPUT, "Downbeat");

        rebuild(48000.f);
        worker_.start();
    }

    ~Stems() override { worker_.stop(); }

    void onSampleRateChange(const SampleRateChangeEvent& e) override {
        // The published stems are indexed in frames at the OLD rate, and the
        // buffer is about to be replaced with one sized at the new rate. Keeping
        // them would play the previous take at the wrong speed against a
        // playhead that no longer matches it, so the take is retired instead.
        worker_.invalidate();
        rebuild(e.sampleRate);
    }

    void onReset(const ResetEvent& e) override {
        Module::onReset(e);
        if (buffer_) buffer_->clear();
        recording_ = false;
        extractor_.reset();
        oscillator_.reset();
        diffusion_.reset();
        grains_.reset();
        detector_.reset();
        quantizer_.reset();
    }

    void process(const ProcessArgs& args) override {
        // Pin the published stems for the whole call. See SeparationWorker: the
        // pointer is valid until release(), and nothing here frees it.
        const StemSet* stems = worker_.acquire();

        readParams();
        handleRecording(args);
        advanceTransport(args);

        const double playhead = transport_.playheadFrames();
        float fallbackLeft = 0.f, fallbackRight = 0.f;
        if (buffer_) buffer_->readFrameInterpolated(playhead, fallbackLeft, fallbackRight);

        const auto loop = mixer_.process(stems, playhead, fallbackLeft, fallbackRight);

        runAnalysis(stems, args);
        const float voice = runVoice(stems, playhead, args);
        const auto out = runGranular(loop, voice, args);

        outputs[LOOP_L_OUTPUT].setVoltage(loop.left * 5.f);
        outputs[LOOP_R_OUTPUT].setVoltage(loop.right * 5.f);
        outputs[VOICE_OUTPUT].setVoltage(voice * 5.f);
        outputs[MAIN_L_OUTPUT].setVoltage(out.left * 5.f);
        outputs[MAIN_R_OUTPUT].setVoltage(out.right * 5.f);
        outputs[QUANT_CV_OUTPUT].setVoltage(quantized_);

        const bool downbeat = transport_.downbeat();
        if (downbeat) downbeatPulse_.trigger(1e-3f);
        outputs[DOWNBEAT_OUTPUT].setVoltage(downbeatPulse_.process(args.sampleTime) ? 10.f : 0.f);

        lights[REC_LIGHT].setBrightness(recording_ ? 1.f : 0.f);
        lights[SEPARATING_LIGHT].setBrightness(stems == nullptr && !buffer_->empty() ? 1.f : 0.f);
        lights[ANALYSIS_ACTIVE_LIGHT].setBrightness(analysisActive_ ? 1.f : 0.f);
        lights[DOWNBEAT_LIGHT].setBrightnessSmooth(downbeat ? 1.f : 0.f, args.sampleTime * 8.f);

        updateDisplay();
        completeHandoff();
        worker_.release(stems);
    }

    json_t* dataToJson() override {
        json_t* root = json_object();
        // The buffer is deliberately NOT saved. It is up to 32 seconds of
        // stereo float, which would put tens of megabytes into every patch
        // file; PixelProbe sets the precedent of storing a reference rather
        // than a payload. Parameters are saved by Rack itself.
        json_object_set_new(root, "detectedRoot", json_integer(detector_.held().root));
        json_object_set_new(root, "detectedMode",
                            json_integer((int)detector_.held().mode));
        return root;
    }

    void dataFromJson(json_t* root) override {
        if (!root) return;
        json_t* r = json_object_get(root, "detectedRoot");
        json_t* m = json_object_get(root, "detectedMode");
        if (r && m) {
            const int root_ = (int)json_integer_value(r);
            const auto mode = (ScaleDetect::Mode)json_integer_value(m);
            // Seeded rather than forced: the next confident detection replaces
            // it. Restoring a key means a reloaded patch quantises the same way
            // it did before, instead of snapping back to C major until the
            // module has heard enough to decide again.
            detector_.setSeed(root_, mode);
        }
    }

    /**
     * A snapshot for the display, written by the audio thread and read by the
     * UI thread.
     *
     * Handing the widget a raw pointer to the live RingBuffer was an
     * unsynchronised race: the UI read its counters and storage while the audio
     * thread wrote, cleared or replaced it, and a sample rate change could
     * delete the object mid-draw. This is a fixed array of peaks, small enough
     * to fill cheaply and never reallocated.
     */
    static constexpr int kDisplayColumns = 256;

    void readDisplay(float* out, int count, float* playheadFraction) const {
        const int n = std::min(count, kDisplayColumns);
        for (int i = 0; i < n; i++) {
            out[i] = displayPeaks_[i].load(std::memory_order_relaxed);
        }
        *playheadFraction = displayPlayhead_.load(std::memory_order_relaxed);
    }

private:
    /**
     * Fill one display column per call, so the cost is a handful of reads per
     * sample rather than a sweep of the whole buffer at draw time.
     */
    void updateDisplay() {
        const std::size_t stored = buffer_ ? buffer_->framesStored() : 0;
        if (stored < 2) {
            displayPeaks_[displayColumn_].store(0.f, std::memory_order_relaxed);
        } else {
            const std::size_t index =
                (std::size_t)((double)displayColumn_ / kDisplayColumns * (stored - 1));
            float l = 0.f, r = 0.f;
            buffer_->readFrame(index, l, r);
            displayPeaks_[displayColumn_].store(std::max(std::fabs(l), std::fabs(r)),
                                                std::memory_order_relaxed);
        }
        displayColumn_ = (displayColumn_ + 1) % kDisplayColumns;
        const float fraction = (stored > 1)
                                   ? (float)(transport_.playheadFrames() / (double)stored)
                                   : 0.f;
        displayPlayhead_.store(std::isfinite(fraction) ? fraction : 0.f,
                               std::memory_order_relaxed);
    }

    void rebuild(float sampleRate) {
        const int rate = (int)sampleRate;
        // Allocated once per sample rate change, on the UI thread, never in
        // process(). Sized for the 32 second ceiling the spec sets.
        buffer_.reset(new RingBuffer(rate, kMaxBufferSeconds, 2));
        spare_.reset(new RingBuffer(rate, kMaxBufferSeconds, 2));
        transport_.setSampleRate(rate);
        transport_.setBufferFrames(buffer_->capacityFrames());
        mixer_.setSampleRate(rate);
        quantizer_.setSampleRate(rate);
        oscillator_.setSampleRate(rate);
        gate_.setSampleRate(rate);
        grains_.setSampleRate(rate);
        diffusion_.setSampleRate(rate);
        yin_.setSampleRate(rate);
        extractor_.reset();
        oscillator_.reset();
    }

    void readParams() {
        transport_.setClockDivision(std::pow(2.f, params[CLOCK_DIV_PARAM].getValue()));
        transport_.setLoopBounds(params[LOOP_START_PARAM].getValue(),
                                 params[LOOP_LEN_PARAM].getValue());

        for (int i = 0; i < 4; i++) {
            mixer_.setLevel(i, params[STEM_1_LEVEL_PARAM + i].getValue());
            // Latched on the press edge. configButton is momentary, so reading
            // it directly meant a stem was muted only while the button was
            // physically held and faded back in the instant it was released.
            if (muteTrigger_[i].process(params[STEM_1_MUTE_PARAM + i].getValue() > 0.5f)) {
                muted_[i] = !muted_[i];
            }
            mixer_.setMute(i, muted_[i]);
            lights[STEM_1_MUTE_LIGHT + i].setBrightness(muted_[i] ? 1.f : 0.f);
        }
        mixer_.setStemSelect((int)params[STEM_SELECT_PARAM].getValue());

        quantizer_.setGlideSeconds(params[QUANT_GLIDE_PARAM].getValue());
        quantizer_.setManualOverride(params[SCALE_MODE_PARAM].getValue() > 0.5f);
        quantizer_.setManualKey((int)params[ROOT_PARAM].getValue(),
                                (Quantizer::Scale)(int)params[SCALE_PARAM].getValue());

        extractor_.setWindowSamples((int)params[WT_WINDOW_PARAM].getValue());
        extractor_.setOffset(params[WT_OFFSET_PARAM].getValue() +
                             inputs[WT_OFFSET_CV_INPUT].getVoltage() / 5.f);
        oscillator_.setMorph(params[WT_MORPH_PARAM].getValue());
        oscillator_.setCoarse(params[WT_COARSE_PARAM].getValue());
        oscillator_.setFine(params[WT_FINE_PARAM].getValue());
        oscillator_.setLevel(params[WT_LEVEL_PARAM].getValue());

        gate_.setDecaySeconds(params[LPG_DECAY_PARAM].getValue());
        gate_.setColour(params[LPG_COLOUR_PARAM].getValue());
        gate_.setRestingLevel(params[LPG_LEVEL_PARAM].getValue());

        grains_.setSizeSeconds(params[GRAIN_SIZE_PARAM].getValue());
        const float densityCv = inputs[GRAIN_DENSITY_CV_INPUT].getVoltage() * 10.f;
        grains_.setDensityHz(params[GRAIN_DENSITY_PARAM].getValue() + densityCv);
        const float pitchCv = inputs[GRAIN_PITCH_CV_INPUT].getVoltage() * 12.f;
        grains_.setPitchSemitones(params[GRAIN_PITCH_PARAM].getValue() + pitchCv);
        grains_.setTexture(params[GRAIN_TEXTURE_PARAM].getValue());
        grains_.setSpread(params[GRAIN_SPREAD_PARAM].getValue());

        const float space = params[GRAIN_SPACE_PARAM].getValue();
        diffusion_.setDecaySeconds(0.2f + space * 6.f);
        diffusion_.setMix(space);
        diffusion_.setDamping(0.3f + space * 0.4f);
    }

    void handleRecording(const ProcessArgs& args) {
        const bool armed = params[REC_ARM_PARAM].getValue() > 0.5f;
        const bool trig = recTrigger_.process(inputs[REC_TRIG_INPUT].getVoltage());
        const float left = inputs[AUDIO_L_INPUT].getVoltage() / 5.f;
        const float right = inputs[AUDIO_R_INPUT].isConnected()
                                ? inputs[AUDIO_R_INPUT].getVoltage() / 5.f
                                : left;

        const int mode = (int)params[REC_MODE_PARAM].getValue();
        const float threshold = std::pow(10.f, params[REC_THRESH_PARAM].getValue() / 20.f);
        const bool armEdge = armPressed_.process(armed);

        if (!recording_) {
            // Threshold looks at BOTH channels. A source panned hard right
            // would otherwise never start the take, since the left channel
            // never crosses.
            const float level = std::max(std::fabs(left), std::fabs(right));
            const bool start = (mode == 1) ? (armed && level > threshold)
                                           : (trig || armEdge);
            if (start) {
                if (mode != 2) {
                    buffer_->clear();
                    // A new take supersedes the old one. Leaving the previous
                    // StemSet published meant the module kept playing the last
                    // recording all the way through the new one, and kept it
                    // for good if the new separation failed.
                    worker_.invalidate();
                }
                recording_ = true;
                recordedFrames_ = 0;
                overdubRead_ = 0;
            }
        } else if (trig || armEdge) {
            // A second trigger STOPS the take. Requiring the momentary arm
            // button to go low meant a panel start ended the moment the user
            // let go of it, and a trigger-started take could never be stopped
            // from the same input.
            stopRecording(args);
        }

        if (recording_) {
            if (mode == 2) {
                // Overdub MIXES rather than concatenating. Skipping the clear
                // and appending turned a partial buffer into two takes end to
                // end, and a full one into a slow replacement.
                float existingLeft = 0.f, existingRight = 0.f;
                buffer_->readFrame(overdubRead_, existingLeft, existingRight);
                buffer_->write(existingLeft + left, existingRight + right);
                const std::size_t stored = buffer_->framesStored();
                if (stored > 0) overdubRead_ = (overdubRead_ + 1) % stored;
            } else {
                buffer_->write(left, right);
            }
            recordedFrames_++;
            if (recordedFrames_ >= targetFrames(args)) stopRecording(args);
        }
    }

    void stopRecording(const ProcessArgs& args) {
        recording_ = false;
        submitSeparation(args);
    }

    void submitSeparation(const ProcessArgs& args) {
        // SWAPPED, not copied. Copying up to 32 seconds of stereo float here
        // meant two resizes and millions of samples inside one audio callback,
        // and submit() then copied it again under a mutex. Two buffers and a
        // pointer swap cost nothing: the worker owns the take it is separating
        // and process() records into the other one.
        const std::size_t frames = buffer_->framesStored();
        if (frames < 2048) return;
        pendingHandoff_ = true;
        handoffFrames_ = frames;
        handoffRate_ = (int)args.sampleRate;
        std::swap(buffer_, spare_);
        buffer_->clear();
    }

    /**
     * Complete a handoff. Called from process() but does the copy only when a
     * take has just ended, which is once per recording rather than per sample.
     *
     * The copy itself still happens here rather than on the worker, because
     * SeparationWorker::submit takes the snapshot under its own lock and that
     * contract is what keeps the worker from ever reading a live buffer. What
     * the swap removes is doing it from the buffer process() is writing.
     */
    void completeHandoff() {
        if (!pendingHandoff_) return;
        pendingHandoff_ = false;
        const std::size_t frames = std::min(handoffFrames_, spare_->framesStored());
        if (frames < 2048) return;
        scratchLeft_.resize(frames);
        scratchRight_.resize(frames);
        for (std::size_t i = 0; i < frames; i++) {
            spare_->readFrame(i, scratchLeft_[i], scratchRight_[i]);
        }
        worker_.submit(scratchLeft_.data(), scratchRight_.data(), frames, handoffRate_);
    }

    void advanceTransport(const ProcessArgs& args) {
        (void)args;
        // The playhead spans what was RECORDED, not the 32 second allocation.
        // Mapping it to capacity meant an eight second take played only in the
        // first quarter of the cycle and the rest was silence.
        const std::size_t stored = buffer_ ? buffer_->framesStored() : 0;
        transport_.setBufferFrames(stored);
        transport_.process(inputs[CLOCK_INPUT].getVoltage(),
                           inputs[RESET_INPUT].getVoltage());
    }

    /** Recording length the bars control asks for, in frames. */
    std::size_t targetFrames(const ProcessArgs& args) {
        const double bars = (double)params[BUF_LEN_PARAM].getValue();
        const double period = transport_.clockPeriodSeconds();
        // Sixteen clocks to a bar of 4/4, matching Transport's default.
        const double seconds = bars * 16.0 * period;
        const std::size_t wanted = (std::size_t)(seconds * args.sampleRate);
        const std::size_t cap = buffer_ ? buffer_->capacityFrames() : wanted;
        // The 32 second ceiling still wins. At very slow clocks sixteen bars is
        // over two minutes, which the spec caps rather than allocating for.
        return std::min(std::max<std::size_t>(wanted, 1), cap);
    }

    void runAnalysis(const StemSet* stems, const ProcessArgs& args) {
        // Analysis runs on a window, not per sample, so it is spread across
        // blocks: gathering samples here and estimating once the window is full
        // keeps the per-call cost flat.
        analysisWindow_[analysisFill_++] = mixer_.tap();
        if (analysisFill_ < analysisWindow_.size()) return;
        analysisFill_ = 0;

        const auto pitch = yin_.analyse(analysisWindow_.data(), analysisWindow_.size());
        detector_.addPitch(pitch.frequency, pitch.confidence, pitch.voiced);
        const auto key = detector_.detect();
        analysisActive_ = key.detected;

        if (!quantizer_.manualOverride()) {
            quantizer_.setDetectedKey(key.root,
                                      (key.mode == ScaleDetect::Mode::Major)
                                          ? Quantizer::Scale::Major
                                          : Quantizer::Scale::NaturalMinor);
        }
        (void)stems;
        (void)args;
    }

    float runVoice(const StemSet* stems, double playhead, const ProcessArgs& args) {
        extractor_.process(stems, mixer_.stemSelect(), playhead);
        oscillator_.offerFrame(extractor_.frame(), extractor_.frameCount());

        // Internal pitch source, active only while the jack is unpatched.
        // Without it an unpatched module sits on one note, which is not a
        // generative instrument.
        float cv = inputs[QUANT_CV_INPUT].isConnected()
                       ? inputs[QUANT_CV_INPUT].getVoltage()
                       : internalPitch_;
        quantized_ = quantizer_.process(cv);

        const float raw = oscillator_.process(quantized_);

        // Internal trigger, likewise. The loop grid fires it so the voice
        // articulates in time rather than never at all.
        if (inputs[LPG_TRIG_INPUT].isConnected()) {
            if (lpgTrigger_.process(inputs[LPG_TRIG_INPUT].getVoltage())) gate_.trigger();
        } else if (transport_.downbeat()) {
            gate_.trigger();
            advanceInternalPitch();
        }
        (void)args;
        return gate_.process(raw);
    }

    void advanceInternalPitch() {
        // A slow random walk over scale degrees, so pitch evolves rather than
        // sitting still. Bounded to two octaves so it cannot wander away.
        walk_ = walk_ * 1664525u + 1013904223u;
        const int step = (int)((walk_ >> 16) % 5) - 2;
        internalPitchSemitones_ =
            std::min(std::max(internalPitchSemitones_ + step, -12), 12);
        internalPitch_ = (float)internalPitchSemitones_ / 12.f;
    }

    Diffusion::Frame runGranular(const StemMixer::Frame& loop, float voice,
                                 const ProcessArgs& args) {
        const float balance = params[GRAIN_BALANCE_PARAM].getValue();
        // Both sides of the loop. Taking the left channel alone discarded the
        // right, so a right-heavy recording went quiet as the grain mix came
        // up, which is the opposite of what the control should do.
        const float loopMono = 0.5f * (loop.left + loop.right);
        const float mono = loopMono * (1.f - balance) + voice * balance;

        grainSource_[grainWrite_] = mono;
        grainWrite_ = (grainWrite_ + 1) % grainSource_.size();
        grains_.setReadPosition((double)grainWrite_);

        const auto cloud = grains_.process(grainSource_.data(), grainSource_.size());
        const auto wet = diffusion_.process(cloud.left, cloud.right);

        const float mix = params[GRAIN_MIX_PARAM].getValue();
        Diffusion::Frame out;
        out.left = loop.left + (wet.left - loop.left) * mix;
        out.right = loop.right + (wet.right - loop.right) * mix;
        (void)args;
        return out;
    }

    std::unique_ptr<RingBuffer> buffer_;
    std::unique_ptr<RingBuffer> spare_;
    bool pendingHandoff_ = false;
    std::size_t handoffFrames_ = 0;
    int handoffRate_ = 48000;
    std::size_t overdubRead_ = 0;
    Transport transport_{48000};
    SeparationWorker worker_;
    StemMixer mixer_{48000};
    WavetableExtract extractor_;
    WavetableOsc oscillator_;
    Quantizer quantizer_{48000};
    ScaleDetect detector_;
    Yin yin_{4096};
    LowpassGate gate_{48000};
    GrainEngine grains_{48000};
    Diffusion diffusion_{48000};

    dsp::SchmittTrigger recTrigger_, lpgTrigger_;
    dsp::BooleanTrigger muteTrigger_[4];
    bool muted_[4] = {false, false, false, false};
    dsp::BooleanTrigger armPressed_;
    dsp::PulseGenerator downbeatPulse_;

    bool recording_ = false;
    bool analysisActive_ = false;
    std::size_t recordedFrames_ = 0;
    float quantized_ = 0.f;
    float internalPitch_ = 0.f;
    int internalPitchSemitones_ = 0;
    uint32_t walk_ = 0x2545F491u;

    std::vector<float> scratchLeft_, scratchRight_;
    std::array<float, 4096> analysisWindow_{};
    std::size_t analysisFill_ = 0;

    // The granular stage reads a short rolling window of the mixed bus rather
    // than the loop buffer, so it follows whatever is actually sounding.
    std::array<float, 48000> grainSource_{};
    std::size_t grainWrite_ = 0;

    std::atomic<float> displayPeaks_[kDisplayColumns] = {};
    std::atomic<float> displayPlayhead_{0.f};
    int displayColumn_ = 0;
};

/** Waveform and playhead. Modelled on PixelProbe's canvas, which handles the
 *  NanoVG lifecycle correctly; ImagePanel leaks its handle and is not a model. */
struct StemsDisplay : LedDisplay {
    Stems* module = nullptr;

    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer != 1 || !module) return;

        // A snapshot the audio thread publishes, not the live buffer. Reading
        // the buffer from here raced with recording and could outlive it.
        float peaks[Stems::kDisplayColumns] = {};
        float playhead = 0.f;
        module->readDisplay(peaks, Stems::kDisplayColumns, &playhead);

        const float w = box.size.x, h = box.size.y;
        nvgBeginPath(args.vg);
        for (int x = 0; x < Stems::kDisplayColumns; x++) {
            const float amplitude = std::min(1.f, peaks[x]);
            const float px = w * x / (float)Stems::kDisplayColumns;
            nvgMoveTo(args.vg, px, h * 0.5f - amplitude * h * 0.45f);
            nvgLineTo(args.vg, px, h * 0.5f + amplitude * h * 0.45f);
        }
        nvgStrokeColor(args.vg, nvgRGBA(0x66, 0xdd, 0xff, 0xaa));
        nvgStrokeWidth(args.vg, 1.f);
        nvgStroke(args.vg);

        const float px = w * std::min(1.f, std::max(0.f, playhead));
        nvgBeginPath(args.vg);
        nvgMoveTo(args.vg, px, 0);
        nvgLineTo(args.vg, px, h);
        nvgStrokeColor(args.vg, nvgRGBA(0xff, 0xaa, 0x33, 0xdd));
        nvgStrokeWidth(args.vg, 1.5f);
        nvgStroke(args.vg);
    }
};

struct StemsWidget : ModuleWidget {
    explicit StemsWidget(Stems* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/Stems.svg")));

        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(
            Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        auto* display = createWidget<StemsDisplay>(mm2px(Vec(6.f, 12.f)));
        display->box.size = mm2px(Vec(160.f, 22.f));
        display->module = module;
        addChild(display);

        // Laid out in the order the signal travels: record and transport, then
        // the stem mixer, then analysis, voice, gate and granular.
        float x = 10.f;
        const float row1 = 44.f, row2 = 60.f, row3 = 76.f, row4 = 92.f, row5 = 108.f;

        addParam(createParamCentered<VCVButton>(mm2px(Vec(x, row1)), module, Stems::REC_ARM_PARAM));
        addChild(createLightCentered<SmallLight<RedLight>>(mm2px(Vec(x, row1 - 7.f)), module, Stems::REC_LIGHT));
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(x + 14.f, row1)), module, Stems::REC_MODE_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(x + 28.f, row1)), module, Stems::REC_THRESH_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(x + 42.f, row1)), module, Stems::BUF_LEN_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(x + 56.f, row1)), module, Stems::CLOCK_DIV_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(x + 70.f, row1)), module, Stems::LOOP_START_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(x + 84.f, row1)), module, Stems::LOOP_LEN_PARAM));

        for (int i = 0; i < 4; i++) {
            const float sx = x + i * 16.f;
            addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(sx, row2)), module, Stems::STEM_1_LEVEL_PARAM + i));
            addParam(createParamCentered<VCVButton>(mm2px(Vec(sx, row2 + 10.f)), module, Stems::STEM_1_MUTE_PARAM + i));
            addChild(createLightCentered<SmallLight<RedLight>>(mm2px(Vec(sx + 5.f, row2 + 10.f)), module, Stems::STEM_1_MUTE_LIGHT + i));
        }
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(x + 70.f, row2)), module, Stems::STEM_SELECT_PARAM));
        addChild(createLightCentered<SmallLight<GreenLight>>(mm2px(Vec(x + 82.f, row2)), module, Stems::ANALYSIS_ACTIVE_LIGHT));
        addChild(createLightCentered<SmallLight<YellowLight>>(mm2px(Vec(x + 90.f, row2)), module, Stems::SEPARATING_LIGHT));

        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(x, row3)), module, Stems::SCALE_MODE_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(x + 14.f, row3)), module, Stems::ROOT_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(x + 28.f, row3)), module, Stems::SCALE_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(x + 42.f, row3)), module, Stems::QUANT_GLIDE_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(x + 56.f, row3)), module, Stems::WT_WINDOW_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(x + 70.f, row3)), module, Stems::WT_OFFSET_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(x + 84.f, row3)), module, Stems::WT_MORPH_PARAM));

        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(x, row4)), module, Stems::WT_COARSE_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(x + 14.f, row4)), module, Stems::WT_FINE_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(x + 28.f, row4)), module, Stems::WT_LEVEL_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(x + 42.f, row4)), module, Stems::LPG_DECAY_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(x + 56.f, row4)), module, Stems::LPG_COLOUR_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(x + 70.f, row4)), module, Stems::LPG_LEVEL_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(x + 84.f, row4)), module, Stems::GRAIN_BALANCE_PARAM));

        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(x, row5)), module, Stems::GRAIN_SIZE_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(x + 14.f, row5)), module, Stems::GRAIN_DENSITY_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(x + 28.f, row5)), module, Stems::GRAIN_PITCH_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(x + 42.f, row5)), module, Stems::GRAIN_TEXTURE_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(x + 56.f, row5)), module, Stems::GRAIN_SPREAD_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(x + 70.f, row5)), module, Stems::GRAIN_SPACE_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(x + 84.f, row5)), module, Stems::GRAIN_MIX_PARAM));

        const float jackRow = 122.f;
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(x, jackRow)), module, Stems::AUDIO_L_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(x + 11.f, jackRow)), module, Stems::AUDIO_R_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(x + 22.f, jackRow)), module, Stems::CLOCK_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(x + 33.f, jackRow)), module, Stems::RESET_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(x + 44.f, jackRow)), module, Stems::REC_TRIG_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(x + 55.f, jackRow)), module, Stems::QUANT_CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(x + 66.f, jackRow)), module, Stems::LPG_TRIG_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(x + 77.f, jackRow)), module, Stems::GRAIN_DENSITY_CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(x + 88.f, jackRow)), module, Stems::GRAIN_PITCH_CV_INPUT));


        const float outRow = 112.f;
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(x + 99.f, outRow)), module, Stems::MAIN_L_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(x + 110.f, outRow)), module, Stems::MAIN_R_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(x + 99.f, jackRow)), module, Stems::LOOP_L_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(x + 110.f, jackRow)), module, Stems::LOOP_R_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(x + 121.f, outRow)), module, Stems::VOICE_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(x + 121.f, jackRow)), module, Stems::QUANT_CV_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(x + 132.f, jackRow)), module, Stems::DOWNBEAT_OUTPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(x + 88.f, outRow)), module, Stems::WT_OFFSET_CV_INPUT));
        addChild(createLightCentered<SmallLight<BlueLight>>(mm2px(Vec(x + 132.f, outRow)), module, Stems::DOWNBEAT_LIGHT));
    }
};

}  // namespace WiggleRoom

Model* modelStems = createModel<WiggleRoom::Stems, WiggleRoom::StemsWidget>("Stems");
