#include <Support/Corpus.h>

#include <Helix/Audio/Decoding/AudioDecoder.h>
#include <Helix/Testing/Test.h>

#include <cmath>
#include <cstdio>

using namespace helix;
using namespace helix::corpus;
using namespace helix::audio;

namespace
{
    bool DecodeFile(StringView file, AudioData& out, String& error)
    {
        Array<u8> bytes;
        if (!Read(file, bytes))
        {
            error = "cannot read the file";
            return false;
        }
        return DecodeAudio(Span<const u8>(bytes), out, error, Extension(file));
    }

    // The frame offset of `ours` against `theirs` (ours[i + offset] ~ theirs[i]) with the least squared difference, within
    // +-range frames, on the first channel.
    i64 BestOffset(const AudioData& ours, const AudioData& theirs, i64 range)
    {
        u32 ch = ours.format.channels;
        i64 n = i64(ours.samples.Size() / ch), m = i64(theirs.samples.Size() / ch);
        i64 best = 0;
        double bestError = 1e300;
        for (i64 offset = -range; offset <= range; offset++)
        {
            double e = 0;
            i64 count = 0;
            for (i64 i = 0; i < m; i += 4)
            {
                i64 j = i + offset;
                if (j < 0 || j >= n)
                    continue;
                double d = ours.samples[usize(j) * ch] - theirs.samples[usize(i) * ch];
                e += d * d, count++;
            }
            if (count > m / 8 && e / double(count) < bestError)
                bestError = e / double(count), best = offset;
        }
        return best;
    }
}

// Every sound against ffmpeg's samples: PCM, A-law, mu-law, ADPCM and FLAC exactly (as floats), MP3 and Vorbis within
// an SNR bound after aligning the encoder delay each decoder removes in its own way.
HX_TEST(Corpus, Audio)
{
    const Manifest& m = Manifest::Get();
    u32 list = m.Section("audio");
    HX_REQUIRE(m.Count(list) > 20);
    u32 exact = 0, close = 0;
    for (u32 i = 0; i < m.Count(list); i++)
    {
        u32 e = m.Item(list, i);
        StringView file = m.Text(e, "file");
        AudioData ours, theirs;
        String error;
        bool ok = DecodeFile(file, ours, error);
        if (!ok)
            std::printf("    %s: not decoded: %s\n", String(file).CStr(), error.CStr());
        HX_CHECK(ok);
        HX_REQUIRE(DecodeFile(m.Text(e, "reference"), theirs, error));
        if (!ok)
            continue;
        u32 rate = u32(m.Number(e, "rate")), channels = u32(m.Number(e, "channels"));
        bool format = ours.format.sampleRate == rate && ours.format.channels == channels && theirs.format.channels == channels;
        if (!format)
            std::printf("    %s: %u Hz %u channels, ffmpeg %u Hz %u channels\n", String(file).CStr(), ours.format.sampleRate, ours.format.channels, rate,
                        channels);
        HX_CHECK(format);
        if (!format)
            continue;
        usize oursFrames = ours.samples.Size() / channels, theirFrames = theirs.samples.Size() / channels;
        if (!m.Flag(e, "lossy"))
        {
            double worst = 0;
            bool sameLength = oursFrames == theirFrames;
            // ADPCM: the fact chunk gives the length (the decoders keep it), ffmpeg decodes the last block whole.
            if (m.Text(e, "codec").Contains(StringView("adpcm")) && oursFrames < theirFrames && theirFrames - oursFrames < 2048)
            {
                sameLength = true;
                theirs.samples.Resize(ours.samples.Size());
            }
            for (usize s = 0; sameLength && s < ours.samples.Size(); s++)
                worst = std::fmax(worst, std::fabs(double(ours.samples[s]) - theirs.samples[s]));
            // ffmpeg and the decoders scale integers to floats the same way (2^-(bits-1)); 2^-24 absorbs float rounding.
            bool same = sameLength && worst <= 1.0 / (1 << 24);
            if (!same)
                std::printf("    %s: %zu frames (ffmpeg %zu), largest difference %g\n", String(file).CStr(), oursFrames, theirFrames, worst);
            HX_CHECK(same);
            exact += same;
            continue;
        }
        i64 offset = BestOffset(ours, theirs, 2400);
        double signal = 0, noise = 0;
        for (usize f = 0; f < theirFrames; f++)
        {
            i64 j = i64(f) + offset;
            if (j < 0 || usize(j) >= oursFrames)
                continue;
            for (u32 c = 0; c < channels; c++)
            {
                double a = theirs.samples[f * channels + c], b = ours.samples[usize(j) * channels + c];
                signal += a * a, noise += (a - b) * (a - b);
            }
        }
        double snr = noise > 0 ? 10 * std::log10(signal / noise) : 999;
        // Two conforming decoders of one stream differ by rounding only; length within one frame of either codec.
        bool within = snr >= 60 && (oursFrames > theirFrames ? oursFrames - theirFrames : theirFrames - oursFrames) <= 2304 + usize(offset < 0 ? -offset : offset);
        std::printf("    %s: offset %lld, %zu frames (ffmpeg %zu), SNR %.1f dB%s\n", String(file).CStr(), (long long)offset, oursFrames, theirFrames, snr,
                    within ? "" : "  <- outside");
        HX_CHECK(within);
        close += within;
    }
    std::printf("    %u sounds exact, %u within the lossy bounds\n", exact, close);
}
