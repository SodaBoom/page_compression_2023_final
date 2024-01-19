// Copyright [2023] Alibaba Cloud All rights reserved
#include "compress_engine.h"

/*
 * Complete the functions below to implement you own engine
 */

RetCode PageEngine::Open(const std::string& path, PageEngine** eptr) {
  return CompressEngine::Open(path, eptr);
}

RetCode CompressEngine::Open(const std::string& path, PageEngine** eptr) {
  return kSucc;
}

CompressEngine::CompressEngine() { }

CompressEngine::~CompressEngine() { }

RetCode CompressEngine::pageWrite(uint32_t page_no, const void *buf) {
  return kSucc;
}

RetCode CompressEngine::pageRead(uint32_t page_no, void *buf) {
  return kSucc;
}
