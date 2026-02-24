#pragma once
#include <HalStorage.h>

#include <iostream>
#include <optional>
#include <type_traits>

namespace serialization {
template <typename T>
static void writePod(std::ostream& os, const T& value) {
  os.write(reinterpret_cast<const char*>(&value), sizeof(T));
}

template <typename T>
static void writePod(FsFile& file, const T& value) {
  file.write(reinterpret_cast<const uint8_t*>(&value), sizeof(T));
}

template <typename T>
static void readPod(std::istream& is, T& value) {
  is.read(reinterpret_cast<char*>(&value), sizeof(T));
}

template <typename T>
static void readPod(FsFile& file, T& value) {
  file.read(reinterpret_cast<uint8_t*>(&value), sizeof(T));
}

static void writeString(std::ostream& os, const std::string& s) {
  const uint32_t len = s.size();
  writePod(os, len);
  os.write(s.data(), len);
}

static void writeString(FsFile& file, const std::string& s) {
  const uint32_t len = s.size();
  writePod(file, len);
  file.write(reinterpret_cast<const uint8_t*>(s.data()), len);
}

static void readString(std::istream& is, std::string& s) {
  uint32_t len;
  readPod(is, len);
  s.resize(len);
  is.read(&s[0], len);
}

static void readString(FsFile& file, std::string& s) {
  uint32_t len;
  readPod(file, len);
  s.resize(len);
  file.read(&s[0], len);
}

// std::optional<T> overloads for integral types.
// On disk the value is stored as a plain T; the sentinel static_cast<T>(-1)
// maps to/from std::nullopt so the binary format is unchanged.
template <typename T, std::enable_if_t<std::is_integral_v<T>, int> = 0>
static void writePod(std::ostream& os, const std::optional<T>& value) {
  const T raw = value.value_or(static_cast<T>(-1));
  os.write(reinterpret_cast<const char*>(&raw), sizeof(T));
}

template <typename T, std::enable_if_t<std::is_integral_v<T>, int> = 0>
static void writePod(FsFile& file, const std::optional<T>& value) {
  const T raw = value.value_or(static_cast<T>(-1));
  file.write(reinterpret_cast<const uint8_t*>(&raw), sizeof(T));
}

template <typename T, std::enable_if_t<std::is_integral_v<T>, int> = 0>
static void readPod(std::istream& is, std::optional<T>& value) {
  T raw;
  is.read(reinterpret_cast<char*>(&raw), sizeof(T));
  value = (raw == static_cast<T>(-1)) ? std::nullopt : std::optional<T>(raw);
}

template <typename T, std::enable_if_t<std::is_integral_v<T>, int> = 0>
static void readPod(FsFile& file, std::optional<T>& value) {
  T raw;
  file.read(reinterpret_cast<uint8_t*>(&raw), sizeof(T));
  value = (raw == static_cast<T>(-1)) ? std::nullopt : std::optional<T>(raw);
}
}  // namespace serialization
