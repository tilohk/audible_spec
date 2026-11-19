// Image decoding on Windows and Linux (macOS uses ImageIO, see ImageDecoder.cpp).
//  - HEIC/HEIF: libheif (with the libde265 HEVC decoder), when built with ITS_HAVE_LIBHEIF.
//  - PNG, JPEG, GIF: JUCE's decoders. JUCE stores pixels premultiplied by alpha; the
//    transparent test PNG still matches Python exactly after the resize (measured).

#include "ImageDecoder.h"

#include <juce_graphics/juce_graphics.h>

#if ITS_HAVE_LIBHEIF
#include <libheif/heif.h>
#endif

#include <cstring>

namespace its
{
namespace
{
bool looksLikeHeif (const std::uint8_t* bytes, size_t size)
{
    if (size < 12 || std::memcmp (bytes + 4, "ftyp", 4) != 0)
        return false;
    const juce::String brand (reinterpret_cast<const char*> (bytes + 8), 4);
    return juce::StringArray{"heic", "heix", "heim", "heis", "hevc", "hevx", "mif1", "msf1"}.contains (brand);
}

#if ITS_HAVE_LIBHEIF
template <typename T, void (*Release) (T*)> struct HeifHolder
{
    T* ptr = nullptr;
    ~HeifHolder()
    {
        if (ptr != nullptr)
            Release (ptr);
    }
};

std::optional<RgbaImage> decodeHeif (const void* data, size_t size, juce::String& error)
{
    static const bool initialised = [] { return heif_init (nullptr).code == heif_error_Ok; }();
    juce::ignoreUnused (initialised);

    HeifHolder<heif_context, heif_context_free> context{heif_context_alloc()};
    if (context.ptr == nullptr ||
        heif_context_read_from_memory_without_copy (context.ptr, data, size, nullptr).code != heif_error_Ok)
    {
        error = "Unreadable HEIC/HEIF file.";
        return std::nullopt;
    }

    HeifHolder<heif_image_handle, heif_image_handle_release> handle;
    if (heif_context_get_primary_image_handle (context.ptr, &handle.ptr).code != heif_error_Ok)
    {
        error = "The HEIC/HEIF file has no image.";
        return std::nullopt;
    }

    // Like ImageIO on macOS: rotation/mirroring metadata is not applied.
    HeifHolder<heif_decoding_options, heif_decoding_options_free> options{heif_decoding_options_alloc()};
    options.ptr->ignore_transformations = 1;

    HeifHolder<heif_image, heif_image_release> image;
    if (heif_decode_image (handle.ptr, &image.ptr, heif_colorspace_RGB, heif_chroma_interleaved_RGBA,
                           options.ptr)
            .code != heif_error_Ok)
    {
        error = "Could not decode the HEIC/HEIF image (no HEVC decoder?).";
        return std::nullopt;
    }

    RgbaImage out;
    out.width = heif_image_get_primary_width (image.ptr);
    out.height = heif_image_get_primary_height (image.ptr);
#if LIBHEIF_HAVE_VERSION(1, 20, 0)
    size_t stride = 0;
    const auto* plane = heif_image_get_plane_readonly2 (image.ptr, heif_channel_interleaved, &stride);
#else
    int intStride = 0;
    const auto* plane = heif_image_get_plane_readonly (image.ptr, heif_channel_interleaved, &intStride);
    const auto stride = (size_t)intStride;
#endif
    if (plane == nullptr || out.width <= 0 || out.height <= 0)
    {
        error = "Unsupported HEIC/HEIF pixel format.";
        return std::nullopt;
    }

    out.pixels.resize ((size_t)out.width * (size_t)out.height * 4);
    for (int y = 0; y < out.height; ++y)
        std::memcpy (out.pixels.data() + (size_t)y * (size_t)out.width * 4, plane + (size_t)y * stride,
                     (size_t)out.width * 4);
    return out;
}
#endif

std::optional<RgbaImage> decodeWithJuce (const void* data, size_t size, juce::String& error)
{
    auto image = juce::ImageFileFormat::loadFrom (data, size);
    if (!image.isValid())
    {
        error = "Unsupported or unreadable image file (PNG, JPEG, GIF and HEIC are supported).";
        return std::nullopt;
    }
    image = image.convertedToFormat (juce::Image::ARGB);

    RgbaImage out;
    out.width = image.getWidth();
    out.height = image.getHeight();
    out.pixels.resize ((size_t)out.width * (size_t)out.height * 4);
    const juce::Image::BitmapData bitmap (image, juce::Image::BitmapData::readOnly);
    auto* dst = out.pixels.data();
    for (int y = 0; y < out.height; ++y)
        for (int x = 0; x < out.width; ++x, dst += 4)
        {
            const auto colour = bitmap.getPixelColour (x, y); // un-premultiplied
            dst[0] = colour.getRed();
            dst[1] = colour.getGreen();
            dst[2] = colour.getBlue();
            dst[3] = colour.getAlpha();
        }
    return out;
}
} // namespace

std::optional<RgbaImage> decodeImageData (const void* data, size_t size, juce::String& error)
{
    if (data == nullptr || size == 0)
    {
        error = "The image file is empty.";
        return std::nullopt;
    }

    if (looksLikeHeif (static_cast<const std::uint8_t*> (data), size))
    {
#if ITS_HAVE_LIBHEIF
        return decodeHeif (data, size, error);
#else
        error = "HEIC/HEIF images are not supported in this build.";
        return std::nullopt;
#endif
    }
    return decodeWithJuce (data, size, error);
}

std::optional<RgbaImage> decodeImageFile (const juce::File& file, juce::String& error)
{
    juce::MemoryBlock block;
    if (!file.loadFileAsData (block))
    {
        error = "Could not read " + file.getFileName() + ".";
        return std::nullopt;
    }
    return decodeImageData (block.getData(), block.getSize(), error);
}
} // namespace its
