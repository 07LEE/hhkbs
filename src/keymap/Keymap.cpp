#include "keymap/Keymap.h"

#include <stdexcept>

namespace hhkbs::keymap {

Keymap::Keymap(const std::vector<std::uint8_t>& profileBytes)
{
    load(profileBytes);
}

void Keymap::load(const std::vector<std::uint8_t>& profileBytes)
{
    if (profileBytes.size() != profileByteCount) {
        throw std::invalid_argument("HHKB Studio profile must contain exactly 960 bytes");
    }

    for (std::size_t layerIndex = 0; layerIndex < layerCount; ++layerIndex) {
        for (std::size_t keyIndex = 0; keyIndex < keysPerLayer; ++keyIndex) {
            const auto byteIndex =
                (layerIndex * layerByteCount) + (keyIndex * scanCodeByteCount);
            layers_[layerIndex][keyIndex] =
                static_cast<ScanCode>(
                    static_cast<ScanCode>(profileBytes[byteIndex]) << 8U)
                | static_cast<ScanCode>(profileBytes[byteIndex + 1]);
        }
    }

    originalLayers_ = layers_;
}

std::vector<std::uint8_t> Keymap::toBytes() const
{
    std::vector<std::uint8_t> bytes;
    bytes.reserve(profileByteCount);

    for (const auto& layer : layers_) {
        for (const auto scanCode : layer) {
            bytes.push_back(static_cast<std::uint8_t>((scanCode >> 8U) & 0xFFU));
            bytes.push_back(static_cast<std::uint8_t>(scanCode & 0xFFU));
        }
    }

    return bytes;
}

Keymap::ScanCode Keymap::scanCode(
    const std::size_t layerIndex,
    const std::size_t keyIndex) const
{
    validateIndices(layerIndex, keyIndex);
    return layers_[layerIndex][keyIndex];
}

void Keymap::setScanCode(
    const std::size_t layerIndex,
    const std::size_t keyIndex,
    const ScanCode scanCode)
{
    validateIndices(layerIndex, keyIndex);
    layers_[layerIndex][keyIndex] = scanCode;
}

const Keymap::Layers& Keymap::layers() const noexcept
{
    return layers_;
}

bool Keymap::isModified() const noexcept
{
    return layers_ != originalLayers_;
}

void Keymap::reset() noexcept
{
    layers_ = originalLayers_;
}

void Keymap::validateIndices(
    const std::size_t layerIndex,
    const std::size_t keyIndex)
{
    if (layerIndex >= layerCount) {
        throw std::out_of_range("keymap layer index is out of range");
    }
    if (keyIndex >= keysPerLayer) {
        throw std::out_of_range("keymap key index is out of range");
    }
}

}  // namespace hhkbs::keymap
