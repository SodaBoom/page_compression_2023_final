// Copyright [2023] Alibaba Cloud All rights reserved
#ifndef PAGE_ENGINE_DUMMY_ENGINE_H_
#define PAGE_ENGINE_DUMMY_ENGINE_H_
#define ZSTD_STATIC_LINKING_ONLY

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <csignal>
#include <cassert>
#include <thread>
#include <mutex>
#include <cstring>
#include <iostream>
#include <cmath>
#include <unistd.h>
#include <atomic>
#include <unordered_map>
#include <list>
#include <vector>
#include "page_engine.h"
#include "lib/zstd.h"

#define THREAD_NUM 15
#define BUFFER_SIZE (4096 * 8)
#define PAGE_SIZE 16384
#define MAX_PAGE_NUM 655360
#define DIRECT_BLOCK_SIZE 512
#define INDEX_WAL_BUFFER_SIZE (4096 * 2)
#define MAX_INDEX_WAL_INCREMENT_NUM (INDEX_WAL_BUFFER_SIZE / sizeof(IndexWAL))
#define MAX_CACHE_SIZE uint32_t(1024 * 1024 * 2.8)
#define MIN_CACHE_SIZE uint32_t(1024 * 1024 * 1.4)

/*
 * Dummy sample of page engine
 */

class Index {
public:
    uint32_t off{0};
    uint16_t len{0};
}__attribute__((packed));

inline bool is_in_interval(uint32_t off, Index *index) {
    return (off >= index->off) && (off < index->off + index->len);
}

class IndexWAL {
public:
    uint16_t page_no{0};
    uint16_t len{0};
};

class RecordHead {
public:
    uint16_t record_page_no{0};
    uint16_t record_len{0};
};

class Record {
public:
    RecordHead header{};
    char data[PAGE_SIZE]{0};

    [[nodiscard]] uint16_t get_data_len() const {
        return header.record_len - sizeof(RecordHead);
    }
};

class Info {
public:
    uint32_t num{0};
    uint32_t off{0};
    uint32_t head{0};
    uint32_t tail{0};
};

class CacheHead {
public:
    uint32_t cache_no_{MAX_PAGE_NUM};
    uint32_t cache_size_{MAX_PAGE_NUM};
    uint16_t out_num_{0};
};

class Cache {
public:
    CacheHead header{};
    char original[PAGE_SIZE]{0};

    Cache(uint32_t cache_no, const std::vector<Index> &out_indexes, uint16_t cache_size) {
        init(cache_no, out_indexes, cache_size);
    }

    void init(uint32_t cache_no, const std::vector<Index> &out_indexes, uint16_t cache_size) {
        header.cache_no_ = cache_no;
        header.cache_size_ = cache_size;
        header.out_num_ = out_indexes.size();
        auto *out_indexes_ = (Index *) original;
        if (header.out_num_ > 0) {
            for (int i = 0; i < header.out_num_; ++i) {
                out_indexes_[i].off = out_indexes[i].off + out_indexes[i].len;
                out_indexes_[i].len = out_indexes[i].len;
            }
        }
    }

    [[nodiscard]] Record *read_buf(uint32_t off) const {
        uint32_t off_len = header.cache_no_ * PAGE_SIZE;
        auto *out_indexes_ = (Index *) original;
        for (int i = 0; i < header.out_num_; ++i) {
            if (off >= out_indexes_[i].off) {
                off_len += out_indexes_[i].len;
            } else {
                break;
            }
        }
        return (Record *) (original + header.out_num_ * sizeof(Index) + (off - off_len));
    }

    [[nodiscard]] uint16_t bytes() const {
        return header.cache_size_ + header.out_num_ * sizeof(Index) + sizeof(CacheHead);
    }

    char *data() {
        return original + header.out_num_ * sizeof(Index);
    }

    static uint16_t get_cache_bytes(uint16_t out_num, uint16_t cache_size) {
        return cache_size + out_num * sizeof(Index) + sizeof(CacheHead);
    }

    ~Cache() = default;
};

class MemoryPool {
public:
    uint32_t max_size_{0};
    uint32_t max_used_size_{0};
    uint32_t head_{0};
    uint32_t tail_{0};
    char *addr_{nullptr};

    explicit MemoryPool(uint32_t max_size) {
        max_size_ = max_size;
        addr_ = (char *) malloc(max_size_);
    }

