#include "HalStorage.h"

#include <SDCardManager.h>
#ifdef BENCHMARK_MODE
#include <Arduino.h>
#endif

#define SDCard SDCardManager::getInstance()

HalStorage HalStorage::instance;

HalStorage::HalStorage() {}

bool HalStorage::begin() { return SDCard.begin(); }

bool HalStorage::ready() const { return SDCard.ready(); }

std::vector<String> HalStorage::listFiles(const char* path, int maxFiles) { return SDCard.listFiles(path, maxFiles); }

String HalStorage::readFile(const char* path) { return SDCard.readFile(path); }

bool HalStorage::readFileToStream(const char* path, Print& out, size_t chunkSize) {
  return SDCard.readFileToStream(path, out, chunkSize);
}

size_t HalStorage::readFileToBuffer(const char* path, char* buffer, size_t bufferSize, size_t maxBytes) {
  return SDCard.readFileToBuffer(path, buffer, bufferSize, maxBytes);
}

bool HalStorage::writeFile(const char* path, const String& content) { return SDCard.writeFile(path, content); }

bool HalStorage::ensureDirectoryExists(const char* path) { return SDCard.ensureDirectoryExists(path); }

FsFile HalStorage::open(const char* path, const oflag_t oflag) {
#ifdef BENCHMARK_MODE
  const auto startUs = micros();
#endif
  FsFile f = SDCard.open(path, oflag);
#ifdef BENCHMARK_MODE
  const auto elapsed = micros() - startUs;
  if (oflag & O_WRONLY || oflag & O_RDWR || oflag & O_CREAT) {
    sdIoCounters.writeTimeUs += elapsed;
    if (f) sdIoCounters.writeOpens++;
  } else {
    sdIoCounters.readTimeUs += elapsed;
    if (f) sdIoCounters.readOpens++;
  }
#endif
  return f;
}

bool HalStorage::mkdir(const char* path, const bool pFlag) { return SDCard.mkdir(path, pFlag); }

bool HalStorage::exists(const char* path) {
#ifdef BENCHMARK_MODE
  sdIoCounters.existsCalls++;
#endif
  return SDCard.exists(path);
}

bool HalStorage::remove(const char* path) {
#ifdef BENCHMARK_MODE
  sdIoCounters.removeCalls++;
#endif
  return SDCard.remove(path);
}

bool HalStorage::rename(const char* oldPath, const char* newPath) { return SDCard.rename(oldPath, newPath); }

bool HalStorage::rmdir(const char* path) { return SDCard.rmdir(path); }

bool HalStorage::openFileForRead(const char* moduleName, const char* path, FsFile& file) {
#ifdef BENCHMARK_MODE
  const auto startUs = micros();
#endif
  bool result = SDCard.openFileForRead(moduleName, path, file);
#ifdef BENCHMARK_MODE
  sdIoCounters.readTimeUs += micros() - startUs;
  if (result) sdIoCounters.readOpens++;
#endif
  return result;
}

bool HalStorage::openFileForRead(const char* moduleName, const std::string& path, FsFile& file) {
  return openFileForRead(moduleName, path.c_str(), file);
}

bool HalStorage::openFileForRead(const char* moduleName, const String& path, FsFile& file) {
  return openFileForRead(moduleName, path.c_str(), file);
}

bool HalStorage::openFileForWrite(const char* moduleName, const char* path, FsFile& file) {
#ifdef BENCHMARK_MODE
  const auto startUs = micros();
#endif
  bool result = SDCard.openFileForWrite(moduleName, path, file);
#ifdef BENCHMARK_MODE
  sdIoCounters.writeTimeUs += micros() - startUs;
  if (result) sdIoCounters.writeOpens++;
#endif
  return result;
}

bool HalStorage::openFileForWrite(const char* moduleName, const std::string& path, FsFile& file) {
  return openFileForWrite(moduleName, path.c_str(), file);
}

bool HalStorage::openFileForWrite(const char* moduleName, const String& path, FsFile& file) {
  return openFileForWrite(moduleName, path.c_str(), file);
}

bool HalStorage::removeDir(const char* path) { return SDCard.removeDir(path); }

#ifdef BENCHMARK_MODE
void HalStorage::resetSdIoCounters() { sdIoCounters = {}; }

HalStorage::SdIoCounters HalStorage::getSdIoCounters() const { return sdIoCounters; }
#endif