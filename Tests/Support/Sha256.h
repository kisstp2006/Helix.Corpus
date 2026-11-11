#pragma once

#include <Helix/Core/Strings/String.h>

#include <cstring>

// SHA-256 (FIPS 180-4), to compare decoded pixels with the digests other tools wrote into the manifest.
namespace helix::corpus
{
    class Sha256
    {
    public:
        void Update(const void* data, usize size)
        {
            const u8* p = static_cast<const u8*>(data);
            total_ += size;
            while (size)
            {
                usize n = 64 - used_ < size ? 64 - used_ : size;
                std::memcpy(block_ + used_, p, n);
                used_ += n, p += n, size -= n;
                if (used_ == 64)
                    Compress(), used_ = 0;
            }
        }

        // Lower case hex.
        String Hex()
        {
            u64 bits = total_ * 8;
            u8 pad = 0x80;
            Update(&pad, 1);
            pad = 0;
            while (used_ != 56)
                Update(&pad, 1);
            u8 length[8];
            for (u32 i = 0; i < 8; i++)
                length[i] = u8(bits >> (56 - 8 * i));
            Update(length, 8);
            static constexpr char kDigits[] = "0123456789abcdef";
            String out;
            for (u32 v : h_)
                for (int s = 28; s >= 0; s -= 4)
                    out.Append(kDigits[(v >> s) & 15]);
            return out;
        }

    private:
        static u32 Rotate(u32 x, u32 n) { return (x >> n) | (x << (32 - n)); }

        void Compress()
        {
            static constexpr u32 k[64] = {
                0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be,
                0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa,
                0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85,
                0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
                0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f,
                0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
            u32 w[64];
            for (u32 i = 0; i < 16; i++)
                w[i] = u32(block_[i * 4]) << 24 | u32(block_[i * 4 + 1]) << 16 | u32(block_[i * 4 + 2]) << 8 | block_[i * 4 + 3];
            for (u32 i = 16; i < 64; i++)
            {
                u32 s0 = Rotate(w[i - 15], 7) ^ Rotate(w[i - 15], 18) ^ (w[i - 15] >> 3);
                u32 s1 = Rotate(w[i - 2], 17) ^ Rotate(w[i - 2], 19) ^ (w[i - 2] >> 10);
                w[i] = w[i - 16] + s0 + w[i - 7] + s1;
            }
            u32 a = h_[0], b = h_[1], c = h_[2], d = h_[3], e = h_[4], f = h_[5], g = h_[6], h = h_[7];
            for (u32 i = 0; i < 64; i++)
            {
                u32 t1 = h + (Rotate(e, 6) ^ Rotate(e, 11) ^ Rotate(e, 25)) + ((e & f) ^ (~e & g)) + k[i] + w[i];
                u32 t2 = (Rotate(a, 2) ^ Rotate(a, 13) ^ Rotate(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
                h = g, g = f, f = e, e = d + t1, d = c, c = b, b = a, a = t1 + t2;
            }
            h_[0] += a, h_[1] += b, h_[2] += c, h_[3] += d, h_[4] += e, h_[5] += f, h_[6] += g, h_[7] += h;
        }

        u32 h_[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
        u8 block_[64] = {};
        usize used_ = 0;
        u64 total_ = 0;
    };

    inline String Sha256Hex(const void* data, usize size)
    {
        Sha256 s;
        s.Update(data, size);
        return s.Hex();
    }
}
