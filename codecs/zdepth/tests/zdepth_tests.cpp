// Copyright (c) 2019 Christopher A. Taylor.  All rights reserved.

#include "zdepth_lossless.hpp"
#include "zdepth_lossy.hpp"

#include <core.hpp>
#include <core_logging.hpp>
using namespace core;


//------------------------------------------------------------------------------
// RVL

// RVL library for performance baseline

// Paper: https://www.microsoft.com/en-us/research/publication/fast-lossless-depth-image-compression/
// Video presentation: https://www.youtube.com/watch?v=WYU2upBs2hA

// RVL author suggests that H.264 is a bad idea to use.
// But it seems like some masking can be used to avoid messing up the edges...

// Effective Compression of Range Data Streams for Remote Robot Operations using H.264
// http://www2.informatik.uni-freiburg.de/~stachnis/pdf/nenci14iros.pdf

// Adapting Standard Video Codecs for Depth Streaming
// http://reality.cs.ucl.ac.uk/projects/depth-streaming/depth-streaming.pdf

inline void EncodeVLE(int* &pBuffer, int& word, int& nibblesWritten, int value)
{
    do
    {
        int nibble = value & 0x7; // lower 3 bits
        if (value >>= 3) {
            nibble |= 0x8; // more to come
        }
        word <<= 4;
        word |= nibble;
        if (++nibblesWritten == 8) // output word
        {
            *pBuffer++ = word;
            nibblesWritten = 0;
            word = 0;
        }
    } while (value);
}

inline int DecodeVLE(int* &pBuffer, int& word, int& nibblesWritten)
{
    unsigned int nibble;
    int value = 0, bits = 29;
    do
    {
        if (!nibblesWritten)
        {
            word = *pBuffer++; // load word
            nibblesWritten = 8;
        }
        nibble = word & 0xf0000000;
        value |= (nibble << 1) >> bits;
        word <<= 4;
        nibblesWritten--;
        bits -= 3;
    } while (nibble & 0x80000000);
    return value;
}

int CompressRVL(short* input, char* output, int numPixels)
{
    int word, nibblesWritten;
    int *pBuffer;
    int *buffer = pBuffer = (int*)output;
    nibblesWritten = 0;
    short *end = input + numPixels;
    short previous = 0;
    while (input != end)
    {
        int zeros = 0, nonzeros = 0;
        for (; (input != end) && !*input; input++, zeros++);
        EncodeVLE(pBuffer, word, nibblesWritten, zeros); // number of zeros
        for (short* p = input; (p != end) && *p++; nonzeros++);
        EncodeVLE(pBuffer, word, nibblesWritten, nonzeros); // number of nonzeros
        for (int i = 0; i < nonzeros; i++)
        {
            short current = *input++;
            int delta = current - previous;
            int positive = (delta << 1) ^ (delta >> 31);
            EncodeVLE(pBuffer, word, nibblesWritten, positive); // nonzero value
            previous = current;
        }
    }
    if (nibblesWritten) // last few values
    {
        *pBuffer++ = word << 4 * (8 - nibblesWritten);
    }
    return int((char*)pBuffer - (char*)buffer); // num bytes
}

void DecompressRVL(char* input, short* output, int numPixels)
{
    int word, nibblesWritten;
    int *pBuffer = pBuffer = (int*)input;
    nibblesWritten = 0;
    short current, previous = 0;
    int numPixelsToDecode = numPixels;
    while (numPixelsToDecode)
    {
        int zeros = DecodeVLE(pBuffer, word, nibblesWritten); // number of zeros
        numPixelsToDecode -= zeros;
        for (; zeros; zeros--) {
            *output++ = 0;
        }
        int nonzeros = DecodeVLE(pBuffer, word, nibblesWritten); // number of nonzeros
        numPixelsToDecode -= nonzeros;
        for (; nonzeros; nonzeros--)
        {
            int positive = DecodeVLE(pBuffer, word, nibblesWritten); // nonzero value
            int delta = (positive >> 1) ^ -(positive & 1);
            current = (short)(previous + delta);
            *output++ = current;
            previous = current;
        }
    }
}


//------------------------------------------------------------------------------
// Test Vectors

#include "test_vectors.inl"


//------------------------------------------------------------------------------
// Test Application

#include <iostream>
using namespace std;

static lossless::DepthCompressor compressor0, decompressor0;
static lossy::DepthCompressor compressor1, decompressor1;
static lossy::DepthCompressor compressor2, decompressor2;

static bool CompareFrames(size_t n, const uint16_t* depth, const uint16_t* frame)
{
    std::vector<unsigned> error_hist(512);

    for (int i = 0; i < n; ++i) {
        const int x = lossless::AzureKinectQuantizeDepth(depth[i]);
        const int y = lossless::AzureKinectQuantizeDepth(frame[i]);
        unsigned z = std::abs(x - y);
        if (z == 0) {
            continue;
        }
        if (z >= 512) {
            z = 511;
        }
        error_hist[z] += z;
    }
#if 1
    for (int i = 0; i < 512; ++i) {
        if (error_hist[i]) {
            cout << "Hist: " << i << " : " << error_hist[i] << endl;
        }
    }
#endif
    return true;
}