    char *allocate(uint16_t size) {
        if (head_ + size > max_size_) {
            max_used_size_ = head_;
            head_ = 0;
        }
        uint32_t capacity;
        if (head_ >= tail_) {
            capacity = max_size_ - head_;
        } else {
            capacity = tail_ - head_;
        }
        assert(capacity >= size);
        char *data = addr_ + head_;
        head_ += size;
        if (head_ > max_used_size_) {
            max_used_size_ = head_;
        }
        return data;
    }

    void deallocate(uint16_t size) {
        tail_ += size;
        if (tail_ >= max_used_size_) {
            tail_ = 0;
        }
    }

    [[nodiscard]] bool check_capacity(uint16_t size) const {
        if (tail_ >= head_ && max_used_size_ != 0) {
            return tail_ - head_ >= size;
        }
        if (max_size_ - head_ >= size) {
            return true;
        }
        return tail_ >= size;
    }
};

class RingArray {
public:
    std::list<uint32_t> fifo_;
    std::list<uint32_t> idle_;
    std::vector<Cache *> caches_;
    uint16_t page_idx_map_[MAX_PAGE_NUM / THREAD_NUM]{0};
    MemoryPool *memory_pool{nullptr};


    explicit RingArray(double_t compression_ratio) {
        memory_pool = new MemoryPool(MAX_CACHE_SIZE - (MAX_CACHE_SIZE - MIN_CACHE_SIZE) * compression_ratio);
    }

    Cache *get(uint32_t cache_no) {
        if (page_idx_map_[cache_no] != 0) {
            return caches_[page_idx_map_[cache_no] - 1];
        }
        return nullptr;
    }

    void out_cache(uint16_t size) {
        while (!memory_pool->check_capacity(size)) {
            auto out_cache_no = fifo_.front();
            fifo_.pop_front();
            auto idx = page_idx_map_[out_cache_no] - 1;
            memory_pool->deallocate(caches_[idx]->bytes());
            caches_[idx] = nullptr;
            page_idx_map_[out_cache_no] = 0;
            idle_.emplace_back(idx);
        }
    }

    Cache *add(uint32_t cache_no, const std::vector<Index> &out_indexes, uint16_t cache_size) {
        assert(page_idx_map_[cache_no] == 0);
        auto size = Cache::get_cache_bytes(out_indexes.size(), cache_size);
        out_cache(size);
        auto cache = (Cache *) memory_pool->allocate(size);
        cache->init(cache_no, out_indexes, cache_size);
        uint32_t idx;
        if (idle_.empty()) {
            idx = caches_.size();
            caches_.emplace_back(cache);
        } else {
            idx = idle_.front();
            idle_.pop_front();
            caches_[idx] = cache;
        }
        page_idx_map_[cache_no] = idx + 1;
        fifo_.emplace_back(cache_no);
        return cache;
    }
};

class MetaPublic {
public:
    char buffer_[BUFFER_SIZE]__attribute__((aligned(DIRECT_BLOCK_SIZE))){};
    IndexWAL increment_[MAX_INDEX_WAL_INCREMENT_NUM]__attribute__((aligned(DIRECT_BLOCK_SIZE))){};
    char temp_[BUFFER_SIZE]__attribute__((aligned(DIRECT_BLOCK_SIZE))){};
    int data_fd_{-1};
    int index_fd_{-1};
    uint32_t index_head{0};
    uint32_t index_tail{0};
    uint32_t thread_id_{THREAD_NUM};
    Info info_{};
    Index *indexes_{nullptr};
    std::atomic_int64_t *no_sync_num_{nullptr};
    bool is_data_dirty{false};
    bool is_index_dirty{false};
    RingArray *ring_array_{nullptr};
    uint64_t write_data_bytes{0};
    double_t compression_ratio{0.2};
    bool no_cache{false};
    uint64_t hit_num{0};
    uint64_t miss_num{0};
    uint64_t compress_failure_cnt{0};
    uint64_t compress_greater50_cnt{0};

