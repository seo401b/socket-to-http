# [Step 5] fcntl 기반 Non-blocking I/O 및 에러 처리 고도화

## 1. 개요 (Overview)
* **목표:** 기존 동기식 블로킹(Blocking) I/O 모델의 스레드 멈춤 현상을 극복하기 위해 `fcntl` 시스템 콜을 활용하여 소켓을 논블로킹(`O_NONBLOCK`) 모드로 전환하고, `EAGAIN` / `EWOULDBLOCK` 예외 처리 및 지연 생성(Lazy Initialization) 기반의 소켓 자원 관리 구조를 구축한다.
* **학습 키워드:** `fcntl`, `O_NONBLOCK`, `F_GETFL` / `F_SETFL`, Non-blocking I/O, `EAGAIN` / `EWOULDBLOCK`, 소켓 속성 미상속, 지연 생성(Lazy Initialization), RAII 소켓 초기화.

---

## 2. 핵심 아키텍처 및 클래스 설계 원칙

### ① 단일 스레드 논블로킹 제어 흐름
* **리스닝 소켓 논블로킹:** `Accept()` 호출 시 클라이언트 접속 요청이 없어도 스레드가 대기하지 않고 즉시 `-1`을 반환하며, `errno`가 `EAGAIN`으로 설정되어 다음 로직으로 진행한다.
* **통신 소켓 논블로킹:** `Recv()` 호출 시 커널 수신 버퍼에 읽을 데이터가 없어도 스레드가 대기하지 않고 즉시 `-1`을 반환하며 다음 루프 주기까지 실행 흐름을 유지한다.

### ② C++ 커널 자원 관리 클래스(RAII & Lazy Initialization) 설계 규칙
* **기본 생성자의 '빈 껍데기' 상태 보장:** `TcpSocket client;` 선언 시점에는 `socket()` 시스템 콜을 호출하지 않고 `sock_fd_ = -1` (유효하지 않은 상태)로 설정해야 `IsValid()` 검사가 올바르게 동작한다.
* **지연 생성 (Lazy Initialization):** 실제 커널 자원(`socket()`)은 `Bind()` 또는 `Connect()`가 명시적으로 호출되는 시점에 할당한다.
* **이동 대입(Move Assignment)을 통한 자원 초기화:** `client = TcpSocket();` 대입 시 이동 대입 연산자가 기존 소켓 FD를 `close()`하고 새로운 빈 객체(`sock_fd_ = -1`)로 상태를 완전 리셋한다.

---

## 3. 깊이 있는 개념 정리 (Deep Dive)

### ① 시스템 콜 동작 방식 비교 (Blocking vs Non-blocking)
소켓 모드 설정에 따른 Kernel Space와 User Space 간의 제어권 반환 특성 차이는 다음과 같다.

| 구 분 | Blocking I/O | Non-blocking I/O (`O_NONBLOCK`) |
| :--- | :--- | :--- |
| **I/O 대기 시 동작** | 조건 만족(데이터 도착/접속) 전까지 스레드 정지 | 커널이 제어권을 즉시 반환 (`return -1`) |
| **데이터 없음 상태** | 스레드가 Sleep 상태로 차단됨 | `errno == EAGAIN` 또는 `EWOULDBLOCK` 설정 |
| **주요 활용 아키텍처**| Multi-Threaded Server (Thread-per-Client) | Event-Driven / Reactor Pattern (`epoll`, `kqueue`) |

### ② `fcntl` 비트 연산 제어 (`F_GETFL` 및 `F_SETFL`)
* **기존 플래그 보존:** 소켓 디스크립터에는 `O_NONBLOCK` 외에도 OS 기본 플래그들이 설정되어 있다.
* **비트 OR 연산 (`|`):** `fcntl(fd, F_GETFL, 0)`로 읽어온 기존 비트 마스크에 `flags | O_NONBLOCK`을 수행한 뒤 `F_SETFL`로 적용해야 기존 제어 속성을 파괴하지 않고 논블로킹 모드만 추가할 수 있다.

### ③ 소켓 속성 미상속 제어
* **독립적 파일 디스크립터:** `server.Accept()`는 커널 대기 큐에서 연결을 확인한 뒤 새로운 자식 소켓 FD를 생성하여 반환한다.
* **상속 불가능 원칙:** POSIX 규격 상 부모 소켓(`server`)이 논블로킹 모드일지라도 새로 생성된 자식 소켓(`client`)은 OS 기본값인 **블로킹 모드**로 초기화된다. 수신 지점에서의 블로킹을 막으려면 자식 소켓 생성 직후 `client.SetNonBlocking()`을 명시적으로 재호출해야 한다.

### ④ 1:1 통신 구조의 한계와 `epoll` 필연성
* **현재 단일 소켓 변수의 한계:** `TcpSocket client;` 변수 하나만 다루는 현재 구조는 동시에 1명의 손님만 처리할 수 있다.
* **`std::vector` 배열 감시의 문제:** N명의 클라이언트를 다루기 위해 소켓 배열을 만들어 `for` 문으로 `Recv()`를 순회하면, 데이터가 없는 소켓에도 계속 시스템 콜을 던져 CPU 연산 비용이 $O(N)$으로 폭주한다.
* **`epoll` 도입 목적:** 커널이 데이터가 도착한 소켓만 $O(1)$로 집어 통지해 주는 이벤트 루프(Reactor Pattern)로 전환하여 비동기 동시성을 확보해야 한다.