static void LossyGraphResult(size_t /*n*/, const uint16_t* depth, const uint16_t* frame)
{
    const int width = 200;
    const int height = 280;
    const int stride = 320;
    const int offx = 100;
    const int offy = 0;

    cout << "Error plot:" << endl;
    for (int yy = 0; yy < height; ++yy) {
        for (int xx = 0; xx < width; ++xx) {
            const int i = xx + offx + (yy + offy) * stride;
            const int x = lossy::AzureKinectQuantizeDepth(depth[i]);
            const int y = lossy::AzureKinectQuantizeDepth(frame[i]);

            unsigned z = std::abs(x - y);
            if (z == 0) {
                cout << " ";
            }
            else if (z < 16) {
                cout << ".";
            }
            else {
                cout << "!";
            }
        }
        cout << endl;
    }

#if 0
    cout << "High bits plot:" << endl;
    for (int yy = 0; yy < height; ++yy) {
        for (int xx = 0; xx < width; ++xx) {
            const int i = xx + offx + (yy + offy) * stride;
            const int x = AzureKinectQuantizeDepth(frame[i]);

            if (x == 0) {
                cout << " ";
            }
            else if (x & 1) {
                cout << ".";
            }
            else {
                cout << ",";
            }
        }
        cout << endl;
    }
#endif
}

static bool LossyCompareFrames(size_t n, const uint16_t* depth, const uint16_t* frame)
{
    std::vector<unsigned> error_hist(256);

    for (int i = 0; i < n; ++i) {
        const int x = lossy::AzureKinectQuantizeDepth(depth[i]);
        const int y = lossy::AzureKinectQuantizeDepth(frame[i]);

        unsigned z = std::abs(x - y);
        if (z == 0) {
            continue;
        }
        if (z > 255) {
            z = 255;
        }
        error_hist[z]++;
    }
#if 1
    for (int i = 0; i < 256; ++i) {
        if (error_hist[i]) {
            cout << "Error hist: " << i << " : " << error_hist[i] << endl;
        }
    }
#endif

    //LossyGraphResult(n, depth, frame);

    return true;
}

