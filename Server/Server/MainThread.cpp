#include <iostream>
#include <string>

#include "Player.h"
#include "Enemy.h"
#include "Item.h"
#include "Bullet.h"
#include "Obstacle.h"
#include "Common.h"
#include "Packet.h"
#include <random>
#include <vector>
#include <list>
#include"Client.h"
#include "GameThread.h"
#include <thread> 
#define SERVERPORT 9000
#define BUFSIZE    512
#define FRAME_TIME 0.033f


using namespace std;

list<c_inputPacket> sharedInputList;
list<Client> waitClientList; // 전역(gameThread 에서 이용하기 위해) 수정필요
 CRITICAL_SECTION cs;                // 전역으로 Critical Section 선언 //Client 클래스 헤더파일로 분리 
 int nextID = 1; // 전역 변수 nextID 정의

//list<Client> waitClientList;
HANDLE hGameStartEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
bool gameOver = false;
//CRITICAL_SECTION cs;

vector<s_enemyPacket> enemies = {};
vector<s_obstaclePacket> obstacles = {};
vector<s_bulletPacket> bullets = {};
vector<s_itemPacket> items = {};
vector<s_playerPacket> players = {};

vector<c_bulletPacket> c_bullets = {};
c_bulletPacket recv_bullet;
vector<c_inputPacket> c_inputs = {};
c_playerPacket c_player = {};


// 게임 데이터 받기
void receiveGameData(SOCKET s);
// 게임 데이터 전송
void sendGameData(SOCKET s);
// 게임 결과 전송
void sendResult(SOCKET s, int result);

void waitUntilNextFrame(const std::chrono::time_point<std::chrono::steady_clock>& frameStartTime);


DWORD WINAPI networkThread(LPVOID arg)
{
	SOCKET clientSock = (SOCKET)arg;
	int retval;
	Client client;

	// 닉네임 받기
	retval = recv(clientSock, client.name, sizeof(client.name), 0);
	if (retval == SOCKET_ERROR) {
		err_display("receive - clientName");
	}
	else {
		cout << "recv - name success - " << client.name << endl;
	}

	// ID 할당
	retval = send(clientSock, (char*)&client.ID, sizeof(client.ID), 0);
	if (retval == SOCKET_ERROR) {
		err_display("send - clientID");
	}
	else {
		cout << "send - ID success - " << client.ID << endl;
	}
	while (true)
	{
		// 매칭 신호 수신
		unsigned short start;
		retval = recv(clientSock, (char*)&start, sizeof(start), MSG_WAITALL);
		if (retval == SOCKET_ERROR) err_display("receive - c_playetPacket");
		if (start != GAMESTART) {
			err_display("receive - start");
		}
		else {
			cout << client.ID << " - Matching start" << endl;
		}

		// 리스트에 입장
		EnterCriticalSection(&cs);		// 크리티컬섹션으로 다른스레드와 동기화
		waitClientList.push_back(client);
		LeaveCriticalSection(&cs);
		cout << "enter List" << endl;
		cout << waitClientList.size()<< "개의 클라이언트 대기중" << endl;


		// 게임 잡힐 때 까지 대기
		WaitForSingleObject(hGameStartEvent, INFINITE);
		waitClientList.remove(client);

		cout << "매칭 완료" << endl;

		


		// initPacket 전송
		
		s_initPacket init;
		retval = send(clientSock, (char*)&init, sizeof(init), 0);
		if (retval == SOCKET_ERROR) { 
			err_display("send - initPacket"); 
		}
		else {
			cout << "send - initPacket success - " << client.ID << endl;
		}
		
		// 게임 루프
		
		while (!gameOver)
		{
			//cout << "Game In " << endl;
			auto frameStartTime = std::chrono::steady_clock::now();
			// 2. 게임 데이터 전송
			//sendGameData(clientSock);

			// 1. 클라이언트로부터 데이터 수신
			receiveGameData(clientSock);
			waitUntilNextFrame(frameStartTime);
		}
		// 게임 결과 전송
		sendResult(clientSock, 0);
		cout << "Game End " << endl;
	}
	closesocket(clientSock);
	nextID--;
}

void sendGameData(SOCKET s)
{
	if (s == INVALID_SOCKET) err_display("invalid socket");

	int retval, dataSize;

	// s_enemyPacket 전송
	dataSize = enemies.size() * sizeof(s_enemyPacket);
	retval = send(s, (char*)&dataSize, sizeof(int), 0);
	if (retval == SOCKET_ERROR) { err_display("send - enemyPacketSize"); }
	retval = send(s, (char*)enemies.data(), dataSize, 0);
	if (retval == SOCKET_ERROR) { err_display("send - enemyPacket"); }

	// s_itemPacket 전송
	dataSize = items.size() * sizeof(s_itemPacket);
	retval = send(s, (char*)&dataSize, sizeof(int), 0);
	if (retval == SOCKET_ERROR) { err_display("send - itemPacketSize"); }
	retval = send(s, (char*)items.data(), dataSize, 0);
	if (retval == SOCKET_ERROR) { err_display("send - itemPacket"); }

	// s_obstaclePacket 전송
	dataSize = obstacles.size() * sizeof(s_obstaclePacket);
	retval = send(s, (char*)&dataSize, sizeof(int), 0);
	if (retval == SOCKET_ERROR) { err_display("send - obstaclePacketSize"); }
	retval = send(s, (char*)obstacles.data(), dataSize, 0);
	if (retval == SOCKET_ERROR) { err_display("send - obstaclePacket"); }

	// s_bulletPacket 전송
	dataSize = bullets.size() * sizeof(s_bulletPacket);
	retval = send(s, (char*)&dataSize, sizeof(int), 0);
	if (retval == SOCKET_ERROR) { err_display("send - bulletPacketSize"); }
	retval = send(s, (char*)bullets.data(), dataSize, 0);
	if (retval == SOCKET_ERROR) { err_display("send - bulletPacket"); }

	// s_playerPacket 전송
	dataSize = players.size() * sizeof(s_playerPacket);
	retval = send(s, (char*)&dataSize, sizeof(int), 0);
	if (retval == SOCKET_ERROR) { err_display("send - playerPacketSize"); }
	retval = send(s, (char*)players.data(), dataSize, 0);
	if (retval == SOCKET_ERROR) { err_display("send - playerPacket"); }
	cout << "send game data" << endl;
}