    MetaPublic(uint32_t thread_id, Index *indexes, int data_fd, int index_fd, std::atomic_int64_t *no_sync_num) {
        thread_id_ = thread_id;
        indexes_ = indexes;
        data_fd_ = data_fd;
        index_fd_ = index_fd;
        no_sync_num_ = no_sync_num;
        struct stat st{};
        assert(fstat(index_fd_, &st) == 0);
        if (st.st_size != 0) { // 重启
            uint64_t page_num = 0;
            auto *temp = (IndexWAL *) mmap(nullptr, st.st_size, PROT_READ, MAP_PRIVATE, index_fd_, 0);
            uint32_t off = 0;
            for (int j = 0; j < st.st_size / sizeof(IndexWAL); ++j) {
                if (temp[j].len == 0) {
                    break;
                }
                uint32_t page_no = temp[j].page_no * THREAD_NUM + thread_id;
                if (indexes_[page_no].len == 0) {
                    page_num++;
                }
                if (temp[j].len >= PAGE_SIZE + sizeof(RecordHead)) {
                    compress_failure_cnt++;
                }
                if (temp[j].len >= 0.4 * PAGE_SIZE + sizeof(RecordHead)) {
                    compress_greater50_cnt++;
                }
                indexes_[page_no].off = off;
                indexes_[page_no].len = temp[j].len;
                write_data_bytes += indexes_[page_no].len;
                off += temp[j].len;
                info_.num++;
            }
            info_.off = off;
            uint32_t write_len = off;
            info_.head = info_.tail = write_len;
            index_head = index_tail = info_.num;
            if (write_len % BUFFER_SIZE) { // 恢复buffer
                assert(pread(data_fd_, buffer_, BUFFER_SIZE, write_len / BUFFER_SIZE * BUFFER_SIZE));
            }
            if (index_head % MAX_INDEX_WAL_INCREMENT_NUM) { // 恢复预写index
                uint32_t r_off = (index_head * sizeof(IndexWAL)) / INDEX_WAL_BUFFER_SIZE * INDEX_WAL_BUFFER_SIZE;
                assert(pread(index_fd_, increment_, INDEX_WAL_BUFFER_SIZE, r_off));
            }
            munmap(temp, st.st_size);
            compression_ratio = double(write_data_bytes) / double(page_num * PAGE_SIZE);
        } else { // 首次启动
//            fallocate(data_fd, FALLOC_FL_KEEP_SIZE, 0, 1024 * 1024 * 256);//设置256M文件大小
        }
        ring_array_ = new RingArray(compression_ratio);
    }

    void sync_data() {
        if (is_data_dirty) {
            uint32_t s_off = info_.tail / DIRECT_BLOCK_SIZE * DIRECT_BLOCK_SIZE;
            uint32_t e_off = (uint32_t) std::ceil((double) (info_.head) / DIRECT_BLOCK_SIZE) * DIRECT_BLOCK_SIZE;
            auto len = e_off - s_off;
            assert(len == pwrite(data_fd_, buffer_ + (s_off % BUFFER_SIZE), len, s_off));
            info_.tail = info_.head;
            is_data_dirty = false;
        }
    }

    void sync_index() {
        if (is_index_dirty) {
            uint32_t s_off = (index_tail * sizeof(IndexWAL)) / DIRECT_BLOCK_SIZE * DIRECT_BLOCK_SIZE;
            uint32_t e_off =
                    (uint32_t) std::ceil((double) (index_head * sizeof(IndexWAL)) / DIRECT_BLOCK_SIZE) *
                    DIRECT_BLOCK_SIZE;
            auto len = e_off - s_off;
            assert(len == pwrite(index_fd_, (char *) increment_ + (s_off % INDEX_WAL_BUFFER_SIZE), len, s_off));
            atomic_fetch_sub(no_sync_num_, index_head - index_tail);
            index_tail = index_head;
            is_index_dirty = false;
        }
    }