bool TestFrame(const uint16_t* frame, bool keyframe)
{
    // Lossy
    {
        std::vector<uint16_t> depth;
        int width, height;
        std::vector<uint8_t> compressed;

        const uint64_t t0 = GetTimeUsec();

        compressor1.Compress(Width, Height, true, 30, frame, compressed, keyframe);

        const uint64_t t1 = GetTimeUsec();

        lossy::DepthResult result = decompressor1.Decompress(compressed, width, height, depth);

        const uint64_t t2 = GetTimeUsec();

        if (result != lossy::DepthResult::Success) {
            cout << "Failed: Lossy decompressor.Decompress returned " << lossy::DepthResultString(result) << endl;
            return false;
        }
        if (width != Width ||
            height != Height)
        {
            cout << "Lossy decompression failed: Resolution" << endl;
            return false;
        }

        if (!LossyCompareFrames(depth.size(), depth.data(), frame)) {
            cout << "Lossy decompression result corrupted" << endl;
            return false;
        }

        const unsigned original_bytes = Width * Height * 2;
        cout << endl;
        cout << "Lossy Zdepth Compression: " << original_bytes << " bytes -> " << compressed.size() << 
            " bytes (ratio = " << original_bytes / (float)compressed.size() << ":1) ("
            << (compressed.size() * 30 * 8) / 1000000.f << " Mbps @ 30 FPS)" << endl;
        cout << "Lossy Zdepth Speed: Compressed in " << (t1 - t0) / 1000.f << " msec. Decompressed in " << (t2 - t1) / 1000.f << " msec" << endl;
    }

    // Lossless
    {
        int width, height;
        std::vector<uint16_t> depth;
        std::vector<uint8_t> compressed;

        const uint64_t t0 = GetTimeUsec();

        compressor0.Compress(Width, Height, frame, compressed, keyframe);

        const uint64_t t1 = GetTimeUsec();

        lossless::DepthResult result = decompressor0.Decompress(compressed, width, height, depth);

        const uint64_t t2 = GetTimeUsec();

        if (result != lossless::DepthResult::Success) {
            cout << "Failed: Lossless decompressor.Decompress returned " << lossless::DepthResultString(result) << endl;
            return false;
        }
        if (width != Width ||
            height != Height)
        {
            cout << "Lossless decompression failed: Resolution" << endl;
            return false;
        }

        if (!CompareFrames(depth.size(), depth.data(), frame)) {
            cout << "Lossless decompression result corrupted" << endl;
            return false;
        }

        const unsigned original_bytes = Width * Height * 2;
        cout << endl;
        cout << "Lossless Zdepth Compression: " << original_bytes << " bytes -> " << compressed.size() << 
            " bytes (ratio = " << original_bytes / (float)compressed.size() << ":1) ("
            << (compressed.size() * 30 * 8) / 1000000.f << " Mbps @ 30 FPS)" << endl;
        cout << "Lossless Zdepth Speed: Compressed in " << (t1 - t0) / 1000.f << " msec. Decompressed in " << (t2 - t1) / 1000.f << " msec" << endl;
    }

    // RVL
    {
        std::vector<uint16_t> depth;
        std::vector<uint8_t> compressed;

        const int n = Width * Height;
        std::vector<uint16_t> quantized(n);
        compressed.resize(n * 3);

        const uint64_t t3 = GetTimeUsec();
        lossless::QuantizeDepthImage(Width, Height, frame, quantized);
        const int compressed_bytes = CompressRVL((short*)quantized.data(), (char*)compressed.data(), n);
        compressed.resize(compressed_bytes);
        const uint64_t t4 = GetTimeUsec();

        std::vector<uint8_t> recompressed;
        std::vector<uint8_t> decompressed;

        const uint64_t t5 = GetTimeUsec();
        lossless::ZstdCompress(compressed, recompressed);
        const uint64_t t6 = GetTimeUsec();
        lossless::ZstdDecompress(recompressed.data(), (int)recompressed.size(), (int)compressed.size(), decompressed);
        const uint64_t t7 = GetTimeUsec();
        quantized.resize(n * 2);
        DecompressRVL((char*)decompressed.data(), (short*)quantized.data(), n);
        lossless::DequantizeDepthImage(Width, Height, quantized.data(), depth);
        const uint64_t t8 = GetTimeUsec();

        for (int i = 0; i < n; ++i) {
            if (lossless::AzureKinectQuantizeDepth(depth[i]) != lossless::AzureKinectQuantizeDepth(frame[i])) {
                cout << "RVL bug" << endl;
                return false;
            }
        }

        cout << endl;
        const unsigned original_bytes = Width * Height * 2;
        cout << "Quantization+RVL+Zstd Compression: " << original_bytes << " bytes -> " << recompressed.size() << 
            " bytes (ratio = " << original_bytes / (float)recompressed.size() << ":1) ("
            << (recompressed.size() * 30 * 8) / 1000000.f << " Mbps @ 30 FPS)" << endl;
        cout << "Quantization+RVL+Zstd Speed: Compressed in " << (t6 - t5 + t4 - t3) / 1000.f << " msec. Decompressed in " << (t8 - t6) / 1000.f << " msec" << endl;

        cout << endl;
        cout << "Quantization+RVL Compression: " << original_bytes << " bytes -> " << compressed.size() << 
            " bytes (ratio = " << original_bytes / (float)compressed.size() << ":1) ("
            << (compressed.size() * 30 * 8) / 1000000.f << " Mbps @ 30 FPS)" << endl;
        cout << "Quantization+RVL Speed: Compressed in " << (t4 - t3) / 1000.f << " msec. Decompressed in " << (t8 - t7) / 1000.f << " msec" << endl;
    }

    return true;
}

bool TestPattern(const uint16_t* frame0, const uint16_t* frame1)
{
    cout << endl;
    cout << "===================================================================" << endl;
    cout << "+ Test: Frame 0 Keyframe=true compression" << endl;
    cout << "===================================================================" << endl;

    if (!TestFrame(frame0, true)) {
        cout << "Failure: frame0 failed";
        return false;
    }

    cout << endl;
    cout << "===================================================================" << endl;
    cout << "+ Test: Frame 1 Keyframe=false compression" << endl;
    cout << "===================================================================" << endl;

    if (!TestFrame(frame1, false)) {
        cout << "Failure: frame1 failed";
        return false;
    }
    return true;
}

int main(int argc, char* argv[])
{
    (void)(argc); (void)(argv);

    SetCurrentThreadName("Main");

    SetupAsyncDiskLog("zdepth_tests.txt");

    cout << endl;
    cout << "-------------------------------------------------------------------" << endl;
    cout << "Test vector: Room" << endl;
    cout << "-------------------------------------------------------------------" << endl;

    if (!TestPattern(TestVector0_Room0, TestVector0_Room1)) {
        cout << "Test failure: Room test vector" << endl;
        return -1;
    }

    cout << endl;
    cout << "-------------------------------------------------------------------" << endl;
    cout << "Test vector: Ceiling" << endl;
    cout << "-------------------------------------------------------------------" << endl;

    if (!TestPattern(TestVector1_Ceiling0, TestVector1_Ceiling1)) {
        cout << "Test failure: Ceiling test vector" << endl;
        return -2;
    }

    cout << endl;
    cout << "-------------------------------------------------------------------" << endl;
    cout << "Test vector: Person" << endl;
    cout << "-------------------------------------------------------------------" << endl;

    if (!TestPattern(TestVector2_Person0, TestVector2_Person1)) {
        cout << "Test failure: Person test vector" << endl;
        return -3;
    }
    cout << endl;

    cout << "Test success" << endl;
    return 0;
}
