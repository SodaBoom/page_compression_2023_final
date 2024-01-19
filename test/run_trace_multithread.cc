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
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <vector>
#include <thread>
#include <sys/eventfd.h>
#include "../page_engine/dummy_engine.h"

static const float SHUTDOWN_THD = 0.001;

typedef enum
{
  IO_READ = 0,
  IO_WRITE = 1
} IOType;

struct Pipe
{
  int pipefd[2];
};
struct TraceItem
{
  char io_type;
  uint32_t page_no;
  char data[PAGE_SIZE];
};
class Visitor
{
private:
  PageEngine *page_engine;
  const int page_size{16384};
  int thread_num;
  int pid;
  std::vector<Pipe> pipes;
  std::vector<int> event_fds;

public:
  Visitor(int thread) : thread_num(thread), pid(-1)
  {
    for (int i = 0; i < thread_num; i++)
    {
      pipes.emplace_back();
      int rc = pipe(pipes.back().pipefd);
      assert(rc != -1);
      event_fds.emplace_back();
      event_fds.back() = eventfd(0, 0);
    }
  }

  ~Visitor()
  {
  }

  void thread_func(int thread_id)
  {
    assert(pid == 0);
    assert(page_size == 16384);
    // std::cout << "Thread " << thread_id << " start on pipe " << pipes[thread_id].pipefd[0] << std::endl;
    void *page_ptr = malloc(2 * page_size);
    void *page_buf = (void *)(((uint64_t)page_ptr + 16384) & (~0x3fff));

    void *trace_ptr = malloc(2 * page_size);
    void *trace_buf = (void *)(((uint64_t)trace_ptr + 16384) & (~0x3fff));
    uint64_t one = 1;
    assert(sizeof(uint64_t) == write(event_fds[thread_id], &one, sizeof one));
    while (true)
    {
      uint8_t cmd;
      uint32_t page_no;
      int bytes;
      RetCode ret;

      bytes = read(pipes[thread_id].pipefd[0], &cmd, 1);
      if (bytes == 0)
        break;
      assert(bytes == 1);

      bytes = read(pipes[thread_id].pipefd[0], &page_no, 4);
      assert(bytes == 4);

      bytes = read(pipes[thread_id].pipefd[0], trace_buf, page_size);
      assert(bytes == page_size);
      assert(sizeof(uint64_t) == write(event_fds[thread_id], &one, sizeof one));
      switch (cmd)
      {
      case IO_READ:
        // std::cout << "Thread " << thread_id << " Receive CMD: Read Page page_no: " << page_no << std::endl;
        ret = page_engine->pageRead(page_no, page_buf);
        assert(ret == kSucc);
        assert(memcmp(page_buf, trace_buf, page_size) == 0);
        break;

      case IO_WRITE:
        // std::cout << "Thread " << thread_id << " Receive CMD: Write Page page_no: " << page_no << std::endl;
        ret = page_engine->pageWrite(page_no, trace_buf);
        assert(ret == kSucc);
        break;
      }
    }

    free(page_ptr);
    free(trace_ptr);

    // std::cout << "Thread " << thread_id << " exit" << std::endl;
  }

  void pageRead(uint32_t page_no, void *buf)
  {
    assert(pid > 0);
    int thread_id = page_no % thread_num;
    uint8_t io_type = IO_READ;
    uint64_t one = 0;
    assert(sizeof(uint64_t) == read(event_fds[thread_id], &one, sizeof one));
    // std::cout << "Send CMD to thread " << thread_id << ": Read Page page_no: " << page_no << std::endl;
    write(pipes[thread_id].pipefd[1], &io_type, 1);
    write(pipes[thread_id].pipefd[1], &page_no, 4);
    write(pipes[thread_id].pipefd[1], buf, page_size);
  }

