// Copyright [2023] Alibaba Cloud All rights reserved

/*
 * Local test to read and write random page
 */

#include "page_engine.h"
#include <unordered_map>
#include <ctime>
#include <stdint.h>
#include <iostream>
#include <cassert>


class Visitor {
 private:
  std::unordered_map<uint32_t, uint32_t> checksum_map;
  PageEngine *page_engine;
  const int page_size{16384};
  void *buf;
  size_t io_cnt;

  void io_cnt_inc() {
    io_cnt++;

    if (io_cnt % 10000 == 0) {
      std::cout << io_cnt << " IO finished" << std::endl;
    }
  }

  void random_page(void *buf) {
    char *data = (char *)buf;
    const char charset[] =
        "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz!#$%&'()*+,-./ :;<=>?@[]^_`{|}~";

    for (int i = 0; i < page_size; i++) {
        int index = rand() % (sizeof(charset) - 1);
        data[i] = charset[index];
    }
  }

  uint32_t page_checksum(void* buf) {
    uint32_t *data = (uint32_t *)buf;

    uint32_t sum = 0;

    for(int i = 0; i < (page_size >> 2); i++){
        sum += data[i];
    }

    return sum;
  }

 public:
  Visitor() : io_cnt(0) {
    std::string path = "./";
    RetCode ret = PageEngine::Open(path, &page_engine);
    assert(ret == kSucc);

    buf = malloc(page_size);
  }

  ~Visitor() {
    delete page_engine;
    free(buf);
  }

  void rand_write_page(uint32_t page_no) {
    random_page(buf);
    uint32_t checksum = page_checksum(buf);

    RetCode ret = page_engine->pageWrite(page_no, buf);
    assert(ret == kSucc);
    checksum_map[page_no] = checksum;

    io_cnt_inc();
  }

  void check_read_page(uint32_t page_no) {
    RetCode ret = page_engine->pageRead(page_no, buf);
    assert(ret == kSucc);

    uint32_t checksum = page_checksum(buf);

    if (checksum != checksum_map[page_no]) {
      std::cout << "Read page_no: " << page_no
                << "checksum mismatch!!!" << std::endl;

      assert(false);
    }

    io_cnt_inc();
  }
};

int main(int argc, char* argv[]) {
  assert(argc <= 2);
  srand(time(NULL));

  uint32_t page_cnt = 65536;

  if (argc == 2) {
    page_cnt = static_cast<uint32_t>(std::stoul(argv[1]));
    assert(page_cnt <= 6553600);
  }

  Visitor visitor = Visitor();

  for (uint32_t page_no = 0; page_no < page_cnt; page_no++) {
    visitor.rand_write_page(page_no);
    visitor.check_read_page(page_no);
  }

  for (uint32_t i=0; i < page_cnt * 2; i++) {
    uint32_t page_no;

    page_no = rand() % (page_cnt - 1);
    visitor.rand_write_page(page_no);

    page_no = rand() % (page_cnt - 1);
    visitor.check_read_page(page_no);
  }

  std::cout << "Finished random read write check!" << std::endl;
  return 0;
}
