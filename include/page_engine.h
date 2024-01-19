// Copyright [2023] Alibaba Cloud All rights reserved
#ifndef INCLUDE_PAGE_ENGINE_H_
#define INCLUDE_PAGE_ENGINE_H_

#include <string>

typedef enum {
  kSucc = 0,
  kNotFound = 1,
  kCorruption = 2,
  kNotSupported = 3,
  kInvalidArgument = 4,
  kIOError = 5,
  kIncomplete = 6,
  kTimedOut = 7,
  kFull = 8,
  kOutOfMemory = 9,
} RetCode;

class PageEngine {
 public:
  // Open engine
  static RetCode Open(const std::string& path, PageEngine** eptr);

  // Close engine
  virtual ~PageEngine() {}

  // Write 16KB page into disk
  virtual RetCode pageWrite(uint32_t page_no, const void *buf) = 0;

  // Read 16KB page from disk
  virtual RetCode pageRead(uint32_t page_no, void *buf) = 0;
};

#endif  // INCLUDE_PAGE_ENGINE_H_
