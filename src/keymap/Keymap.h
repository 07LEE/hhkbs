#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace hhkbs::keymap {

class Keymap final {
public:
    using ScanCode = std::uint16_t;

    static constexpr std::size_t layerCount = 4;
    static constexpr std::size_t layerByteCount = 0xF0;
    static constexpr std::size_t scanCodeByteCount = sizeof(ScanCode);
    static constexpr std::size_t keysPerLayer = layerByteCount / scanCodeByteCount;
    static constexpr std::size_t profileByteCount = layerCount * layerByteCount;

    using Layer = std::array<ScanCode, keysPerLayer>;
    using Layers = std::array<Layer, layerCount>;

    Keymap() = default;
    explicit Keymap(const std::vector<std::uint8_t>& profileBytes);

    void load(const std::vector<std::uint8_t>& profileBytes);
    [[nodiscard]] std::vector<std::uint8_t> toBytes() const;

    [[nodiscard]] ScanCode scanCode(std::size_t layerIndex, std::size_t keyIndex) const;
    void setScanCode(std::size_t layerIndex, std::size_t keyIndex, ScanCode scanCode);

    [[nodiscard]] const Layers& layers() const noexcept;
    [[nodiscard]] bool isModified() const noexcept;
    void reset() noexcept;

private:
    static void validateIndices(std::size_t layerIndex, std::size_t keyIndex);

    Layers layers_{};
    Layers originalLayers_{};
};

}  // namespace hhkbs::keymap
