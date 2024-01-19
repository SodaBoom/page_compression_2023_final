// Copyright [2023] Alibaba Cloud All rights reserved
#include "dummy_engine.h"
#include <fcntl.h>
#include <unistd.h>
#include <cassert>
#include <vector>
#include <mutex>
#include <cmath>
#include <fstream>
#include <set>
#include "lib/zstd.h"

/*
 * Dummy sample of page engine
 */

RetCode PageEngine::Open(const std::string &path, PageEngine **eptr) {
    return DummyEngine::Open(path, eptr);
}

static std::string pathJoin(const std::string &p1, const std::string &p2) {
    char sep = '/';
    std::string tmp = p1;

#ifdef _WIN32
    sep = '\\';
#endif

    if (p1[p1.length() - 1] != sep) {
        tmp += sep;
        return tmp + p2;
    } else {
        return p1 + p2;
    }
}

RetCode DummyEngine::Open(const std::string &path, PageEngine **eptr) {
    auto *engine = new DummyEngine(path);
    *eptr = engine;
    return kSucc;
}

DummyEngine::DummyEngine(const std::string &path) {
    data_path_ = path;
    indexes_ = (Index *) calloc(MAX_PAGE_NUM, sizeof(Index));
    if (access(pathJoin(path, "data0.ibd").c_str(), F_OK) == 0) { // 文件存在
        auto f = [&](int i) {
            load_meta(i);
        };
        std::vector<std::thread> threads;
        for (int i = 0; i < THREAD_NUM; ++i) {
            threads.emplace_back(f, i);
        }
        for (auto &item: threads) {
            item.join();
        }
    } else {
        for (int i = 0; i < THREAD_NUM; ++i) {
            load_meta(i);
        }
    }
}

DummyEngine::~DummyEngine() {
    for (auto &item: metas_) {
        delete item;
        item = nullptr;
    }
}

RetCode DummyEngine::pageWrite(uint32_t page_no, const void *buf) {
    thread_local Meta *meta = load_meta(page_no % THREAD_NUM);
    atomic_fetch_add(&no_sync_num_, 1);
    atomic_fetch_add(&work_num_, 1);
    meta->mutex_.lock();
    meta->add(page_no, buf);
    meta->mutex_.unlock();
    if (1 == atomic_fetch_sub(&work_num_, 1) && no_sync_num_.load() > 0) {
        sync();
    }
    return kSucc;
}

RetCode DummyEngine::pageRead(uint32_t page_no, void *buf) {
    thread_local Meta *meta = load_meta(page_no % THREAD_NUM);
    atomic_fetch_add(&work_num_, 1);
    meta->get(page_no, buf);
    if (1 == atomic_fetch_sub(&work_num_, 1) && no_sync_num_.load() > 0) {
        sync();
    }
    return kSucc;
}

Meta *DummyEngine::load_meta(uint32_t thread_id) {
    if (metas_[thread_id] == nullptr) {
        std::string data_file = pathJoin(data_path_, "data" + std::to_string(thread_id) + ".ibd");
        int data_fd = open(data_file.c_str(), O_RDWR | O_CREAT | O_DIRECT | O_NOATIME, S_IRWXU | S_IRWXG | S_IRWXO);
        assert(data_fd >= 0);
        std::string index_file = pathJoin(data_path_, "index" + std::to_string(thread_id) + ".ibd");
        int index_fd = open(index_file.c_str(), O_RDWR | O_CREAT | O_DIRECT | O_NOATIME, S_IRWXU | S_IRWXG | S_IRWXO);
        assert(index_fd >= 0);
        auto meta = new Meta(thread_id, indexes_, data_fd, index_fd, &no_sync_num_);
        metas_[thread_id] = meta;
    }
    return metas_[thread_id];
}

void DummyEngine::sync() {
    for (const auto &meta: metas_) {
        if (meta != nullptr && meta->is_dirty() && meta->mutex_.try_lock()) {
            meta->sync();
            meta->mutex_.unlock();
        }
        if (work_num_.load() != 0 || no_sync_num_.load() == 0) {
            return;
        }
    }
}
