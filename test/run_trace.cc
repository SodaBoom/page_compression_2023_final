// Copyright [2023] Alibaba Cloud All rights reserved

/*
 * Local test to run sample trace
 */

#include "page_engine.h"
#include <iostream>
#include <fstream>
#include <cassert>
#include <cstring>
#include <sstream>

class Visitor {
 private:
  PageEngine *page_engine;
  const int page_size{16384};
  void *page_buf;
  void *trace_buf;

 public:
  Visitor() {
    std::string path = "./";
    RetCode ret = PageEngine::Open(path, &page_engine);
    assert(ret == kSucc);

    page_buf = malloc(page_size);
    trace_buf = malloc(page_size);
  }

  ~Visitor() {
    delete page_engine;
    free(page_buf);
    free(trace_buf);
  }

  void run_trace(std::string path) {
    std::ifstream trace_file(path);
    char RW;
    uint32_t page_no;

    std::string line;
    while (std::getline(trace_file, line)) {
      std::stringstream linestream(line);
      if (!(linestream >> RW >> page_no)) break;
      trace_file.read((char*)trace_buf, page_size);

      switch(RW) {
        case 'R':
        {
          std::cout << "Read Page page_no: " << page_no << std::endl;
          RetCode ret = page_engine->pageRead(page_no, page_buf);
          assert(ret == kSucc);
          assert(memcmp(page_buf, trace_buf, page_size) == 0);
          break;
        }
        case 'W':
        {
          std::cout << "Write Page page_no: " << page_no << std::endl;
          RetCode ret = page_engine->pageWrite(page_no, trace_buf);
          assert(ret == kSucc);
          break;
        }
        default:
          assert(false);
      }
    }
    trace_file.close();
  }
};

int main(int argc, char* argv[]) {
  assert(argc == 2);

  std::string path(argv[1]);

  Visitor visitor = Visitor();

  visitor.run_trace(path);

  std::cout << "Finished trace run!" << std::endl;
  return 0;
}
