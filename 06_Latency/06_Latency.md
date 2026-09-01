# [Step 4] Thread-per-Client 모델의 Latency 성능 측정 및 C10K 한계 검증

## 1. 개요 (Overview)
* **목표:** Thread-per-Client 아키텍처 환경에서 클라이언트 접속 수 증가에 따른 서버 Latency(지연 시간) 폭증 현상과 OS 스레드 생성 한계(`Resource temporarily unavailable`)를 직접 측정하고, 멀티스레드 블로킹 서버의 C10K 한계를 검증한다.
* **학습 키워드:** Thread-per-Client, Latency 측정, Context Switch 오버헤드, C10K 문제, OS 스레드 한계 (`ulimit`), `std::system_error`, RAII 소켓 래퍼.

---

## 2. 핵심 아키텍처 및 클래스 설계 원칙

### ① 서버 단의 순수 처리 지연 시간(Latency) 측정
* **커널/스레드 경합 지연 격리:** 클라이언트 RTT(Round Trip Time)가 아닌, 서버 내부에서 `Recv` 완료 시점부터 `Send` 완료 시점까지의 `high_resolution_clock` 수치를 측정하여 네트워크 이동 지연을 배제하고 pure 커널 스케줄링 지연만 측정한다.
* **I/O 병목 최소화:** 콘솔 출력(`std::cout`) 자체가 발생하는 I/O 병목을 줄이기 위해 일정 조건(지연 시간 스파이크 또는 주기적 샘플링) 만족시에만 출력을 수행하도록 설계한다.

### ② RAII 소켓 래퍼(TcpSocket) 기반 자원 관리
* **자원 누수 방지:** 저수준 C POSIX 소켓 API를 C++ RAII 클래스로 감싸, 스레드 종료나 예외 발생 시 소멸자를 통해 파일 디스크립터(`close`)가 자동 해제되도록 한다.
* **소유권 이관:** `Accept()`로 생성된 클라이언트 소켓은 Move Semantics(`std::move`)를 활용해 독립된 처리 스레드로 이동시켜 소켓 자원의 단일 소유권을 보장한다.

### ③ 부하 발생기(Load Generator) 클라이언트 설계
* **동시 접속 폭격:** 멀티스레드 기반으로 대량의 소켓 접속을 순식간에 유도하여 서버의 `Listen Backlog` 및 스레드 생성 한계를 시험한다.
* **주기적 트래픽 유지:** 접속 후 단발성 전송으로 끝나지 않고 10ms 단위로 메시지를 주기적 전송하여 서버 스레드 간 CPU 경합을 지속해서 유발한다.

---

## 3. 깊이 있는 개념 정리 (Deep Dive)

### ① 동시 접속 수에 따른 서버 Latency 변화
접속 클라이언트 수(활성 스레드 수)에 따른 서버 처리 지연 시간 관찰 결과는 다음과 같다.

| 접속 클라이언트 수 | 평균 Latency | 주요 원인 및 스레드 상태 |
| :--- | :--- | :--- |
| **10개 (소규모)** | 0.01 ~ 0.05 ms | CPU 코어 여유, 대기 없는 즉각적인 스레드 스케줄링 |
| **300개 이상 (중대규모)** | 5.00 ~ 20.00 ms+ | 스레드 문맥 교환(Context Switch) 폭증 및 CPU 캐시 미스 발생 |

### ② Thread-per-Client 아키텍처의 C10K 구조적 한계
* **메모리 오버헤드:** 스레드당 기본 할당되는 스택 메모리(Linux 기본 8MB)로 인해 1만 개 접속(C10K) 시 수십 GB의 RAM이 단순 스레드 스택 유지비로 고갈된다.
* **Context Switch 비용 폭증:** 스레드 수가 CPU 코어 수를 초과하면 OS 커널 스케줄러가 스레드를 교체하는 데 대부분의 CPU 시간을 소모하여 실제 I/O 처리 효율이 급격히 떨어진다.

### ③ OS 스레드 한계와 예외 발생 메커니즘
* **커널 시스템 제한:** 리눅스 커널의 `ulimit -u` (최대 프로세스/스레드 수) 제한에 도달하면 `pthread_create()`가 실패하고 `EAGAIN` (`Resource temporarily unavailable`) 에러를 반환한다.
* **C++ 예외 전환:** C++ `std::thread` 생성자는 내부적으로 실패 시 `std::system_error` 예외를 던지며, 이를 `try-catch`로 포착하지 않을 경우 `std::terminate()`가 호출되어 코어 덤프와 함께 프로세스가 강제 종료(Crash)된다.

---

## 4. 트러블슈팅 노트 (Troubleshooting)