  void pageWrite(uint32_t page_no, void *buf)
  {
    assert(pid > 0);
    int thread_id = page_no % thread_num;
    uint8_t io_type = IO_WRITE;
    uint64_t one = 0;
    assert(sizeof(uint64_t) == read(event_fds[thread_id], &one, sizeof one));
    // std::cout << "Send CMD to thread " << thread_id << ": Write Page page_no: " << page_no << std::endl;
    write(pipes[thread_id].pipefd[1], &io_type, 1);
    write(pipes[thread_id].pipefd[1], &page_no, 4);
    write(pipes[thread_id].pipefd[1], buf, page_size);
  }

  void run()
  {

    pid = fork();
    assert(pid >= 0);

    if (pid == 0)
    {
      std::string path = "./";
      RetCode ret = PageEngine::Open(path, &page_engine);
      assert(ret == kSucc);

      std::vector<std::thread> threads;

      for (int thread_id = 0; thread_id < thread_num; thread_id++)
      {
        close(pipes[thread_id].pipefd[1]);
        threads.emplace_back(std::thread(&Visitor::thread_func, this, thread_id));
        // sleep(1);
        usleep(1000);
      }

      for (int thread_id = 0; thread_id < thread_num; thread_id++)
      {
        threads[thread_id].join();
        close(pipes[thread_id].pipefd[0]);
      }

      // online judge will not call delete(page_engine)
      // delete(page_engine);
      exit(0);
    }
    else
    {
      for (int thread_id = 0; thread_id < thread_num; thread_id++)
      {
        close(pipes[thread_id].pipefd[0]);
      }
    }
  }

  void shutdown()
  {
    for (int thread_id = 0; thread_id < thread_num; thread_id++)
    {
      close(pipes[thread_id].pipefd[1]);
    }

    int status;
    wait(&status);
    if (WIFEXITED(status))
    {
      int exitCode = WEXITSTATUS(status);
      printf("Child process exited with code %d\n", exitCode);
      assert(exitCode == 0);
    }
    else
    {
      printf("Child process did not exit normally\n");
      assert(false);
    }
  }
};

void run_trace(std::string path)
{

  char RW;
  uint32_t page_no;
  const int page_size = 16384;

  Visitor *visitor = new Visitor(THREAD_NUM);
  visitor->run();

  void *trace_buf = malloc(page_size);
  std::string line;
  std::vector<TraceItem> trace_items;
  std::ifstream trace_file(path);
  while (std::getline(trace_file, line))
  {
    std::stringstream linestream(line);
    if (!(linestream >> RW >> page_no))
      break;
    auto &item = trace_items.emplace_back();
    item.io_type = RW;
    item.page_no = page_no;
    trace_file.read((char *)item.data, page_size);
  }
  trace_file.close();

  auto starttime = std::chrono::system_clock::now();
  for (int i = 0; i < 500; i++)
  {
    for (auto item : trace_items)
    {
      switch (item.io_type)
      {
      case 'R':
      {
        visitor->pageRead(item.page_no, item.data);
        break;
      }
      case 'W':
      {
        visitor->pageWrite(item.page_no, item.data);
        break;
      }
      default:
        assert(false);
      }

      // if ((float)rand() / RAND_MAX < SHUTDOWN_THD)
      // {
      //   std::cout << "shutdown" << std::endl;
      //   visitor->shutdown();
      //   delete visitor;
      //   visitor = new Visitor(THREAD_NUM);
      //   visitor->run();
      // }
    }
    std::cout << "done: " << i << std::endl;
    visitor->shutdown();
    delete visitor;
    system("rm -rf ./*.ibd");
    visitor = new Visitor(THREAD_NUM);
    visitor->run();
  }
  std::chrono::duration<double> diff = std::chrono::system_clock::now() - starttime;
  std::cout << "所耗时间为：" << diff.count() * 1e3 << "ms" << std::endl;
  delete visitor;
  free(trace_buf);
}

int main(int argc, char *argv[])
{
  assert(argc == 2);

  // std::string path("/root/page_compression_2023/trace/sysbench.trace");
  std::string path(argv[1]);
  system("rm -rf ./*.ibd");
  srand(0);

  run_trace(path);

  std::cout << "Finished trace run!" << std::endl;
  return 0;
}
