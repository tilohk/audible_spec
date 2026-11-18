#include "ImageProcessor.h"

#include <algorithm>
#include <cmath>

namespace its
{
namespace pil
{
ByteImage toGrayscale (const RgbaImage& rgba)
{
    ByteImage out (rgba.height, rgba.width);
    const auto* src = rgba.pixels.data();
    for (size_t i = 0; i < out.data.size(); ++i, src += 4)
    {
        // #define L24(rgb) ((rgb)[0] * 19595 + (rgb)[1] * 38470 + (rgb)[2] * 7471 + 0x8000)
        const std::uint32_t l24 = src[0] * 19595u + src[1] * 38470u + src[2] * 7471u + 0x8000u;
        out.data[i] = (std::uint8_t)(l24 >> 16);
    }
    return out;
}

namespace
{
constexpr int precisionBits = 32 - 8 - 2;

double sincFilter (double x)
{
    if (x == 0.0)
        return 1.0;
    x = x * M_PI;
    return std::sin (x) / x;
}

double lanczosFilter (double x)
{
    // truncated sinc, support 3
    if (-3.0 <= x && x < 3.0)
        return sincFilter (x) * sincFilter (x / 3);
    return 0.0;
}

struct Coefficients
{
    int kernelSize = 0;
    std::vector<int> bounds;      // (xmin, count) per output pixel
    std::vector<std::int32_t> kk; // fixed-point weights, kernelSize per output pixel
};

// precompute_coeffs + normalize_coeffs_8bpc for box (0, inSize)
Coefficients precomputeCoefficients (int inSize, int outSize)
{
    const float in0 = 0.0f, in1 = (float)inSize; // the box is float in Pillow
    double scale = (double)(in1 - in0) / outSize;
    double filterscale = scale < 1.0 ? 1.0 : scale;
    const double support = 3.0 * filterscale;
    const int ksize = (int)std::ceil (support) * 2 + 1;

    Coefficients c;
    c.kernelSize = ksize;
    c.bounds.resize ((size_t)outSize * 2);
    std::vector<double> prekk ((size_t)outSize * (size_t)ksize, 0.0);

    for (int xx = 0; xx < outSize; ++xx)
    {
        const double center = in0 + (xx + 0.5) * scale;
        double ww = 0.0;
        const double ss = 1.0 / filterscale;
        int xmin = (int)(center - support + 0.5);
        if (xmin < 0)
            xmin = 0;
        int xmax = (int)(center + support + 0.5);
        if (xmax > inSize)
            xmax = inSize;
        xmax -= xmin;

        double* k = &prekk[(size_t)xx * (size_t)ksize];
        for (int x = 0; x < xmax; ++x)
        {
            const double w = lanczosFilter ((x + xmin - center + 0.5) * ss);
            k[x] = w;
            ww += w;
        }
        for (int x = 0; x < xmax; ++x)
            if (ww != 0.0)
                k[x] /= ww;

        c.bounds[(size_t)xx * 2] = xmin;
        c.bounds[(size_t)xx * 2 + 1] = xmax;
    }

    c.kk.resize (prekk.size());
    for (size_t i = 0; i < prekk.size(); ++i)
        c.kk[i] = prekk[i] < 0 ? (std::int32_t)(-0.5 + prekk[i] * (1 << precisionBits))
                               : (std::int32_t)(0.5 + prekk[i] * (1 << precisionBits));
    return c;
}

std::uint8_t clip8 (std::int32_t in)
{
    // clip8_lookups[in >> PRECISION_BITS]
    return (std::uint8_t)std::clamp (in >> precisionBits, 0, 255);
}
} // namespace

ByteImage resizeLanczos (const ByteImage& in, int outWidth, int outHeight)
{
    // ImagingResampleInner: horizontal pass first (only over the rows the
    // vertical pass needs), then vertical pass.
    const bool needHorizontal = outWidth != in.cols;
    const bool needVertical = outHeight != in.rows;

    const auto horiz = precomputeCoefficients (in.cols, outWidth);
    auto vert = precomputeCoefficients (in.rows, outHeight);

    const int yboxFirst = vert.bounds[0];
    const int yboxLast = vert.bounds[(size_t)outHeight * 2 - 2] + vert.bounds[(size_t)outHeight * 2 - 1];

    ByteImage current = in;
    if (needHorizontal)
    {
        for (int i = 0; i < outHeight; ++i)
            vert.bounds[(size_t)i * 2] -= yboxFirst;

        ByteImage temp (yboxLast - yboxFirst, outWidth);
        for (int yy = 0; yy < temp.rows; ++yy)
        {
            const std::uint8_t* src = in.rowPtr (yy + yboxFirst);
            for (int xx = 0; xx < outWidth; ++xx)
            {
                const int xmin = horiz.bounds[(size_t)xx * 2];
                const int xmax = horiz.bounds[(size_t)xx * 2 + 1];
                const std::int32_t* k = &horiz.kk[(size_t)xx * (size_t)horiz.kernelSize];
                std::int32_t ss0 = 1 << (precisionBits - 1);
                for (int x = 0; x < xmax; ++x)
                    ss0 += (std::int32_t)src[x + xmin] * k[x];
                temp (yy, xx) = clip8 (ss0);
            }
        }
        current = std::move (temp);
    }

    if (needVertical)
    {
        ByteImage out (outHeight, current.cols);
        for (int yy = 0; yy < outHeight; ++yy)
        {
            const int ymin = vert.bounds[(size_t)yy * 2];
            const int ymax = vert.bounds[(size_t)yy * 2 + 1];
            const std::int32_t* k = &vert.kk[(size_t)yy * (size_t)vert.kernelSize];
            for (int xx = 0; xx < current.cols; ++xx)
            {
                std::int32_t ss0 = 1 << (precisionBits - 1);
                for (int y = 0; y < ymax; ++y)
                    ss0 += (std::int32_t)current (y + ymin, xx) * k[y];
                out (yy, xx) = clip8 (ss0);
            }
        }
        current = std::move (out);
    }
    return current;
}

ByteImage invert (const ByteImage& in)
{
    ByteImage out = in;
    for (auto& v : out.data)
        v = (std::uint8_t)(255 - v);
    return out;
}

ByteImage contrast (const ByteImage& in, double factor)
{
    // degenerate = solid image of the rounded mean (ImageStat over the histogram)
    double sum = 0.0;
    for (auto v : in.data)
        sum += v;
    const int mean = (int)(sum / (double)in.data.size() + 0.5);

    // Image.blend(degenerate, image, factor) with C float alpha
    const float alpha = (float)factor;
    ByteImage out = in;
    if (alpha == 0.0f)
    {
        std::fill (out.data.begin(), out.data.end(), (std::uint8_t)mean);
        return out;
    }
    if (alpha == 1.0f)
        return out;

    for (auto& v : out.data)
    {
        // C: (int)in1 + alpha * ((int)in2 - (int)in1), evaluated in float
        const auto in1 = (float)mean, delta = (float)(v - mean);
        if (alpha >= 0 && alpha <= 1.0f)
        {
            v = (std::uint8_t)(in1 + alpha * delta);
        }
        else
        {
            const float temp = in1 + alpha * delta;
            v = temp <= 0.0f ? 0 : (temp >= 255.0f ? 255 : (std::uint8_t)temp);
        }
    }
    return out;
}

namespace
{
float gaussianBlurRadius (float radius, int passes)
{
    // _gaussian_blur_radius (Gwosdek et al. extended box filter)
    float sigma2, L, l, a;
    sigma2 = radius * radius / (float)passes;
    L = (float)std::sqrt (12.0 * sigma2 + 1.0);
    l = (float)std::floor ((L - 1.0) / 2.0);
    a = (2.0f * l + 1.0f) * (l * (l + 1.0f) - 3.0f * sigma2);
    a /= 6.0f * (sigma2 - (l + 1.0f) * (l + 1.0f));
    return l + a;
}

// ImagingLineBoxBlur8: one line, out-of-place
void lineBoxBlur8 (std::uint8_t* lineOut, const std::uint8_t* lineIn, int lastx, int radius, int edgeA,
                   int edgeB, std::uint32_t ww, std::uint32_t fw)
{
    std::uint32_t acc, bulk;
    auto moveAcc = [&] (int subtract, int add) { acc += (std::uint32_t)(lineIn[add] - lineIn[subtract]); };
    auto addFar = [&] (int left, int right)
    { bulk = (acc * ww) + (std::uint32_t)(lineIn[left] + lineIn[right]) * fw; };
    auto save = [&] (int x) { lineOut[x] = (std::uint8_t)((bulk + (1u << 23)) >> 24); };

    acc = (std::uint32_t)lineIn[0] * (std::uint32_t)(radius + 1);
    for (int x = 0; x < edgeA - 1; ++x)
        acc += lineIn[x];
    acc += (std::uint32_t)lineIn[lastx] * (std::uint32_t)(radius - edgeA + 1);

    if (edgeA <= edgeB)
    {
        for (int x = 0; x < edgeA; ++x)
        {
            moveAcc (0, x + radius);
            addFar (0, x + radius + 1);
            save (x);
        }
        for (int x = edgeA; x < edgeB; ++x)
        {
            moveAcc (x - radius - 1, x + radius);
            addFar (x - radius - 1, x + radius + 1);
            save (x);
        }
        for (int x = edgeB; x <= lastx; ++x)
        {
            moveAcc (x - radius - 1, lastx);
            addFar (x - radius - 1, lastx);
            save (x);
        }
    }
    else
    {
        for (int x = 0; x < edgeB; ++x)
        {
            moveAcc (0, x + radius);
            addFar (0, x + radius + 1);
            save (x);
        }
        for (int x = edgeB; x < edgeA; ++x)
        {
            moveAcc (0, lastx);
            addFar (0, lastx);
            save (x);
        }
        for (int x = edgeA; x <= lastx; ++x)
        {
            moveAcc (x - radius - 1, lastx);
            addFar (x - radius - 1, lastx);
            save (x);
        }
    }
}

// ImagingHorizontalBoxBlur (in place, line by line)
void horizontalBoxBlur (ByteImage& img, float floatRadius)
{
    const int radius = (int)floatRadius;
    const auto ww = (std::uint32_t)((float)(1 << 24) / (floatRadius * 2 + 1));
    const auto fw = ((1u << 24) - (std::uint32_t)(radius * 2 + 1) * ww) / 2;
    const int edgeA = std::min (radius + 1, img.cols);
    const int edgeB = std::max (img.cols - radius - 1, 0);

    std::vector<std::uint8_t> lineOut ((size_t)img.cols);
    for (int y = 0; y < img.rows; ++y)
    {
        lineBoxBlur8 (lineOut.data(), img.rowPtr (y), img.cols - 1, radius, edgeA, edgeB, ww, fw);
        std::copy (lineOut.begin(), lineOut.end(), img.rowPtr (y));
    }
}

ByteImage transpose (const ByteImage& in)
{
    ByteImage out (in.cols, in.rows);
    for (int y = 0; y < in.rows; ++y)
        for (int x = 0; x < in.cols; ++x)
            out (x, y) = in (y, x);
    return out;
}
} // namespace

ByteImage gaussianBlur (const ByteImage& in, double radius)
{
    if (radius == 0.0)
        return in;

    constexpr int passes = 3;
    const float r = gaussianBlurRadius ((float)radius, passes);

    // ImagingBoxBlur: n horizontal passes, then transpose, n passes, transpose back
    ByteImage out = in;
    if (r != 0.0f)
    {
        for (int i = 0; i < passes; ++i)
            horizontalBoxBlur (out, r);
        ByteImage t = transpose (out);
        for (int i = 0; i < passes; ++i)
            horizontalBoxBlur (t, r);
        out = transpose (t);
    }
    return out;
}

ByteImage mirror (const ByteImage& in)
{
    ByteImage out = in;
    for (int y = 0; y < out.rows; ++y)
        std::reverse (out.rowPtr (y), out.rowPtr (y) + out.cols);
    return out;
}
} // namespace pil

ByteImage resizeForSpectrogram (const RgbaImage& decoded, const RenderParams& params)
{
    return pil::resizeLanczos (pil::toGrayscale (decoded), params.width, params.specHeight());
}

FloatMatrix processImage (const ByteImage& resized, const RenderParams& params)
{
    ByteImage img = resized;
    if (params.invert)
        img = pil::invert (img);
    if (params.contrast != 1.0)
        img = pil::contrast (img, params.contrast);
    if (params.blur > 0)
        img = pil::gaussianBlur (img, params.blur);
    if (params.flipHorizontal)
        img = pil::mirror (img);

    // pixels = asarray(img).astype(float32) / 255.0 ; clip ; ** gamma ; flipud
    const float gamma = (float)params.gamma;
    FloatMatrix pixels (img.rows, img.cols);
    for (int y = 0; y < img.rows; ++y)
    {
        const int targetRow = params.flipVertical ? img.rows - 1 - y : y;
        for (int x = 0; x < img.cols; ++x)
        {
            float v = (float)img (y, x) / 255.0f;
            v = std::clamp (v, 0.0f, 1.0f);
            pixels (targetRow, x) = std::pow (v, gamma);
        }
    }
    return pixels;
}
} // namespace its