### ① `Connect fail: Bad file descriptor` 에러 발생
* **문제 현상:** 클라이언트 실행 시 `Connect fail: Bad file descriptor` 메시지가 출력되며 접속에 실패함.
* **원인 분석:** `TcpSocket` 기본 생성자가 `sock_fd_(-1)` 상태로 객체를 생성하는데, `Connect()` 함수 내부에서 소켓 미생성 상태일 때 `socket()`을 새로 할당하는 지연 생성(Lazy Initialization) 로직이 누락되어 유효하지 않은 FD(`-1`)로 `connect()`를 시도함.
* **해결 방법:** `Connect()` 시작 부분에 `if (!IsValid())` 상태를 감지하여 `socket()` 시스템 콜을 호출하는 지연 생성 기법을 추가하고, 실패 시 `Close()`를 수행하여 `sock_fd_ = -1` 상태를 유지하도록 보완함.

### ② 부하 테스트 중 `std::system_error` (Resource temporarily unavailable) Crash
* **문제 현상:** 클라이언트 접속자 수를 500~1000 이상으로 올렸을 때 `terminate called after throwing an instance of 'std::system_error'` 에러와 함께 클라이언트 프로그램이 강제 종료됨.
* **원인 분석:** 스레드 생성 간격 대기 구문을 지우고 단시간에 무수한 스레드를 생성하자 OS의 사용자 스레드 생성 한계(`ulimit -u`)에 걸려 `std::thread` 생성자가 예외를 던짐.
* **해결 방법:** 터미널에서 `ulimit -u 65535`로 OS 한도를 확장하고, 클라이언트 메인 루프의 스레드 생성부를 `try-catch (const std::system_error&)`로 감싸 한도 도달 시에도 프로그램이 죽지 않고 생성 성공한 스레드만으로 테스트를 계속 진행하도록 예외 처리.

### ③ 서버 터미널 콘솔 출력 폭주로 인한 Latency 측정 왜곡
* **문제 현상:** 클라이언트 접속자 수가 300명 이상일 때 서버 터미널에 콘솔 출력이 초당 수만 줄씩 도배되며 서버가 마비됨.
* **원인 분석:** 서버의 출력 조건문이 `active_clients % 50 == 0`으로 설정되어 있어, 접속자가 300명일 때 300개 스레드 전체의 매 메시지마다 `std::cout`이 실행되어 콘솔 I/O 병목이 발생함.
* **해결 방법:** 전체 접속자 수 조건문을 삭제하고, 각 스레드별 메시지 카운트(`msg_count % 500 == 0`) 정기 샘플링 및 `latency > 10000` (10ms 이상 급증) 조건으로 변경하여 출력을 제어함.

### ④ 테스트 시간 종료 후에도 프로세스가 멈추지 않는 현상
* **문제 현상:** 설정한 테스트 유지 시간(예: 3초)이 지난 후에도 클라이언트와 서버 프로세스가 정지하지 않고 계속 대기 상태로 유지됨.
* **원인 분석:** 서버의 처리 지연으로 인해 클라이언트 스레드가 `client.Recv()` 블로킹 호출에 고립(Hang)되어 루프 상단의 시간 체크 조건문까지 흐름이 도달하지 못함. 이로 인해 메인 스레드의 `t.join()`이 종료되지 않은 스레드를 무한 대기함.
* **해결 방법:** 타임아웃 처리 로직을 도입하거나 동기식 블로킹 I/O 구조의 한계를 인지하고 Non-blocking I/O 아키텍처로의 전환 필요성을 확인함.

---

## 5. Next Step

### Non-blocking I/O 및 I/O 멀티플렉싱(`epoll`)으로의 전환
Thread-per-Client 구조는 접속자 수에 비례하여 스레드 및 메모리가 폭증하고 블로킹 I/O 대기로 인해 반응성이 급격히 떨어지는 한계가 명확히 입증되었다. 이를 극복하기 위해 소켓을 논블로킹(`O_NONBLOCK`) 모드로 전환하고, 단일 스레드로 수천 개의 소켓 이벤트를 효율적으로 감시하는 **Linux `epoll` 멀티플렉싱 아키텍처**로 고도화를 진행한다.

* **주요 과제**
    1. `fcntl`을 활용한 소켓 Non-blocking 속성 부여 및 `EAGAIN` / `EWOULDBLOCK` 예외 처리
    2. `epoll_create1`, `epoll_ctl`, `epoll_wait` 기반의 C++ Epoll Wrapper 클래스 구현
    3. 스레드 생성 없이 단일 이벤트 루프에서 대규모 동시 접속을 처리하는 **Reactor Pattern** 구축