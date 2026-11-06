# hft::spsc_queue benchmarks
 
[![C/C++ CI](https://github.com/pinkytoefoo/spsc-queue-benchmarks/workflows/C/C++%20CI/badge.svg)](https://github.com/pinkytoefoo/spsc-queue-benchmarks/actions)
[![License](https://img.shields.io/badge/license-MIT-blue.svg)](https://raw.githubusercontent.com/rigtorp/SPSCQueue/master/LICENSE)
 
A bounded, lock-free single-producer / single-consumer (SPSC) queue in C++ from my [data structures](https://github.com/pinkytoefoo/libhft) repo, benchmarked against two well-known implementations:
 
- [`rigtorp::SPSCQueue`](https://github.com/rigtorp/SPSCQueue)
- [`boost::lockfree::spsc_queue`](https://www.boost.org/doc/libs/release/doc/html/boost/lockfree/spsc_queue.html)

## The optimization
 
Like Rigtorp's queue, each side keeps a *cached copy* of the other side's index, so the hot path usually avoids touching the other thread's cache line at all.
 
Each cached index is only ever accessed by the thread with the real opposite index. So instead of giving the cached indexes their own cache line, I aggregated the index pairs:
 
**hft::spsc_queue** (this repo)
 
```cpp
alignas(kCachelineSize) std::atomic<size_type> writeIdx_{ 0 };
size_type readCacheIdx_{ 0 };   // same cache line as writeIdx_
 
alignas(kCachelineSize) std::atomic<size_type> readIdx_{ 0 };
size_type writeCacheIdx_{ 0 };  // same cache line as readIdx_
```
 
**rigtorp::SPSCQueue**
 
```cpp
alignas(kCachelineSize) std::atomic<size_type> writeIdx_{ 0 };
alignas(kCachelineSize) size_type readCacheIdx_{ 0 };   // seperate cache line
 
alignas(kCachelineSize) std::atomic<size_type> readIdx_{ 0 };
alignas(kCachelineSize) size_type writeCacheIdx_{ 0 };  // seperate cache line
```
 
Since `readCacheIdx_` is only ever touched by the producer (alongside `writeIdx_`), and `writeCachedIdx_` is only ever touched by the consumer (alongside `readIdx_`), we can keep them on the same cache lines because it introduces no extra false sharing. By keeping our current and cached indexes on the same cache line, we can keep reduce the pressure on our cores by simply having 1 cache line being shot up into the core.

## Benchmarks
 
The benchmark code is from Rigtorp's original repo, with an added adaptation for `hft::spsc_queue`. 
Each queue is created with a producer and consumer thread pinned to a CPU core via Linux CPU affinity APIs,
to prevent issues with core migration. Each queue was run 5 times back to back.
 
| Queue                   | Throughput (ops/ms) | Latency RTT (ns) |
| ----------------------- | ------------------: | ---------------: |
| `hft::spsc_queue`       |             312,294 |              160 |
| `rigtorp::SPSCQueue`    |             107,406 |              161 |
| `boost::lockfree::spsc` |              83,393 |              176 |
 
*Mean of 5 runs. Throughput is ~2.9x `rigtorp::SPSCQueue` and ~3.7x `boost::lockfree::spsc`. RTT is on par with Rigtorp's and ~9% lower than Boost's.*
 
| Queue                   | Throughput range (ops/ms) | Median throughput (ops/ms) | RTT range (ns) |
| ----------------------- | ------------------------: | -------------------------: | -------------: |
| `hft::spsc_queue`       |         299,313 – 321,295 |                    319,521 |        158–162 |
| `rigtorp::SPSCQueue`    |          57,501 – 144,808 |                    105,710 |        158–163 |
| `boost::lockfree::spsc` |           78,059 – 89,451 |                     83,891 |        169–195 |
 
<details>
  <summary>Raw runs</summary>
| Queue                   | Run | Throughput (ops/ms) | RTT (ns) |
| ----------------------- | --: | ------------------: | -------: |
| `hft::spsc_queue`       |   1 |             301,761 |      160 |
| `hft::spsc_queue`       |   2 |             319,581 |      159 |
| `hft::spsc_queue`       |   3 |             299,313 |      161 |
| `hft::spsc_queue`       |   4 |             319,521 |      158 |
| `hft::spsc_queue`       |   5 |             321,295 |      162 |
| `rigtorp::SPSCQueue`    |   1 |             105,710 |      161 |
| `rigtorp::SPSCQueue`    |   2 |              57,501 |      158 |
| `rigtorp::SPSCQueue`    |   3 |             123,836 |      162 |
| `rigtorp::SPSCQueue`    |   4 |             105,176 |      161 |
| `rigtorp::SPSCQueue`    |   5 |             144,808 |      163 |
| `boost::lockfree::spsc` |   1 |              84,301 |      195 |
| `boost::lockfree::spsc` |   2 |              83,891 |      170 |
| `boost::lockfree::spsc` |   3 |              81,263 |      169 |
| `boost::lockfree::spsc` |   4 |              78,059 |      172 |
| `boost::lockfree::spsc` |   5 |              89,451 |      174 |
 
</details>

### Notes
 
- **Hardware:** The benchmarks were ran on a AMD Ryzen 5 Pro 7535U CPU.
- **Throughput and RTT:** The throughput gain comes from the producer/consumer hotpath touching fewer cache lines. RTT and latency is dominated remains relatively the same. Although, it is worth noting that Rigtorp's implementation adds additional padding to the underlying buffer to prevent possible false sharing if the buffer data ends up being on the same cache line as some frequently modified variable.
- **Testing variance:** Currently, there was only 3 total trials. The results will differ on different machines and more test iterations should be ran to make these results concrete.

## Building and running
 
```sh
mkdir build && cd build
cmake -G Ninja -DCMAKE_BUILD_TYPE=Release ..
ninja
./SPSCQueueBenchmark
```
 
## Credits
 
The base of this project, including the benchmark harness, was created by [Erik Rigtorp](http://rigtorp.se) <[erik@rigtorp.se](mailto:erik@rigtorp.se)>.
 
## License
 
MIT. See [LICENSE](LICENSE).
 
