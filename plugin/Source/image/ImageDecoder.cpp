#include "ImageDecoder.h"

#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>

namespace its
{
namespace
{
template <typename Ref> struct CFHolder
{
    Ref ref = nullptr;
    explicit CFHolder (Ref r) : ref (r) {}
    ~CFHolder()
    {
        if (ref != nullptr)
            CFRelease (ref);
    }
    CFHolder (const CFHolder&) = delete;
    CFHolder& operator= (const CFHolder&) = delete;
};

// Reads the image's own pixel bytes (no colour conversion) for the common
// 8-bit layouts. Returns false for layouts it does not handle.
bool readRawPixels (CGImageRef image, RgbaImage& out)
{
    const auto bitsPerComponent = CGImageGetBitsPerComponent (image);
    const auto bitsPerPixel = CGImageGetBitsPerPixel (image);
    if (bitsPerComponent != 8 ||
        (bitsPerPixel != 8 && bitsPerPixel != 16 && bitsPerPixel != 24 && bitsPerPixel != 32))
        return false;

    const auto bitmapInfo = CGImageGetBitmapInfo (image);
    const auto alphaInfo = (CGImageAlphaInfo)(bitmapInfo & kCGBitmapAlphaInfoMask);
    const auto byteOrder = bitmapInfo & kCGBitmapByteOrderMask;
    if (alphaInfo == kCGImageAlphaPremultipliedFirst || alphaInfo == kCGImageAlphaPremultipliedLast)
        return false; // stored values are premultiplied: use the drawing fallback
    if (bitmapInfo & kCGBitmapFloatComponents)
        return false;

    auto* colourSpace = CGImageGetColorSpace (image);
    const auto model = colourSpace != nullptr ? CGColorSpaceGetModel (colourSpace) : kCGColorSpaceModelRGB;
    const int channels = (int)(bitsPerPixel / 8);
    const bool isGray = model == kCGColorSpaceModelMonochrome;
    if (!isGray && model != kCGColorSpaceModelRGB)
        return false;

    CFHolder<CFDataRef> data (CGDataProviderCopyData (CGImageGetDataProvider (image)));
    if (data.ref == nullptr)
        return false;

    const auto* bytes = CFDataGetBytePtr (data.ref);
    const auto bytesPerRow = CGImageGetBytesPerRow (image);
    const bool alphaFirst = alphaInfo == kCGImageAlphaFirst || alphaInfo == kCGImageAlphaNoneSkipFirst;
    const bool hasAlpha = alphaInfo == kCGImageAlphaFirst || alphaInfo == kCGImageAlphaLast;
    const bool littleEndian32 = byteOrder == kCGBitmapByteOrder32Little;

    for (int y = 0; y < out.height; ++y)
    {
        const auto* row = bytes + (size_t)y * bytesPerRow;
        auto* dst = out.pixels.data() + (size_t)y * (size_t)out.width * 4;
        for (int x = 0; x < out.width; ++x, dst += 4)
        {
            const auto* p = row + (size_t)x * (size_t)channels;
            std::uint8_t c[4] = {p[0], channels > 1 ? p[1] : (std::uint8_t)0,
                                 channels > 2 ? p[2] : (std::uint8_t)0,
                                 channels > 3 ? p[3] : (std::uint8_t)0};
            if (littleEndian32 && channels == 4)
                std::swap (c[0], c[3]), std::swap (c[1], c[2]);

            if (isGray)
            {
                const std::uint8_t g = (channels == 2 && alphaFirst) ? c[1] : c[0];
                const std::uint8_t a = channels == 2 && hasAlpha ? (alphaFirst ? c[0] : c[1]) : 255;
                dst[0] = dst[1] = dst[2] = g;
                dst[3] = a;
            }
            else if (channels == 3)
            {
                dst[0] = c[0], dst[1] = c[1], dst[2] = c[2], dst[3] = 255;
            }
            else if (channels == 4)
            {
                const int o = alphaFirst ? 1 : 0;
                dst[0] = c[o], dst[1] = c[o + 1], dst[2] = c[o + 2];
                dst[3] = hasAlpha ? c[alphaFirst ? 0 : 3] : 255;
            }
            else
            {
                return false;
            }
        }
    }
    return true;
}

// Fallback: draw into an RGBA context in the image's own colour space, then un-premultiply.
bool drawPixels (CGImageRef image, RgbaImage& out)
{
    CGColorSpaceRef space = CGImageGetColorSpace (image);
    CFHolder<CGColorSpaceRef> ownedSpace (nullptr);
    if (space == nullptr || CGColorSpaceGetModel (space) != kCGColorSpaceModelRGB)
    {
        ownedSpace.ref = CGColorSpaceCreateWithName (kCGColorSpaceSRGB);
        space = ownedSpace.ref;
    }

    CFHolder<CGContextRef> context (
        CGBitmapContextCreate (out.pixels.data(), (size_t)out.width, (size_t)out.height, 8,
                               (size_t)out.width * 4, space, (CGBitmapInfo)kCGImageAlphaPremultipliedLast));
    if (context.ref == nullptr)
        return false;

    CGContextSetBlendMode (context.ref, kCGBlendModeCopy);
    CGContextDrawImage (context.ref, CGRectMake (0, 0, out.width, out.height), image);

    for (size_t i = 0; i < out.pixels.size(); i += 4)
    {
        const int a = out.pixels[i + 3];
        if (a != 0 && a != 255)
            for (int c = 0; c < 3; ++c)
                out.pixels[i + (size_t)c] =
                    (std::uint8_t)std::min (255, (out.pixels[i + (size_t)c] * 255 + a / 2) / a);
    }
    return true;
}

std::optional<RgbaImage> decodeFromSource (CGImageSourceRef source, juce::String& error)
{
    if (source == nullptr || CGImageSourceGetCount (source) == 0)
    {
        error = "Unsupported or unreadable image file.";
        return std::nullopt;
    }

    CFHolder<CGImageRef> image (CGImageSourceCreateImageAtIndex (source, 0, nullptr));
    if (image.ref == nullptr)
    {
        error = "Could not decode the image.";
        return std::nullopt;
    }

    RgbaImage out;
    out.width = (int)CGImageGetWidth (image.ref);
    out.height = (int)CGImageGetHeight (image.ref);
    out.pixels.assign ((size_t)out.width * (size_t)out.height * 4, 0);

    if (readRawPixels (image.ref, out) || drawPixels (image.ref, out))
        return out;

    error = "Unsupported pixel format.";
    return std::nullopt;
}
} // namespace

std::optional<RgbaImage> decodeImageFile (const juce::File& file, juce::String& error)
{
    const auto path = file.getFullPathName().toStdString();
    CFHolder<CFURLRef> url (CFURLCreateFromFileSystemRepresentation (
        nullptr, reinterpret_cast<const UInt8*> (path.c_str()), (CFIndex)path.size(), false));
    CFHolder<CGImageSourceRef> source (url.ref != nullptr ? CGImageSourceCreateWithURL (url.ref, nullptr)
                                                          : nullptr);
    return decodeFromSource (source.ref, error);
}

std::optional<RgbaImage> decodeImageData (const void* data, size_t size, juce::String& error)
{
    CFHolder<CFDataRef> cfData (CFDataCreate (nullptr, static_cast<const UInt8*> (data), (CFIndex)size));
    CFHolder<CGImageSourceRef> source (
        cfData.ref != nullptr ? CGImageSourceCreateWithData (cfData.ref, nullptr) : nullptr);
    return decodeFromSource (source.ref, error);
}
} // namespace its
