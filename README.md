![plot](./banner.png)

# Tianchi x PolarDB: Page Compression Engine

Aiming to implement a page engine that achieves higher data compression rates and page read/write performance.

## Quick Start

Execute following command to build and run local test:

```
$ cmake .
$ make
$ ./bin/random_read_write
```

## Coding

`page_engine/` dir contains sample code using naive pread/pwrite which can pass the test.

`template/` dir contains blank interface templates that can be used as a codebase for implementing your own data page compression engine.

`test/` dir contains local test scripts for different scenarios, which can be run locally before submitting.

## Tips

- Only `page_engine/` dir is available for online judge

## 注意！
- 只有对`page_engine/`目录的修改会在测评中被采用，其他目录仅作为示例和本地测试使用