    void write_record(Record *record) {
        uint32_t page_no = record->header.record_page_no * THREAD_NUM + thread_id_;
        uint16_t capacity = PAGE_SIZE - (info_.head % PAGE_SIZE); // buffer剩余容量
        uint16_t write_len = 0; // 记录写入长度
        auto &record_len = record->header.record_len;
        if (record_len <= capacity) { // 容量足够写入记录
            memcpy(buffer_ + (info_.head % BUFFER_SIZE), record, record_len);
            uint16_t new_capacity = capacity - record_len;
            info_.head += record_len;
            write_len += record_len;
            if (new_capacity <= sizeof(RecordHead)) { // 新的容量不够存1字节数据
                info_.head += new_capacity;
                write_len += new_capacity;
            }
            is_data_dirty = true;
        } else { // 记录需要跨页
            auto second_record_len = record_len - capacity + sizeof(RecordHead); // 第二页的记录长度
            record_len = capacity;
            memcpy(buffer_ + (info_.head % BUFFER_SIZE), record, capacity);
            info_.head += capacity;
            write_len += capacity;
            is_data_dirty = true;
            if (info_.head % BUFFER_SIZE == 0) { // buffer满了
                sync_data();
            }
            record_len = second_record_len;
            // 移动数据到第二页
            memmove(record->data, record->data + capacity - sizeof(RecordHead), second_record_len - sizeof(RecordHead));
            memcpy(buffer_ + (info_.head % BUFFER_SIZE), record, record_len);
            info_.head += record_len;
            write_len += record_len;
            is_data_dirty = true;
        }
        if (info_.head % BUFFER_SIZE == 0) { // buffer满了
            sync_data();
        }
        indexes_[page_no].off = info_.off;
        indexes_[page_no].len = write_len;
        uint32_t cur_inc_idx = (index_head++) % MAX_INDEX_WAL_INCREMENT_NUM;
        increment_[cur_inc_idx].page_no = record->header.record_page_no;
        increment_[cur_inc_idx].len = write_len;
        is_index_dirty = true;
        if (index_head % MAX_INDEX_WAL_INCREMENT_NUM == 0) { // 索引满了
            sync_data();
            sync_index();
            memset(increment_, 0, INDEX_WAL_BUFFER_SIZE); // 不可去除
        }
        info_.off += write_len;
        info_.num++;
        write_data_bytes += write_len;
        compression_ratio = double(write_data_bytes) / (info_.num * PAGE_SIZE);
    }

    [[nodiscard]] Cache *load_cache(uint32_t cache_no) {
        auto cache = ring_array_->get(cache_no);
        if (cache == nullptr) {
            assert(PAGE_SIZE == pread(data_fd_, temp_, PAGE_SIZE, cache_no * PAGE_SIZE));
            uint32_t base_off = cache_no * PAGE_SIZE;
            uint32_t off = base_off;
            uint32_t end_off = base_off + PAGE_SIZE;
            uint16_t cache_size = 0;
            uint32_t out_off = 0;
            bool has_out = false;
            std::vector<Index> out_indexes;
            while (off < end_off - sizeof(RecordHead)) {
                auto *record = (Record *) ((char *) temp_ + (off - base_off));
                uint32_t page_no = record->header.record_page_no * THREAD_NUM + thread_id_;
                if (is_in_interval(off, &indexes_[page_no])) {
                    cache_size += record->header.record_len;
                    if (has_out) {
                        Index *out_index = &out_indexes.emplace_back();
                        out_index->off = out_off;
                        out_index->len = off - out_off;
                        has_out = false;
                    }
                } else {
                    if (!has_out) {
                        out_off = off;
                        has_out = true;
                    }
                }
                off += record->header.record_len;
            }
            if (has_out) {
                Index *out_index = &out_indexes.emplace_back();
                out_index->off = out_off;
                out_index->len = off - out_off;
            }
            cache = ring_array_->add(cache_no, out_indexes, cache_size);
            uint32_t w_off = 0;
            uint32_t last_out_end = cache_no * PAGE_SIZE;
            for (const auto &out_index: out_indexes) {
                if (out_index.off - last_out_end > 0) {
                    auto l = out_index.off - last_out_end;
                    memcpy(cache->data() + w_off, temp_ + (last_out_end % PAGE_SIZE), l);
                    w_off += l;
                }
                last_out_end = out_index.off + out_index.len;
            }
            if (w_off < cache_size) {
                auto l = cache_size - w_off;
                memcpy(cache->data() + w_off, temp_ + (last_out_end % PAGE_SIZE), l);
                w_off += l;
            }
            miss_num++;
        } else {
            hit_num++;
        }
        return cache;
    }

