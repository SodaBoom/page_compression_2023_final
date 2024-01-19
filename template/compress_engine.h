// Copyright [2023] Alibaba Cloud All rights reserved
#ifndef PAGE_ENGINE_COMPRESS_ENGINE_H_
#define PAGE_ENGINE_COMPRESS_ENGINE_H_
#include "include/page_engine.h"

/*
 * Complete the functions below to implement you own engine
 */

class CompressEngine : public PageEngine  {
 public:
  static RetCode Open(const std::string& path, PageEngine** eptr);

  explicit CompressEngine();

  ~CompressEngine() override;

  RetCode pageWrite(uint32_t page_no, const void *buf) override;

  RetCode pageRead(uint32_t page_no, void *buf) override;
};

#endif  // PAGE_ENGINE_COMPRESS_ENGINE_H_
