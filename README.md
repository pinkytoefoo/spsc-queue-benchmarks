# libhft benchmark comparison

[![C/C++ CI](https://github.com/pinkytoefoo/spsc-queue-benchmarks/workflows/C/C++%20CI/badge.svg)](https://github.com/pinkytoefoo/spsc-queue-benchmarks/actions)
[![License](https://img.shields.io/badge/license-MIT-blue.svg)](https://raw.githubusercontent.com/rigtorp/SPSCQueue/master/LICENSE)

This repo is to demonstrate my SPSCQueue's implemetation against Erik Rigtorp's and the boost library.
[*boost::lockfree::spsc_queue*](https://www.boost.org/doc/libs/1_76_0/doc/html/boost/lockfree/spsc_queue.html)
and [*rigtorp::SPSCQueue*](https://github.com/rigtorp/SPSCQueue).

## Benchmarks

For the benchmarks, I am using Rigtorp's original benchmarking code, and simply added an adaptation for my
implementation. These results for an AMD Ryzen 5 Pro 7535U CPU:

| Queue                        | Throughput (ops/ms) | Latency RTT (ns) |
| ---------------------------- | ------------------: | ---------------: |
| hft::spsc_queue              |              29380  |              214 |
| rigtorp::SPSCQueue           |              19375  |              164 |
| boost::lockfree::spsc        |              14929  |              227 |

The base of this project was created by [Erik Rigtorp](http://rigtorp.se)
<[erik@rigtorp.se](mailto:erik@rigtorp.se)>.