    void read_record(Record *record) {
        uint32_t page_no = record->header.record_page_no * THREAD_NUM + thread_id_;

        uint32_t off = indexes_[page_no].off;
        uint16_t write_len = indexes_[page_no].len;
        uint32_t cache_no = off / PAGE_SIZE;
        if (cache_no >= info_.tail / BUFFER_SIZE * BUFFER_SIZE / PAGE_SIZE) { // 数据在buffer
            auto tmp = (Record *) (buffer_ + (off % BUFFER_SIZE));
            memcpy(record, tmp, tmp->header.record_len);
            if (write_len > sizeof(RecordHead) + tmp->header.record_len) { // 第二页还有数据
                auto second_off = (cache_no + 1) * PAGE_SIZE;
                auto second_tmp = (Record *) (buffer_ + (second_off % BUFFER_SIZE));
                record->header.record_len += second_tmp->get_data_len();
                memcpy(record->data + tmp->get_data_len(), second_tmp->data, second_tmp->get_data_len());
            }
            return;
        }
        if (no_cache) { // 不走缓存
            uint32_t s_off = off / DIRECT_BLOCK_SIZE * DIRECT_BLOCK_SIZE;
            uint32_t e_off =
                    (uint32_t) std::ceil((double) (off + write_len) / DIRECT_BLOCK_SIZE) * DIRECT_BLOCK_SIZE;
            auto len = e_off - s_off;
            assert(pread(data_fd_, temp_, len, s_off));
            auto record_first = (Record *) (temp_ + (off - s_off));
            record->header.record_len = record_first->header.record_len;
            memcpy(record->data, record_first->data, record_first->get_data_len());
            if (write_len > sizeof(RecordHead) + record_first->header.record_len) { // 第二页还有数据
                Record *record_second;
                auto record_first_data_len = record_first->get_data_len();
                if (cache_no + 1 >= info_.tail / BUFFER_SIZE * BUFFER_SIZE / PAGE_SIZE) { // 第二页数据在buffer
                    record_second = (Record *) buffer_;
                } else {
                    record_second = (Record *) (temp_ + ((cache_no + 1) * PAGE_SIZE - s_off));
                }
                record->header.record_len += record_second->get_data_len();
                memcpy(record->data + record_first_data_len, record_second->data, record_second->get_data_len());
            }
        } else {
            auto cache_first = load_cache(cache_no);
            auto record_first = cache_first->read_buf(off);
            record->header.record_len = record_first->header.record_len;
            memcpy(record->data, record_first->data, record_first->get_data_len());
            if (write_len > sizeof(RecordHead) + record_first->header.record_len) { // 第二页还有数据
                Record *record_second;
                auto record_first_data_len = record_first->get_data_len();
                if (cache_no + 1 >= info_.tail / BUFFER_SIZE * BUFFER_SIZE / PAGE_SIZE) { // 第二页数据在buffer
                    record_second = (Record *) buffer_;
                } else {
                    auto cache_second = load_cache(cache_no + 1);
                    record_second = cache_second->read_buf((cache_no + 1) * PAGE_SIZE);
                }
                record->header.record_len += record_second->get_data_len();
                memcpy(record->data + record_first_data_len, record_second->data, record_second->get_data_len());
            }
            if (hit_num + miss_num > 100 && hit_num < 5) {
                no_cache = true;
            }
        }
    }

    [[nodiscard]] bool is_dirty() const {
        return is_data_dirty || is_index_dirty;
    }

    void sync() {
        sync_data();
        sync_index();
    }
};

class Meta {
public:
    MetaPublic *item{nullptr};
    Index *indexes_{nullptr};
    ZSTD_CCtx *cctx_{nullptr};
    ZSTD_DCtx *dctx_{nullptr};
    int32_t level{-1};
    uint32_t thread_id_{THREAD_NUM};
    std::mutex mutex_;
    Record record{};


public:
    explicit Meta(uint32_t thread_id, Index *indexes, int data_fd, int index_fd, std::atomic_int64_t *no_sync_num) {
        thread_id_ = thread_id;
        indexes_ = indexes;
        item = new MetaPublic(thread_id, indexes_, data_fd, index_fd, no_sync_num);
        // 自适应压缩级别
        if (item->compress_failure_cnt > 0) {
            level = 1;
        }
        if (level < 0 && item->info_.num > 100 && double(item->compress_greater50_cnt) / item->info_.num > 0.2) {
            level = 1;
        }
    };

    ~Meta() {
        if (cctx_ != nullptr) {
            ZSTD_freeCCtx(cctx_);
            cctx_ = nullptr;
        }
        if (dctx_ != nullptr) {
            ZSTD_freeDCtx(dctx_);
            dctx_ = nullptr;
        }
        indexes_ = nullptr;
    }

