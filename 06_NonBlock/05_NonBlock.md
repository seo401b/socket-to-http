 트러블슈팅

TcpSocket 기본 생성자 내부에서 socket() 시스템 콜을 즉시 호출하도록 구현되어 있어, 변수 선언(TcpSocket client;) 시점에 이미 sock_fd_ >= 0 (유효함) 상태가 됨. 이로 인해 if (!client.IsValid()) 조건을 통과하지 못해 Accept()를 건너뛰고, Recv()를 호출함.

기본 생성자는 sock_fd_(-1)로 하고 Bind()나 Connect() 호출 시점에 socket을 생성하도록 클래스를 수정.