---

## 4. 트러블슈팅 노트 (Troubleshooting)

### ① 기본 생성자의 자원 즉시 할당으로 인한 `ENOTCONN` / `recv err` 무한 발생
* **문제 현상:** 서버 실행 즉시 클라이언트 접속이 없는데도 콘솔에 `recv err` (ENOTCONN: Socket is not connected) 로그가 무한 출력됨.
* **원인 분석:** `TcpSocket` 기본 생성자 내부에서 `socket()` 시스템 콜을 즉시 호출하도록 구현되어 있어, 변수 선언(`TcpSocket client;`) 시점에 이미 `sock_fd_ >= 0` (유효함) 상태가 됨. 이로 인해 `if (!client.IsValid())` 조건을 통과하지 못해 `Accept()`를 건너뛰고, 연결되지 않은 껍데기 소켓에 `Recv()`를 호출함.
* **해결 방법:** 기본 생성자는 `sock_fd_(-1)`로 비워두는 지연 생성(Lazy Initialization) 기법을 적용하고, `Bind()`나 `Connect()` 호출 시점에 `socket()`을 생성하도록 클래스를 수정하여 객체 상태와 커널 자원 상태를 일치시킴.

### ② 자식 소켓 논블로킹 설정 누락으로 인한 서버 Hang 현상
* **문제 현상:** `server.SetNonBlocking()`을 적용했음에도, 클라이언트 연결 후 메시지를 보내지 않으면 서버 전체가 `Recv()` 지점에서 영구 정지(Hang)됨.
* **원인 분석:** POSIX 규격에 따라 `Accept()`로 생성된 자식 소켓 FD는 부모의 논블로킹 설정을 상속받지 않고 블로킹 모드로 동작함.
* **해결 방법:** `Accept()` 성공 직후 반환된 클라이언트 소켓 객체에 `client.SetNonBlocking()`을 명시적으로 호출하여 비동기 모드로 전환.

### ③ `EAGAIN` / `EWOULDBLOCK` 처리 미비로 인한 메인 루프 강제 종료
* **문제 현상:** 접속 요청이나 데이터 수신이 없을 때 서버가 `Recv Error` 또는 `Accept Error` 로그를 남기고 루프를 탈출함.
* **원인 분석:** 논블로킹 모드에서 데이터가 없는 상태는 시스템 콜이 `-1`을 반환하고 `errno`를 `EAGAIN` 또는 `EWOULDBLOCK`으로 설정함. 이를 치명적 에러로 잘못 오인하여 루프 종료 조건으로 처리함.
* **해결 방법:** 반환값이 `-1`일 때 `errno == EAGAIN || errno == EWOULDBLOCK`을 검사하여 조건 충족 시 에러 처리를 스킵하고 다음 루프로 진행시킴.

### ④ 단일 소켓 변수 재사용 시 이전 자원 미해결로 인한 무한 Recv 시도
* **문제 현상:** 클라이언트가 접속을 끊어도 서버가 끊김 상태를 인지하지 못하고 유효하지 않은 소켓에 지속적으로 `Recv()` 시스템 콜을 시도함.
* **원인 분석:** 단일 스레드 구조 특성상 `client` 변수가 루프 바깥에 선언되어 있어, 연결이 끊어진 후에도 `client.IsValid()`가 계속 `true` 상태로 남음.
* **해결 방법:** `len == 0` (FIN 수신) 또는 치명적 에러 발생 시 `client = TcpSocket();` 대입을 실행하여 RAII 이동 대입으로 이전 소켓 FD를 `close()`하고 객체 내부 `sock_fd_` 상태를 `-1`로 리셋.

---

## 5. Next Step

### `epoll` 기반 I/O 멀티플렉싱 도입
단일 스레드 단일 소켓 제어 방식은 1:1 통신만 가능하며, `sleep_for` 대기 시간으로 인한 반응성 저하 및 무한 Polling 루프로 인한 CPU 자원 낭비 한계를 지닌다. 이를 극복하기 위해 커널 레벨에서 수천 개의 소켓 이벤트를 감시하고 통지해 주는 Linux I/O 멀티플렉싱 커널 객체를 작성한다.

* **주요 과제**
    1. `epoll_create1`, `epoll_ctl`, `epoll_wait` 시스템 콜 인터페이스 이해 및 C++ Wrapper 클래스 설계
    2. 소켓 등록/수정/삭제 (`EPOLL_CTL_ADD`, `EPOLL_CTL_DEL`) 및 이벤트 비트 마스크(`EPOLLIN`, `EPOLLET`) 설정
    3. `while(true)` Polling 방식을 커널 이벤트 대기 기반의 **Reactor Pattern (Event Loop)** 아키텍처로 완전 전환