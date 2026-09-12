초기 코드:
main() 내부에서 
while(!stop_server){
    {
        std::lock_guard<std::mutex> lock(mtx);
        jobq.push(socket);
    }
    cv.notify_one();
}
이런 코드가 있었음
근데 서버가 멈추지 않는 이상 socket 함수가 계속 실행되며 큐를 무한대로 채우는 문제가 발생함. Terminated 사망. 
gdb로 디버깅해보니

Thread 1 "server" received signal SIGTERM, Terminated.
0x0000555555559d78 in std::function<void ()>::operator bool() const ()
(gdb) bt
#0  0x0000555555559d78 in std::function<void ()>::operator bool() const ()
#1  0x000055555555c428 in std::function<void ()>::function(std::function<void ()>&&) ()
#2  0x000055555555a403 in std::function<void ()>& std::deque<std::function<void ()>, std::allocator<std::function<void ()> > >::emplace_back<std::function<void ()> >(std::function<void ()>&&) ()
#3  0x0000555555559228 in std::deque<std::function<void ()>, std::allocator<std::function<void ()> > >::push_back(std::function<void ()>&&) ()
#4  0x000055555555873a in std::queue<std::function<void ()>, std::deque<std::function<void ()>, std::allocator<std::function<void ()> > > >::push(std::function<void ()>&&) ()
--Type <RET> for more, q to quit, c to continue without paging--
#5  0x000055555555702b in main ()


#5 main 함수 실행 중 뻗었고 그 전까진 #4~#1의 작업을 하던중이었음
그 작업들은 func를 작업 큐에 넣는 것임.
아마 작업 큐가 터져 메모리 부족으로 추정

-----------------------------------------------------------

1차 트러블슈팅:

main() 함수에서 작업 큐에 함수를 넣지 말고
socket 함수에서 클라이언트 할당 받을 때 작업 큐에 넣어볼까?

else if(cev[i].events & EPOLLIN){
    int client_fd = cev[i].data.fd;

    {
        std::lock_guard<std::mutex> lock(mtx);
        jobq.push([client_fd](){
            auto it = clients_map.find(cev[i].data.fd);
            if(it==clients_map.end()) return;

            TcpSocket& client_sock = it->second;
            char buf[1024] = {0,};

            while(1){
                데이터 recv logic
            }
        });
    }
    cv.notify_one();
}

작업 큐는 공유자원이니 mutex 걸어주고
작업 큐 안에 직접 함수를 구현함.

트러블슈팅 노트를 적으며 생각해보니 이것도 초기 코드와 다를게 없는데 왜 이렇게 했지..

결과는 초기 코드와 똑같다.


--------------------------------------------

2차 트슈:
무한 작업 큐에 푸쉬가 되는 이유는 무엇일까?

클라이언트가 데이터 전송 -> EPOLLIN 발생 -> 작업큐에 이벤트 삽입 -> cv.notify_one()으로 스레드 호출 (여기서 시간이 걸림) -> 스레드 호출되거나 처리 중 -> 다시 while(1)로 돌아가 작업큐에 이벤트 삽입 -> 무한 반복

스레드 호출 및 처리 시간을 고려하지 않은 코드다.

해결방법:
epoll 설정 중 EPOLLONESHOT을 활용한다.
이벤트 중복 감지를 막아준다.
데이터를 모두 읽은 후엔 다시 새로운 이벤트를 감지할 수 있도록 epoll_ctl 재등록이 필요하다.


결과:
Thread 1 "server" received signal SIGTERM, Terminated.
0x00005555555584c8 in std::_Function_base::_Function_base() ()
(gdb) bt
#0  0x00005555555584c8 in std::_Function_base::_Function_base() ()
#1  0x00005555555586ae in std::function<void ()>::function<void (&)(), void>(void (&)()) ()
#2  0x0000555555557015 in main ()
(gdb) 


분석:
컴파일 할 때 오류가 났는데 확인 못하고 코드를 돌려 고친 코드가 아닌 기존 코드로 돌았다.

재시도:
성공!


추가 수정:
while(1){
    int nfds = ::epoll_wait(epfd, cev, 1024, -1);
    if(nfds < 0){
        if(errno == EINTR) continue;
        break;
    }
에서 while(!stop_server)로 수정



-------------------------------------------------------

클라이언트 측 오류 발생:
실행하면 바로 connect fail이 뜸
connect fail은 TcpSocket.cpp file에

bool TcpSocket::Connect(const std::string& ip, int port){
    sockaddr_in server_addr;
    std::memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);

    if(inet_pton(AF_INET, ip.c_str(), &server_addr.sin_addr) <= 0){
        std::cerr << "Connect fail\n";
        return  false;
    }

    if(connect(sock_fd_, reinterpret_cast<struct sockaddr*>(&server_addr), sizeof(server_addr)) < 0){
        std::cerr << "Connect fail\n";
        return false;
    }
    return true;
}
코드에 있음
자세한 오류 출력으로 어디가 문제인지 보겠음

std::cerr << "Connect fail: " << std::strerror(errno) << '\n';
추가하면

Bad file descriptor
라는 오류가 남

잘못된 파일 디스크립터를 접근했을 때 이 에러가 나는데
connect할 때 소켓이 생성되지 않은 상태거나 파일 디스크립터를 잘못 받아오는 경우로 추정.
전에는 잘 작동하던 파일이었으므로 어떤 특정한 경우에선 connect 시 소켓이 생성되어 있고
어떤 경우엔 connect 시 소켓이 아직 만들어지지 않았다고 생각.

NonBlockServer는 잘 동작하는데 왜 MultiRsever만 에러가 날까?
NonBlockServer:

while(1){
    if(!client.IsValid()) {
        client = server.Accept();

        if(client.IsValid()){
            client.SetNonBlocking();
            std::cout << "client connect\n";
        } else{
            if(errno != EAGAIN && errno != EWOULDBLOCK){
                std::cerr << "err: " << std::strerror(errno) << '\n';
                break;
            }
        }
    }
.....
}

MultiR:

for(int i = 0; i < nfds; ++i){
    if(cev[i].data.fd == listen_sock.GetFd()){
        TcpSocket client_sock = listen_sock.Accept();
        client_sock.SetNonBlocking();
        ev.events = EPOLLIN | EPOLLET | EPOLLONESHOT;
        ev.data.fd = client_sock.GetFd();
        if(::epoll_ctl(epfd, EPOLL_CTL_ADD, client_sock.GetFd(), &ev) < 0){
            std::cerr << "epoll add fail\n";
            return;
        }
        {
            std::lock_guard<std::mutex> lock(mtx);
            clients_map.emplace(client_sock.GetFd(), std::move(client_sock));
        }
    }


NonBlockServer: while 루프를 돌며 !client.IsValid() 상태를 체크하고 재시도하므로 소켓 생성 타이밍이나 Accept 타이밍이 엇갈려도 묻혀 지나갔을 것.

Multi-Reactor: 소켓이 미처 생성되지 않은 상태에서 Connect()를 시도하거나 서버 측 Accept가 누락되어 시도할 때 에러가드러난 것.

해결:
TcpSocket.cpp에

bool TcpSocket::Connect(const std::string& ip, int port){
    if(!IsValid()){
        sock_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if(sock_fd_ < 0) {
            std::cerr << "socket creation fail\n";
            return false;
        }
    }

로직을 추가.
소켓이 없다면 확실히 소켓을 만들고 커넥해라
