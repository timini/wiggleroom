#pragma once
/******************************************************************************
 * WavFile - RIFF/WAVE reader for Stems
 *
 * Framework-free: no rack.hpp, so it is directly unit-testable, which matters
 * more here than anywhere else in the core. This is the only component that
 * parses a file the user chose, so it is the only one whose input is genuinely
 * untrusted. Every field that indexes or sizes anything is checked against what
 * is actually there rather than believed.
 *
 * Deliberately not a general WAV library. It reads what a user is realistically
 * going to drop on a sampler:
 *
 *   PCM         8 bit unsigned, 16, 24 and 32 bit signed
 *   IEEE float  32 and 64 bit
 *   WAVE_FORMAT_EXTENSIBLE wrapping either of the above
 *
 * and refuses anything else with a message rather than guessing. Compressed
 * formats, ADPCM and the rest are out of scope: they need a decoder, and the
 * point of this file is to be small enough to audit.
 *
 * Channels above two are downmixed rather than rejected, because a five channel
 * field recording is a reasonable thing to want to granulate.
 ******************************************************************************/

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace WiggleRoom {
namespace stems {

class WavFile {
public:
    struct Result {
        bool ok = false;
        /** Empty when ok. Written for a user to read, not a developer. */
        std::string error;
        int sampleRate = 0;
        int channels = 0;
        /** Deinterleaved. For mono input both hold the same samples. */
        std::vector<float> left;
        std::vector<float> right;
        /** True when the file was longer than the cap and was cut short. */
        bool truncated = false;

        std::size_t frames() const { return left.size(); }
    };

    /**
     * Parse a WAVE file already in memory.
     *
     * @param data      File contents. May be anything at all.
     * @param size      Length of @p data in bytes.
     * @param maxFrames Stop after this many frames. A sampler has a fixed
     *                  buffer, and without a cap a large file decides how much
     *                  memory the host allocates.
     */
    static Result read(const unsigned char* data, std::size_t size, std::size_t maxFrames) {
        Result out;
        if (!data || size < 12) {
            out.error = "File is too short to be a WAV.";
            return out;
        }
        if (std::memcmp(data, "RIFF", 4) != 0 || std::memcmp(data + 8, "WAVE", 4) != 0) {
            out.error = "Not a WAV file (no RIFF/WAVE header).";
            return out;
        }

        // The RIFF size field is advisory: plenty of real files disagree with
        // their own length, usually by the eight header bytes. The actual
        // buffer length is what bounds every read below, so a wrong value here
        // cannot make anything unsafe.
        Format format;
        bool haveFormat = false;

        std::size_t at = 12;
        while (at + 8 <= size) {
            char id[5] = {0};
            std::memcpy(id, data + at, 4);
            const uint32_t declared = readU32(data + at + 4);
            const std::size_t body = at + 8;
            // Clamped to what exists. A chunk claiming to be larger than the
            // file is the most common way a malformed WAV tries to walk off the
            // end of the buffer.
            const std::size_t length = std::min<std::size_t>(declared, size - body);

            if (std::memcmp(id, "fmt ", 4) == 0) {
                if (!parseFormat(data + body, length, format, out.error)) return out;
                haveFormat = true;
            } else if (std::memcmp(id, "data", 4) == 0) {
                if (!haveFormat) {
                    out.error = "WAV has audio data before its format chunk.";
                    return out;
                }
                if (!decode(data + body, length, format, maxFrames, out)) return out;
                out.ok = true;
                out.sampleRate = format.sampleRate;
                out.channels = std::min(format.channels, 2);
                return out;
            }

            // Chunks are padded to an even length. Missing that is how a reader
            // ends up one byte out and reading chunk ids from the middle of
            // audio for the rest of the file.
            const std::size_t advance = length + (length & 1u);
            if (advance == 0) break;   // a zero-length chunk would loop forever
            at = body + advance;
        }

        out.error = haveFormat ? "WAV has no audio data." : "WAV has no format chunk.";
        return out;
    }

private:
    struct Format {
        int codec = 0;          // 1 PCM, 3 IEEE float
        int channels = 0;
        int sampleRate = 0;
        int bitsPerSample = 0;
    };

    static uint16_t readU16(const unsigned char* p) {
        return (uint16_t)(p[0] | (p[1] << 8));
    }
    static uint32_t readU32(const unsigned char* p) {
        return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
               ((uint32_t)p[3] << 24);
    }

    static bool parseFormat(const unsigned char* p, std::size_t length, Format& format,
                            std::string& error) {
        if (length < 16) {
            error = "WAV format chunk is truncated.";
            return false;
        }
        format.codec = readU16(p);
        format.channels = readU16(p + 2);
        format.sampleRate = (int)readU32(p + 4);
        format.bitsPerSample = readU16(p + 14);

        // EXTENSIBLE carries the real codec in a GUID after the extension size.
        if (format.codec == 0xFFFE) {
            if (length < 40) {
                error = "WAV extensible format chunk is truncated.";
                return false;
            }
            format.codec = readU16(p + 24);
        }

        if (format.channels < 1 || format.channels > 64) {
            error = "WAV has an unsupported channel count.";
            return false;
        }
        if (format.sampleRate < 1000 || format.sampleRate > 768000) {
            error = "WAV has an implausible sample rate.";
            return false;
        }
        if (format.codec == 1) {
            if (format.bitsPerSample != 8 && format.bitsPerSample != 16 &&
                format.bitsPerSample != 24 && format.bitsPerSample != 32) {
                error = "WAV uses an unsupported PCM bit depth.";
                return false;
            }
        } else if (format.codec == 3) {
            if (format.bitsPerSample != 32 && format.bitsPerSample != 64) {
                error = "WAV uses an unsupported float bit depth.";
                return false;
            }
        } else {
            error = "WAV uses a compressed format this module cannot read.";
            return false;
        }
        return true;
    }

    static bool decode(const unsigned char* p, std::size_t length, const Format& format,
                       std::size_t maxFrames, Result& out) {
        const std::size_t bytesPerSample = (std::size_t)format.bitsPerSample / 8;
        const std::size_t frameBytes = bytesPerSample * (std::size_t)format.channels;
        if (frameBytes == 0) {
            out.error = "WAV frame size is zero.";
            return false;
        }
        const std::size_t available = length / frameBytes;
        if (available == 0) {
            out.error = "WAV contains no complete audio frames.";
            return false;
        }
        const std::size_t frames = std::min(available, maxFrames);
        out.truncated = frames < available;

        out.left.resize(frames);
        out.right.resize(frames);
        for (std::size_t f = 0; f < frames; f++) {
            const unsigned char* frame = p + f * frameBytes;
            const float a = sampleAt(frame, 0, format);
            // Channels beyond the second are folded in rather than dropped, so
            // a surround field recording keeps its content instead of losing
            // everything but the front pair.
            float l = a;
            float r = (format.channels > 1) ? sampleAt(frame, 1, format) : a;
            if (format.channels > 2) {
                float rest = 0.f;
                for (int c = 2; c < format.channels; c++) {
                    rest += sampleAt(frame, c, format);
                }
                rest /= (float)(format.channels - 2);
                l = 0.5f * (l + rest);
                r = 0.5f * (r + rest);
            }
            // Never trust the decoded value either: a float WAV can legally
            // contain NaN, and one sample of it poisons HPSS, the mixer and
            // every oscillator frame after it.
            out.left[f] = std::isfinite(l) ? std::max(-4.f, std::min(4.f, l)) : 0.f;
            out.right[f] = std::isfinite(r) ? std::max(-4.f, std::min(4.f, r)) : 0.f;
        }
        return true;
    }

    static float sampleAt(const unsigned char* frame, int channel, const Format& format) {
        const std::size_t bytes = (std::size_t)format.bitsPerSample / 8;
        const unsigned char* p = frame + (std::size_t)channel * bytes;
        if (format.codec == 3) {
            if (format.bitsPerSample == 32) {
                float v = 0.f;
                std::memcpy(&v, p, 4);
                return v;
            }
            double v = 0.0;
            std::memcpy(&v, p, 8);
            return (float)v;
        }
        switch (format.bitsPerSample) {
            case 8:
                // Eight bit WAV is UNSIGNED, unlike every other depth. Reading
                // it as signed puts the waveform an octave of amplitude off
                // centre and turns silence into a full-scale offset.
                return ((float)p[0] - 128.f) / 128.f;
            case 16: {
                const int16_t v = (int16_t)readU16(p);
                return (float)v / 32768.f;
            }
            case 24: {
                int32_t v = (int32_t)((uint32_t)p[0] << 8 | (uint32_t)p[1] << 16 |
                                      (uint32_t)p[2] << 24);
                v >>= 8;   // sign-extend by shifting the 24 bits down
                return (float)v / 8388608.f;
            }
            case 32: {
                const int32_t v = (int32_t)readU32(p);
                return (float)v / 2147483648.f;
            }
            default:
                return 0.f;
        }
    }
};

}  // namespace stems
}  // namespace WiggleRoom
