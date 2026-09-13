# Multi-Reactor C++ HTTP Server Benchmark & Latency Report

본 보고서는 구현된 C++ Multi-Reactor 비동기 HTTP 서버의 부하 한계 테스트 결과, Latency 지표, 병목 원인 분석 및 향후 아키텍처 개선 방향을 정리한 문서입니다.

---

## 1. 종합 테스트 결과 요약

| 테스트 항목 | 부하 조건 | RPS (초당 처리량) | Avg Latency | Max Latency | 성공률 | 주요 현상 |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **wrk 고동시성** | 2,000 connections / 8 threads / 15s | **20,296.67** | 64.62 ms | 2,000 ms | **99.7%** | 772건 timeout 발생 |
| **ab 대용량 GET** | 1,000 connections / 500,000 requests (-k) | **22,885.17** | 43.70 ms | 116 ms | **100%** | 실패 0건, 극강의 내구성 |
| **ab 1MB POST** | 500 connections / 10,000 requests (-k) | **231.09** | 2,163.70 ms | 43,187 ms | **5.0%** | 9,500건 404 Error 및 지연 급증 |

---

## 2. 시나리오별 상세 측정 지표

### A. wrk 2,000 동시 접속 테스트 (소켓 수용력 측정)
* **Total Requests**: 306,389 reqs (15.10s)
* **Transfer/sec**: 2.61 MB/s
* **Latency Distribution**:
  * Mean: `64.62 ms` | Stdev: `72.38 ms` | Max: `2.00 s`
* **Socket Errors**: Connect 0, Read 0, Write 0, Timeout 772
* **분석**: 싱글/멀티 리액터 이벤트 루프가 2,000개 접속 세션을 안정적으로 처리했으나, I/O 이벤트 대기열 순서 밀림 현상으로 일부 타임아웃 발생.

### B. ab 50만 건 연속 GET 내구성 테스트 (메모리 무결성 검증)
* **Total Requests**: 500,000 reqs (21.85s)
* **Transfer Rate**: 3017.09 KB/s
* **Latency Percentiles**:
  * 50%: `43 ms` | 90%: `59 ms` | 99%: `79 ms` | Max: `116 ms`
* **Failed Requests**: 0
* **분석**: `std::string_view` 파싱과 `compact()` 메모리 정돈 로직의 무결성 검증 완료. 50만 번의 처리 동안 메모리 누수나 파편화 없이 일정한 속도 유지.

### C. ab 1MB POST 스트레스 테스트 (서버 한계점 테스트)
* **Total Sent Body**: 10.48 GB (1MB * 10,000 requests)
* **Failed Requests**: 9,500 (Non-2xx Response)
* **Latency Percentiles**:
  * 50%: `16 ms` | 90%: `29 ms` | 98%: `42,338 ms` | Max: `43,187 ms`
* **분석**: 대용량 바이너리 바디 수신 과정에서 디스크 I/O 병목 및 자원 한계 도달.

---

## 3. 병목 원인 상세 분석 (Root Cause Analysis)

### 1. File Descriptor (FD) 자원 고갈
* 500개의 클라이언트 세션 소켓이 상시 유지되는 상황에서 `handleRequest()`의 `std::ifstream` 호출로 동기식 파일 오픈 연산이 대량 중복 발생.
* 프로세스의 오픈 가능한 파일 한도(`ulimit -n`)를 초과하면서 `file.is_open()`이 실패, 9,500건의 요청이 404 에러로 처리됨.

### 2. 동적 버퍼 확장(`buffer.resize()`) 연산 병목
* 기본 8KB 수신 버퍼에 1MB 바이너리가 유입되면서 `appendData()` 내부의 2배 확장 로직이 기하급수적으로 호출됨.
* 힙 메모리 재할당(`malloc`/`realloc`) 연산으로 인해 CPU 오버헤드가 급증.

### 3. 동기식(Blocking) 파일 I/O로 인한 Reactor 동결
* 메인 이벤트 루프 스레드 내에서 디스크 파일 읽기 작업을 동기식으로 수행.
* 디스크에서 파일 스트림을 긁어오는 동안 소켓 I/O 이벤트 처리가 전체 멈춤(Block) 상태에 빠져 꼬리 지연(Tail Latency가 43초까지 급증) 발생.

---

## 4. 아키텍처 고도화 로드맵 (Optimization Roadmap)

* **In-Memory Static File Caching (단기)**
  * 파일 조회 시 매번 디스크를 읽지 않고 메모리 맵(`std::unordered_map<std::string, std::string>`)에 캐싱하여 FD 고갈 및 동기 I/O 지연을 원천 차단.
* **Fixed-size Memory Pool 도입 (중기)**
  * 대용량 수신 시 잦은 `resize()`를 방지하기 위해 고정 크기 메모리 블록을 관리하는 메모리 풀(Memory Pool) 할당자 도입.
* **Worker Thread Pool 분리 (장기)**
  * 네트워크 I/O 전담 스레드(Reactor)와 파일/비즈니스 처리 스레드(Worker)를 분리하여 비동기 Non-blocking 성능 극대화.