    ZSTD_CCtx *init_cctx() {
        if (cctx_ == nullptr) {
            cctx_ = ZSTD_createCCtx();
            ZSTD_CCtx_setParameter(cctx_, ZSTD_c_checksumFlag, 0); // 禁用校验和
            ZSTD_CCtx_setParameter(cctx_, ZSTD_c_windowLog, 10);
            ZSTD_CCtx_setParameter(cctx_, ZSTD_c_hashLog, 6);
            ZSTD_CCtx_setParameter(cctx_, ZSTD_c_chainLog, 6);
            ZSTD_CCtx_setParameter(cctx_, ZSTD_c_strategy, 1);
            ZSTD_CCtx_setParameter(cctx_, ZSTD_c_ldmHashLog, 6);
            ZSTD_CCtx_setParameter(cctx_, ZSTD_c_ldmBucketSizeLog, 1);
            ZSTD_CCtx_setParameter(cctx_, ZSTD_c_jobSize, 256 * 1024);
        }
        return cctx_;
    }

    ZSTD_DCtx *init_dctx() {
        if (dctx_ == nullptr) {
            dctx_ = ZSTD_createDCtx();
        }
        return dctx_;
    }

    void add(uint32_t page_no, const void *buf) {
        thread_local ZSTD_CCtx *cctx = init_cctx();
        record.header.record_len = compress_page(cctx, buf, record.data, sizeof(record.data)) + sizeof(RecordHead);
        record.header.record_page_no = page_no / THREAD_NUM;
        item->write_record(&record);
    }

    void get(uint32_t page_no, void *buf) {
        thread_local ZSTD_DCtx *dctx = init_dctx();
        record.header.record_page_no = page_no / THREAD_NUM;
        item->read_record(&record);
        uint16_t data_len = record.get_data_len();
        if (data_len == PAGE_SIZE) { // 压缩失败读取原始数据
            memcpy(buf, record.data, PAGE_SIZE);
        } else {
            decompressed_page(dctx, buf, record.data, data_len, page_no);
        }
    }

    uint16_t compress_page(ZSTD_CCtx *cctx, const void *buf, void *dst, uint16_t capacity) {
        size_t dest_len = ZSTD_compressCCtx(cctx, dst, capacity, buf, PAGE_SIZE, level);
        if (dest_len > PAGE_SIZE || ZSTD_isError(dest_len)) { // 压缩失败
            // 压缩失败存原始数据
            memcpy(dst, buf, PAGE_SIZE);
            dest_len = PAGE_SIZE;
            level = 1;
        } else if (level < 0 && (uint32_t) dest_len > PAGE_SIZE * 0.4) { // 压缩效果不佳
            if (item->info_.num > 100 &&
                double(item->compress_greater50_cnt) / item->info_.num > 0.2) { // 压缩效果不佳超过20%提升压缩级别
                level = 1;
            }
            item->compress_greater50_cnt++;
        }
        assert(dest_len <= capacity);
        return dest_len;
    }

    static void decompressed_page(ZSTD_DCtx *dctx, void *buf, const void *src, uint16_t data_len, uint32_t page_no) {
        size_t decompressed_size = ZSTD_decompressDCtx(dctx, (void *) buf, PAGE_SIZE, (const void *) src, data_len);

        if (ZSTD_isError(decompressed_size) || decompressed_size != PAGE_SIZE) {
            std::cout << page_no << std::endl;
        }
        assert(!ZSTD_isError(decompressed_size) && decompressed_size == PAGE_SIZE);
    }

    [[nodiscard]] bool is_dirty() const {
        return item->is_dirty();
    }

    void sync() const {
        item->sync();
    }
};

class DummyEngine : public PageEngine {
private:
    Meta *metas_[THREAD_NUM]{};
    Index *indexes_{nullptr};
    std::atomic_uint8_t work_num_{0};
    std::atomic_int64_t no_sync_num_{0};
    std::string data_path_;

private:
    Meta *load_meta(uint32_t thread_id);

    void sync();

public:
    static RetCode Open(const std::string &path, PageEngine **eptr);

    explicit DummyEngine(const std::string &path);

    ~DummyEngine() override;

    RetCode pageWrite(uint32_t page_no, const void *buf) override;

    RetCode pageRead(uint32_t page_no, void *buf) override;
};

#endif // PAGE_ENGINE_DUMMY_ENGINE_H_