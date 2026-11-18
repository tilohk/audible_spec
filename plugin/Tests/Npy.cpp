#include "Npy.h"

#include <cstring>
#include <stdexcept>

namespace its::test
{
NpyArray loadNpy (const juce::File& file)
{
    juce::MemoryBlock block;
    if (!file.loadFileAsData (block))
        throw std::runtime_error ("cannot read " + file.getFullPathName().toStdString());

    const auto* bytes = static_cast<const unsigned char*> (block.getData());
    const size_t size = block.getSize();
    if (size < 10 || std::memcmp (bytes, "\x93NUMPY", 6) != 0)
        throw std::runtime_error ("not a .npy file: " + file.getFullPathName().toStdString());

    const int major = bytes[6];
    size_t headerLen, offset;
    if (major == 1)
    {
        headerLen = (size_t)bytes[8] | ((size_t)bytes[9] << 8);
        offset = 10;
    }
    else
    {
        headerLen = (size_t)bytes[8] | ((size_t)bytes[9] << 8) | ((size_t)bytes[10] << 16) |
                    ((size_t)bytes[11] << 24);
        offset = 12;
    }

    const juce::String header (reinterpret_cast<const char*> (bytes + offset), headerLen);
    NpyArray arr;

    arr.dtype = header.fromFirstOccurrenceOf ("'descr':", false, false)
                    .fromFirstOccurrenceOf ("'", false, false)
                    .upToFirstOccurrenceOf ("'", false, false);
    if (header.contains ("'fortran_order': True"))
        throw std::runtime_error ("fortran order not supported");

    const auto shapeText = header.fromFirstOccurrenceOf ("'shape':", false, false)
                               .fromFirstOccurrenceOf ("(", false, false)
                               .upToFirstOccurrenceOf (")", false, false);
    size_t count = 1;
    for (auto& token : juce::StringArray::fromTokens (shapeText, ",", ""))
        if (token.trim().isNotEmpty())
        {
            arr.shape.push_back (token.trim().getIntValue());
            count *= (size_t)arr.shape.back();
        }

    const auto* data = bytes + offset + headerLen;
    const size_t available = size - offset - headerLen;
    arr.values.resize (count);

    if (arr.dtype == "|u1")
    {
        if (available < count)
            throw std::runtime_error ("truncated .npy");
        for (size_t i = 0; i < count; ++i)
            arr.values[i] = data[i];
    }
    else if (arr.dtype == "<f4")
    {
        if (available < count * 4)
            throw std::runtime_error ("truncated .npy");
        for (size_t i = 0; i < count; ++i)
        {
            float v;
            std::memcpy (&v, data + i * 4, 4);
            arr.values[i] = v;
        }
    }
    else if (arr.dtype == "<f8")
    {
        if (available < count * 8)
            throw std::runtime_error ("truncated .npy");
        for (size_t i = 0; i < count; ++i)
        {
            double v;
            std::memcpy (&v, data + i * 8, 8);
            arr.values[i] = v;
        }
    }
    else
    {
        throw std::runtime_error ("unsupported dtype " + arr.dtype.toStdString());
    }
    return arr;
}
} // namespace its::test