void receiveGameData(SOCKET s)
{
	if (s == INVALID_SOCKET) err_display("invalid socket");

	int retval, dataSize{}, vSize;

	// c_playerPacket 받기
	retval = recv(s, (char*)&c_player, sizeof(c_player), 0);
	if (retval == SOCKET_ERROR) err_display("receive - c_playetPacket");

	std::cout << "[LOG] Player Packet Received: Name=" << c_player.c_playerName
		<< ", ID=" << c_player.c_playerID
		<< ", PosX=" << c_player.c_playerPosX
		<< ", PosY=" << c_player.c_playerPosY << std::endl;

	// c_bulletPacket 받기
	retval = recv(s, (char*)&recv_bullet, sizeof(c_bulletPacket), 0);
	if (retval == SOCKET_ERROR) err_display("receive - c_bulletPacket");

	std::cout << "[LOG] Bullet Packet Received: Name=" << recv_bullet.c_playerX
		<< ", ID=" << recv_bullet.c_playerY
		<< ", PosX=" << recv_bullet.c_targetX
		<< ", PosY=" << recv_bullet.c_targetY << std::endl;

}

void sendResult(SOCKET s, int result)
{
	int retval;

	s_UIPacket UIPacket(result);
	retval = send(s, (char*)&UIPacket, sizeof(UIPacket), 0);
	if (retval == SOCKET_ERROR) err_display("send - UIPacket");
}


DWORD WINAPI gameThread(LPVOID arg) {
	std::cout << "GameThread 생성" << std::endl;

	// 매칭 로직
	bool Matching = true;
	while (Matching) {
		EnterCriticalSection(&cs);
		if (waitClientList.size() == 1) {
			std::cout << "GameThread - Matching finish" << std::endl;
			Matching = false;
		}
		LeaveCriticalSection(&cs);
	}

	// 매칭 완료 후 게임 시작 이벤트 설정
	SetEvent(hGameStartEvent);
	std::cout << "gameThread start" << std::endl;

	// GameThread 객체 생성 및 게임 실행
	GameThread game;  // GameThread 객체 생성
	std::cout << "gameThread run in" << std::endl;
	game.run();       // 게임 실행

	return 0;
}

int main(int argc, char* argv[])
{
	InitializeCriticalSection(&cs);
	int retval;

	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
		return 1;
	printf("[알림] 윈속 초기화 성공\n");

	SOCKET listen_sock = socket(AF_INET, SOCK_STREAM, 0);
	if (listen_sock == INVALID_SOCKET)
		err_quit("socket()");

	// bind
	struct sockaddr_in serveraddr;
	memset(&serveraddr, 0, sizeof(serveraddr));
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
	serveraddr.sin_port = htons(SERVERPORT);
	retval = bind(listen_sock, (struct sockaddr*)&serveraddr, sizeof(serveraddr));
	if (retval == SOCKET_ERROR)
		err_quit("bind()");

	// listen
	retval = listen(listen_sock, SOMAXCONN);
	if (retval == SOCKET_ERROR)
		err_quit("listen()");

	// 데이터 통신 변수
	SOCKET client_sock;
	struct sockaddr_in clientaddr;
	int addrlen;
	HANDLE hThread_g, hThread;
	// 게임스레드 생성
	CreateThread(NULL, 0, gameThread, NULL, 0, NULL); //4q

	while (1) {

		// accept()
		addrlen = sizeof(clientaddr);
		client_sock = accept(listen_sock, (struct sockaddr*)&clientaddr, &addrlen);
		if (client_sock == INVALID_SOCKET) {
			err_display("accept()");
			break;
		}

		// 접속한 클라이언트 정보 출력
		char addr[INET_ADDRSTRLEN];
		inet_ntop(AF_INET, &clientaddr.sin_addr, addr, sizeof(addr));
		printf("\n[TCP 서버] 클라이언트 접속: IP 주소=%s, 포트 번호=%d\n",
			addr, ntohs(clientaddr.sin_port));

		// 네트워크스레드 생성
		hThread = CreateThread(NULL, 0, networkThread, (LPVOID)client_sock, 0, NULL);
		if (hThread == NULL) { closesocket(client_sock); }
		else { CloseHandle(hThread); }

	}

	// 소켓 닫기
	closesocket(listen_sock);
	DeleteCriticalSection(&cs);

	// 윈속 종료
	WSACleanup();
	return 0;
}

void waitUntilNextFrame(const std::chrono::time_point<std::chrono::steady_clock>& frameStartTime) {
	auto frameEndTime = std::chrono::steady_clock::now();
	std::chrono::duration<float> elapsed = frameEndTime - frameStartTime;

	if (elapsed.count() < FRAME_TIME) {
		std::this_thread::sleep_for(std::chrono::duration<float>(FRAME_TIME - elapsed.count()));
	}
}