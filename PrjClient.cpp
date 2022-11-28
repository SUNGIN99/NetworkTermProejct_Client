#pragma comment(lib, "ws2_32")
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "resource.h"
#include <Commctrl.h>

#define MULTICAST_SEND_IPv4 "235.7.8.1"
#define MULTICASTIPv4 "235.7.8.2"

#define MULTICAST_SEND_IPv6 "FF12::1:2:3:9"
#define MULTICASTIPv6 "FF12::1:2:3:4"

#define SERVERIPV4  "127.0.0.1"
#define SERVERIPV6  "::1"
#define SERVERPORT  9000
#define REMOTEPORT  9000

#define BUFSIZE     257                    // 전송 메시지 전체 크기
#define MSGSIZE     (BUFSIZE-sizeof(int)-ID_SIZE-CHECK_WHO)  // 채팅 메시지 최대 길이
#define ID_SIZE     20
#define CHECK_WHO   1

#define CHATTING    2000                   // 메시지 타입: 채팅
#define DRAWLINE    2001                   // 메시지 타입: 선 그리기
#define DRAWSTRA    2002				   // 메시지 타입: 직선 그리기
#define DRAWTRIA    2003				   // 메시지 타입: 삼각형 그리기		
#define DRAWRECT    2004				   // 메시지 타입: 사각형 그리기			
#define DRAWCIRC    2005				   // 메시지 타입: 원 그리기
#define DRAWERAS    2006				   // 메시지 타입: 지우개

#define ACCESS	    3000				   // 메시지 타입: 클라이언트 아이디 서버에 최초 전송
#define KICKOUT     3001
#define WM_DRAWIT   (WM_USER+1)            // 사용자 정의 윈도우 메시지

// 공통 메시지 형식
// sizeof(COMM_MSG) == 256
struct COMM_MSG
{
	int  type;
	char dummy[MSGSIZE];
};

// 채팅 메시지 형식
// sizeof(CHAT_MSG) == 257
struct CHAT_MSG
{
	int  type;
	char client_id[ID_SIZE];
	char buf[MSGSIZE];
	char whoSent;
};

// 선 그리기 메시지 형식
// sizeof(DRAWLINE_MSG) == 257
struct DRAWLINE_MSG
{
	int  type;
	int  color;
	int  x0, y0;
	int  x1, y1;
	int  width;
	int  r;
	char dummy[BUFSIZE - 6 * sizeof(int) - 1];
	char whoSent;
};

//<수정> 최초 연결 설정시 보내는 데이터 형식
struct INIT_MSG {
	int type;
	char client_id[ID_SIZE];
	char buf[MSGSIZE];
	char whoSent;
};

// UDP 소켓 파라미터 <수정>
struct SOCKET_SendnRecv {
	SOCKET recv;
	SOCKET send;
};

static SOCKET	     listen_sock_UDPv4;
static SOCKET	     send_sock_UDPv4;
static SOCKET	     listen_sock_UDPv6;
static SOCKET	     send_sock_UDPv6; // UDP소켓 전역변수 <수정>

static HINSTANCE     g_hInst; // 응용 프로그램 인스턴스 핸들
static HWND          g_hDrawWnd; // 그림을 그릴 윈도우
static HWND          g_hButtonSendMsg; // '메시지 전송' 버튼
static HWND          g_hEditStatus; // 받은 메시지 출력 (상대방)
static char          g_ipaddr[64]; // 서버 IP 주소
static u_short       g_port; // 서버 포트 번호
static BOOL          g_isIPv6; // IPv4 or IPv6 주소?
static HANDLE        g_hClientThread; // 스레드 핸들
static volatile BOOL g_bStart; // 통신 시작 여부
static SOCKET        g_sock; // 클라이언트 소켓

static HANDLE        g_hReadEvent, g_hWriteEvent; // 이벤트 핸들

static CHAT_MSG      g_chatmsg; // 채팅 메시지 저장
static DRAWLINE_MSG  g_drawmsg; // 선 그리기 메시지 저장
static int           g_drawcolor; // 선 그리기 색상
static int           g_drawr; // 반지름 저장
static HWND			 hEditUserID;//사용자 ID

// 수정
static INIT_MSG		g_initmsg;

static HWND			g_hEditStatus2; // 보낸 메시지 출력 (내가 보냄)
static BOOL			g_isUDP; // 체크하면 UDP, 아니면 그냥 TCP
static BOOL			g_boardValid; // 현재 그림판 활성화 상태
DWORD WINAPI WriteThread_UDP(LPVOID);
DWORD WINAPI ReadThread_UDP(LPVOID);
DWORD WINAPI ClientMainUDP(LPVOID);
DWORD WINAPI WriteThread_UDPv6(LPVOID);
DWORD WINAPI ReadThread_UDPv6(LPVOID);

SOCKADDR_IN remoteaddr_v4;
SOCKADDR_IN6 remoteaddr_v6;


// 대화상자 프로시저
BOOL CALLBACK DlgProc(HWND, UINT, WPARAM, LPARAM);
// 소켓 통신 스레드 함수
DWORD WINAPI ClientMain(LPVOID arg);
DWORD WINAPI ReadThread(LPVOID arg);
DWORD WINAPI WriteThread(LPVOID arg);
// 자식 윈도우 프로시저
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

// 편집 컨트롤 출력 함수
void DisplayText_Recv(char* fmt, ...);
void DisplayText_Send(char* fmt, ...);

// 사용자 정의 데이터 수신 함수
int recvn(SOCKET s, char* buf, int len, int flags);
// 오류 출력 함수
void err_quit(char* msg);
void err_display(char* msg);

// 메인 함수
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
	LPSTR lpCmdLine, int nCmdShow)
{
	// 윈속 초기화
	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 1;

	// 이벤트 생성
	g_hReadEvent = CreateEvent(NULL, FALSE, TRUE, NULL);
	if (g_hReadEvent == NULL) return 1;
	g_hWriteEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	if (g_hWriteEvent == NULL) return 1;

	// 변수 초기화(일부)
	g_chatmsg.type = CHATTING;
	g_drawmsg.type = DRAWLINE;
	g_drawmsg.color = RGB(0, 0, 0);
	g_drawmsg.width = 5;

	//수정
	g_initmsg.type = ACCESS;
	strncpy(g_initmsg.buf, "CLIENT_ACCESS", MSGSIZE);

	// 대화상자 생성
	g_hInst = hInstance;
	DialogBox(hInstance, MAKEINTRESOURCE(IDD_DIALOG1), NULL, DlgProc);

	// 이벤트 제거
	CloseHandle(g_hReadEvent);
	CloseHandle(g_hWriteEvent);

	// 윈속 종료
	WSACleanup();
	return 0;
}

// 대화상자 프로시저
BOOL CALLBACK DlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	static HWND hButtonIsIPv6;
	static HWND hEditIPaddr;
	static HWND hEditPort;
	static HWND hButtonConnect;
	static HWND hEditMsg;
	static HWND hColorRed;
	static HWND hColorGreen;
	static HWND hColorBlue;
	static HWND hColorBlack;
	static HWND hColorPink;
	static HWND hLineWidth;
	
	static HWND hBoardClear;
	static HWND hNewBoard;
	static HWND hDelBoard;

	// 수정
	static HWND hUDPCheck;

	switch (uMsg) {
	case WM_INITDIALOG:
		// 컨트롤 핸들 얻기
		hButtonIsIPv6 = GetDlgItem(hDlg, IDC_ISIPV6);
		hEditIPaddr = GetDlgItem(hDlg, IDC_IPADDR);
		hEditPort = GetDlgItem(hDlg, IDC_PORT);
		hButtonConnect = GetDlgItem(hDlg, IDC_CONNECT);
		g_hButtonSendMsg = GetDlgItem(hDlg, IDC_SENDMSG);
		hEditMsg = GetDlgItem(hDlg, IDC_MSG);
		g_hEditStatus = GetDlgItem(hDlg, IDC_STATUS);
		hColorRed = GetDlgItem(hDlg, IDC_COLORRED);
		hColorGreen = GetDlgItem(hDlg, IDC_COLORGREEN);
		hColorBlue = GetDlgItem(hDlg, IDC_COLORBLUE);
		hColorBlack = GetDlgItem(hDlg, IDC_COLORBLACK);
		hColorPink = GetDlgItem(hDlg, IDC_COLORPINK);
		hEditUserID = GetDlgItem(hDlg, IDC_USERID);
		hLineWidth = GetDlgItem(hDlg, IDC_THICK);

		// 수정
		g_hEditStatus2 = GetDlgItem(hDlg, IDC_STATUS2);
		hUDPCheck = GetDlgItem(hDlg, IDC_UDPCHECK);
		hBoardClear = (GetDlgItem(hDlg, IDC_BOARDCLEAR));
		hNewBoard = (GetDlgItem(hDlg, IDC_NEWBOARD));
		hDelBoard =(GetDlgItem(hDlg, IDC_DELBOARD));

		// 컨트롤 초기화
		SendMessage(hEditMsg, EM_SETLIMITTEXT, MSGSIZE, 0);
		SendMessage(hEditUserID, EM_SETLIMITTEXT, ID_SIZE-1, 0);
		EnableWindow(g_hButtonSendMsg, FALSE);
		SetDlgItemText(hDlg, IDC_IPADDR, SERVERIPV4);
		SetDlgItemInt(hDlg, IDC_PORT, SERVERPORT, FALSE);
		SendMessage(hColorBlack, BM_SETCHECK, BST_CHECKED, 0);
		SendMessage(hColorRed, BM_SETCHECK, BST_UNCHECKED, 0);
		SendMessage(hColorGreen, BM_SETCHECK, BST_UNCHECKED, 0);
		SendMessage(hColorBlue, BM_SETCHECK, BST_UNCHECKED, 0);
		SendMessage(hColorPink, BM_SETCHECK, BST_UNCHECKED, 0);
		SendMessage(hLineWidth, TBM_SETPOS, TRUE, 5);
		SendMessage(hLineWidth, TBM_SETRANGE, (WPARAM)1, (LPARAM)MAKELONG(1, 10));

		// 윈도우 클래스 등록
		WNDCLASS wndclass;
		wndclass.style = CS_HREDRAW | CS_VREDRAW;
		wndclass.lpfnWndProc = WndProc;
		wndclass.cbClsExtra = 0;
		wndclass.cbWndExtra = 0;
		wndclass.hInstance = g_hInst;
		wndclass.hIcon = LoadIcon(NULL, IDI_APPLICATION);
		wndclass.hCursor = LoadCursor(NULL, IDC_ARROW);
		wndclass.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
		wndclass.lpszMenuName = NULL;
		wndclass.lpszClassName = "MyWndClass";
		if (!RegisterClass(&wndclass)) return 1;
		g_boardValid = FALSE;

		// 자식 윈도우 생성
		if (g_boardValid == FALSE) {
			g_hDrawWnd = CreateWindow("MyWndClass", "그림 그릴 윈도우", WS_CHILD,
				450, 60, 425, 415, hDlg, (HMENU)NULL, g_hInst, NULL);
			if (g_hDrawWnd == NULL) return 1;
			ShowWindow(g_hDrawWnd, SW_SHOW);
			UpdateWindow(g_hDrawWnd);

			g_boardValid = TRUE;
			EnableWindow(hNewBoard, FALSE);
		}
		return TRUE;

		//굵기
	case WM_HSCROLL:
		g_drawmsg.width = SendDlgItemMessage(hDlg, IDC_THICK, TBM_GETPOS, 0, 0);
		return 0;

	case WM_COMMAND:
		switch (LOWORD(wParam)) {

		case IDC_MSG:
			if (HIWORD(wParam) == EN_SETFOCUS) {
				//DisplayText_Send("IDC_MSG");
			}
			return TRUE;

		case IDC_BOARDCLEAR: // 그림판 비우기
			if (g_boardValid == TRUE) {
				DestroyWindow(g_hDrawWnd);
				g_hDrawWnd = CreateWindow("MyWndClass", "그림 그릴 윈도우", WS_CHILD,
					450, 60, 425, 415, hDlg, (HMENU)NULL, g_hInst, NULL);
				if (g_hDrawWnd == NULL) return 1;
				ShowWindow(g_hDrawWnd, SW_SHOW);
				UpdateWindow(g_hDrawWnd);

				g_boardValid = TRUE;
				EnableWindow(hNewBoard, FALSE);
			}

			return TRUE;

		case IDC_NEWBOARD:
			if (g_boardValid == FALSE) {
				g_hDrawWnd = CreateWindow("MyWndClass", "그림 그릴 윈도우", WS_CHILD,
					450, 60, 425, 415, hDlg, (HMENU)NULL, g_hInst, NULL);
				if (g_hDrawWnd == NULL) return 1;
				ShowWindow(g_hDrawWnd, SW_SHOW);
				UpdateWindow(g_hDrawWnd);

				g_boardValid = TRUE;
				EnableWindow(hNewBoard, FALSE);
				EnableWindow(hBoardClear, TRUE);
			}
			return TRUE;

		case IDC_DELBOARD:
			if (g_boardValid == TRUE) {
				DestroyWindow(g_hDrawWnd);

				g_boardValid = FALSE;
				EnableWindow(hNewBoard, TRUE);
				EnableWindow(hBoardClear, FALSE);
			}
			return TRUE;

		case IDC_ISIPV6: // <수정>
			g_isIPv6 = SendMessage(hButtonIsIPv6, BM_GETCHECK, 0, 0);
			g_isUDP = SendMessage(hUDPCheck, BM_GETCHECK, 0, 0);
			if (g_isUDP == false) { // UDP안눌러져잇을때만
				if (g_isIPv6 == false)
					SetDlgItemText(hDlg, IDC_IPADDR, SERVERIPV4);
				else
					SetDlgItemText(hDlg, IDC_IPADDR, SERVERIPV6);
			}
			else {
				if (g_isIPv6 == false)
					SetDlgItemText(hDlg, IDC_IPADDR, "UDP_IPV4");
				else
					SetDlgItemText(hDlg, IDC_IPADDR, "UDP_IPV6");
			}
			return TRUE;

		case IDC_UDPCHECK: // <수정>
			g_isUDP = SendMessage(hUDPCheck, BM_GETCHECK, 0, 0);
			g_isIPv6 = SendMessage(hButtonIsIPv6, BM_GETCHECK, 0, 0);
			if (g_isUDP == true) { // UDP 체크
				if (g_isIPv6 == false)
					SetDlgItemText(hDlg, IDC_IPADDR, "UDP_IPV4");
				else
					SetDlgItemText(hDlg, IDC_IPADDR, "UDP_IPV6");
			}
			else { // UDP 해제
				if (g_isIPv6 == false)
					SetDlgItemText(hDlg, IDC_IPADDR, SERVERIPV4);
				else
					SetDlgItemText(hDlg, IDC_IPADDR, SERVERIPV6);
			}

			return TRUE;

		case IDC_CONNECT:// <수정>
			if (GetDlgItemText(hDlg, IDC_USERID, (LPSTR)g_chatmsg.client_id, ID_SIZE) != NULL) {
				// UDP 일때 연결 X, TCP일 때 연결
				g_isUDP = SendMessage(hUDPCheck, BM_GETCHECK, 0, 0);
				// InitMSG에 사용자 이름 추가
				GetDlgItemText(hDlg, IDC_USERID, (LPSTR)g_initmsg.client_id, ID_SIZE);

				g_isIPv6 = SendMessage(hButtonIsIPv6, BM_GETCHECK, 0, 0);
				GetDlgItemText(hDlg, IDC_USERID, (LPSTR)g_chatmsg.client_id, ID_SIZE);
				g_port = GetDlgItemInt(hDlg, IDC_PORT, NULL, FALSE);

				if (g_isUDP == false) {
					GetDlgItemText(hDlg, IDC_IPADDR, g_ipaddr, sizeof(g_ipaddr));

					// 소켓 통신 스레드 시작
					g_hClientThread = CreateThread(NULL, 0, ClientMain, NULL, 0, NULL);
					if (g_hClientThread == NULL) {
						MessageBox(hDlg, "TCP 클라이언트를 시작할 수 없습니다."
							"\r\n프로그램을 종료합니다.", "실패!", MB_ICONERROR);
						EndDialog(hDlg, 0);
					}
					else {
						EnableWindow(hButtonConnect, FALSE);
						while (g_bStart == FALSE); // 서버 접속 성공 기다림
						EnableWindow(hButtonIsIPv6, FALSE);
						EnableWindow(hEditIPaddr, FALSE);
						EnableWindow(hEditPort, FALSE);
						EnableWindow(hUDPCheck, FALSE);
						EnableWindow(g_hButtonSendMsg, TRUE);
						SetFocus(hEditMsg);
					}
				}
				else { // UDP 연결 <수정>

					// 소켓 UDP통신 스레드 시작
					g_hClientThread = CreateThread(NULL, 0, ClientMainUDP, NULL, 0, NULL);
					if (g_hClientThread == NULL) {
						MessageBox(hDlg, "UDP 클라이언트를 시작할 수 없습니다."
							"\r\n프로그램을 종료합니다.", "실패!", MB_ICONERROR);
						EndDialog(hDlg, 0);
					}
					else {
						EnableWindow(hButtonConnect, FALSE);
						while (g_bStart == FALSE); // 서버 접속 성공 기다림
						EnableWindow(hButtonIsIPv6, FALSE);
						EnableWindow(hEditIPaddr, FALSE);
						EnableWindow(hEditPort, FALSE);
						EnableWindow(g_hButtonSendMsg, TRUE);
						EnableWindow(hUDPCheck, FALSE);
						SetFocus(hEditMsg);
					}

				}

				return TRUE;
			}

			else {
				break;
			}

		case IDC_SENDMSG:
			// 읽기 완료를 기다림
			WaitForSingleObject(g_hReadEvent, INFINITE);
			GetDlgItemText(hDlg, IDC_MSG, g_chatmsg.buf, MSGSIZE);
			// 쓰기 완료를 알림
			SetEvent(g_hWriteEvent);
			// 입력된 텍스트 전체를 선택 표시
			SendMessage(hEditMsg, EM_SETSEL, 0, -1);
			return TRUE;

			// 색깔
		case IDC_COLORRED:
			g_drawmsg.color = RGB(255, 0, 0);
			return TRUE;

		case IDC_COLORGREEN:
			g_drawmsg.color = RGB(0, 255, 0);
			return TRUE;

		case IDC_COLORBLUE:
			g_drawmsg.color = RGB(0, 0, 255);
			return TRUE;

		case IDC_COLORBLACK:
			g_drawmsg.color = RGB(0, 0, 0);
			return TRUE;

		case IDC_COLORPINK:
			g_drawmsg.color = RGB(254, 211, 255);
			return TRUE;

		case IDCANCEL:
			if (MessageBox(hDlg, "정말로 종료하시겠습니까?",
				"질문", MB_YESNO | MB_ICONQUESTION) == IDYES)
			{
				closesocket(g_sock);
				EndDialog(hDlg, IDCANCEL);
			}
			return TRUE;

			//도형
		case IDC_LINE:
			g_drawmsg.type = DRAWLINE;
			return TRUE;

		case IDC_STRA:
			g_drawmsg.type = DRAWSTRA;
			return TRUE;

		case IDC_TRIA:
			g_drawmsg.type = DRAWTRIA;
			return TRUE;

		case IDC_RECT:
			g_drawmsg.type = DRAWRECT;
			return TRUE;

		case IDC_CIRC:
			g_drawmsg.type = DRAWCIRC;
			return TRUE;

		case IDC_ERAS:
			g_drawmsg.type = DRAWERAS;
			return TRUE;
		}

		return FALSE;
	}
	return FALSE;
}

// 소켓 통신 스레드 함수
DWORD WINAPI ClientMain(LPVOID arg)
{
	int retval;

	if (g_isIPv6 == false) {
		// socket()
		g_sock = socket(AF_INET, SOCK_STREAM, 0);
		if (g_sock == INVALID_SOCKET) err_quit("socket()");

		// connect()
		SOCKADDR_IN serveraddr;
		ZeroMemory(&serveraddr, sizeof(serveraddr));
		serveraddr.sin_family = AF_INET;
		serveraddr.sin_addr.s_addr = inet_addr(g_ipaddr);
		serveraddr.sin_port = htons(g_port);
		retval = connect(g_sock, (SOCKADDR*)&serveraddr, sizeof(serveraddr));
		if (retval == SOCKET_ERROR) err_quit("connect()");
	}
	else {
		// socket()
		g_sock = socket(AF_INET6, SOCK_STREAM, 0);
		if (g_sock == INVALID_SOCKET) err_quit("socket()");

		// connect()
		SOCKADDR_IN6 serveraddr;
		ZeroMemory(&serveraddr, sizeof(serveraddr));
		serveraddr.sin6_family = AF_INET6;
		int addrlen = sizeof(serveraddr);
		WSAStringToAddress(g_ipaddr, AF_INET6, NULL,
			(SOCKADDR*)&serveraddr, &addrlen);
		serveraddr.sin6_port = htons(g_port);
		retval = connect(g_sock, (SOCKADDR*)&serveraddr, sizeof(serveraddr));
		if (retval == SOCKET_ERROR) err_quit("connect()");
	}
	retval = send(g_sock, (char*)&g_initmsg, BUFSIZE, 0);
	MessageBox(NULL, "서버에 TCP로 접속했습니다.", "성공!", MB_ICONINFORMATION);

	// 읽기 & 쓰기 스레드 생성
	HANDLE hThread[2];
	hThread[0] = CreateThread(NULL, 0, ReadThread, NULL, 0, NULL);
	hThread[1] = CreateThread(NULL, 0, WriteThread, NULL, 0, NULL);
	if (hThread[0] == NULL || hThread[1] == NULL) {
		MessageBox(NULL, "스레드를 시작할 수 없습니다."
			"\r\n프로그램을 종료합니다.",
			"실패!", MB_ICONERROR);
		exit(1);
	}

	g_bStart = TRUE;

	// 스레드 종료 대기
	retval = WaitForMultipleObjects(2, hThread, FALSE, INFINITE);
	retval -= WAIT_OBJECT_0;
	if (retval == 0)
		TerminateThread(hThread[1], 1);
	else
		TerminateThread(hThread[0], 1);
	CloseHandle(hThread[0]);
	CloseHandle(hThread[1]);

	g_bStart = FALSE;

	MessageBox(NULL, "서버가 접속을 끊었습니다", "알림", MB_ICONINFORMATION);
	EnableWindow(g_hButtonSendMsg, FALSE);

	closesocket(g_sock);
	return 0;
}

// 데이터 받기
DWORD WINAPI ReadThread(LPVOID arg)
{
	int retval;
	COMM_MSG comm_msg;
	CHAT_MSG* chat_msg;
	DRAWLINE_MSG* draw_msg;

	while (1) {
		retval = recvn(g_sock, (char*)&comm_msg, BUFSIZE, 0);

		if (retval == 0 || retval == SOCKET_ERROR || comm_msg.type == KICKOUT ) {
			break;
		}


		if (comm_msg.type == CHATTING) {
			chat_msg = (CHAT_MSG*)&comm_msg;
			if (strcmp(chat_msg->client_id, g_chatmsg.client_id) == 0) {
				DisplayText_Send("[%s] %s\r\n", chat_msg->client_id, chat_msg->buf);
				DisplayText_Recv("%s\r\n", " ");
			}
			else
			{
				DisplayText_Recv("[%s] %s\r\n", chat_msg->client_id, chat_msg->buf);
				DisplayText_Send("%s\r\n", " ");
			}
		}
		else if (comm_msg.type == DRAWERAS) {
			draw_msg = (DRAWLINE_MSG*)&comm_msg;
			g_drawcolor = RGB(255, 255, 255);
			g_drawmsg.type = draw_msg->type;
			g_drawmsg.x0 = draw_msg->x0;
			g_drawmsg.y0 = draw_msg->y0;
			g_drawmsg.x1 = draw_msg->x1;
			g_drawmsg.y1 = draw_msg->y1;

			g_drawmsg.width = draw_msg->width;
			SendMessage(g_hDrawWnd, WM_DRAWIT,
				MAKEWPARAM(draw_msg->x0, draw_msg->y0),
				MAKELPARAM(draw_msg->x1, draw_msg->y1));
		}
		else {
			draw_msg = (DRAWLINE_MSG*)&comm_msg;
			g_drawcolor = draw_msg->color;
			g_drawmsg.type = draw_msg->type;
			g_drawmsg.x0 = draw_msg->x0;
			g_drawmsg.y0 = draw_msg->y0;
			g_drawmsg.x1 = draw_msg->x1;
			g_drawmsg.y1 = draw_msg->y1;

			g_drawmsg.width = draw_msg->width;
			SendMessage(g_hDrawWnd, WM_DRAWIT,
				MAKEWPARAM(draw_msg->x0, draw_msg->y0),
				MAKELPARAM(draw_msg->x1, draw_msg->y1));
		}
	}
	return 0;
}

// 데이터 보내기
DWORD WINAPI WriteThread(LPVOID arg)
{
	int retval;


	// 서버와 데이터 통신
	while (1) {
		// 쓰기 완료 기다리기
		WaitForSingleObject(g_hWriteEvent, INFINITE);

		// 문자열 길이가 0이면 보내지 않음
		if (strlen(g_chatmsg.buf) == 0) {
			// '메시지 전송' 버튼 활성화
			EnableWindow(g_hButtonSendMsg, TRUE);
			// 읽기 완료 알리기
			SetEvent(g_hReadEvent);
			continue;
		}

		// 데이터 보내기
		retval = send(g_sock, (char*)&g_chatmsg, BUFSIZE, 0);
		if (retval == SOCKET_ERROR) {
			break;
		}

		// '메시지 전송' 버튼 활성화
		EnableWindow(g_hButtonSendMsg, TRUE);
		// 읽기 완료 알리기
		SetEvent(g_hReadEvent);
	}

	return 0;
}

// 수정 UDP Client 스레드
DWORD WINAPI ClientMainUDP(LPVOID arg) {
	int retvalUDP;
	HANDLE hThread[2];

	if (g_isIPv6 == false) {
		// socket()
		listen_sock_UDPv4 = socket(AF_INET, SOCK_DGRAM, 0);
		if (listen_sock_UDPv4 == INVALID_SOCKET) err_quit("socket()");

		send_sock_UDPv4 = socket(AF_INET, SOCK_DGRAM, 0);
		if (send_sock_UDPv4 == INVALID_SOCKET) err_quit("socket()");

		hThread[0] = CreateThread(NULL, 0, ReadThread_UDP, NULL, 0, NULL);
		hThread[1] = CreateThread(NULL, 0, WriteThread_UDP, NULL, 0, NULL);
		MessageBox(NULL, "서버에 UDPv4로 접속했습니다.", "성공!", MB_ICONINFORMATION);
	}

	else {
		listen_sock_UDPv6 = socket(AF_INET6, SOCK_DGRAM, 0);
		if (listen_sock_UDPv6 == INVALID_SOCKET) err_quit("socket()");

		send_sock_UDPv6 = socket(AF_INET6, SOCK_DGRAM, 0);
		if (send_sock_UDPv6 == INVALID_SOCKET) err_quit("socket()");

		hThread[0] = CreateThread(NULL, 0, ReadThread_UDPv6, NULL, 0, NULL);
		hThread[1] = CreateThread(NULL, 0, WriteThread_UDPv6, NULL, 0, NULL);
		MessageBox(NULL, "서버에 UDPv6로 접속했습니다.", "성공!", MB_ICONINFORMATION);
	}

	// 읽기 & 쓰기 스레드 생성 UDP
	// UDP IP 버전 따져서 스레드 실행할 것
	if (hThread[0] == NULL || hThread[1] == NULL) {
		MessageBox(NULL, "스레드를 시작할 수 없습니다."
			"\r\n프로그램을 종료합니다.",
			"실패!", MB_ICONERROR);
		exit(1);
	}

	g_bStart = TRUE;

	// 스레드 종료 대기
	retvalUDP = WaitForMultipleObjects(2, hThread, FALSE, INFINITE);
	retvalUDP -= WAIT_OBJECT_0;
	if (retvalUDP == 0)
		TerminateThread(hThread[1], 1);
	else
		TerminateThread(hThread[0], 1);
	CloseHandle(hThread[0]);
	CloseHandle(hThread[1]);

	g_bStart = FALSE;

	MessageBox(NULL, "서버가 접속을 끊었습니다", "알림", MB_ICONINFORMATION);
	EnableWindow(g_hButtonSendMsg, FALSE);

	closesocket(g_sock);
	return 0;
}

DWORD WINAPI ReadThread_UDP(LPVOID arg) {
	bool optval = TRUE;
	int retvalUDP = setsockopt(listen_sock_UDPv4, SOL_SOCKET,
		SO_REUSEADDR, (char*)&optval, sizeof(optval));
	if (retvalUDP == SOCKET_ERROR) err_quit("setsockopt()");

	// bind()
	SOCKADDR_IN serveraddrUDPv4;
	ZeroMemory(&serveraddrUDPv4, sizeof(serveraddrUDPv4));
	serveraddrUDPv4.sin_family = AF_INET;
	serveraddrUDPv4.sin_addr.s_addr = htonl(INADDR_ANY);
	serveraddrUDPv4.sin_port = htons(SERVERPORT);
	retvalUDP = bind(listen_sock_UDPv4, (SOCKADDR*)&serveraddrUDPv4, sizeof(serveraddrUDPv4));
	if (retvalUDP == SOCKET_ERROR) err_quit("bind()");

	// 멀티캐스트 그룹 가입
	struct ip_mreq mreq_v4;
	mreq_v4.imr_multiaddr.s_addr = inet_addr(MULTICASTIPv4);
	mreq_v4.imr_interface.s_addr = htonl(INADDR_ANY);
	retvalUDP = setsockopt(listen_sock_UDPv4, IPPROTO_IP, IP_ADD_MEMBERSHIP,
		(char*)&mreq_v4, sizeof(mreq_v4));
	if (retvalUDP == SOCKET_ERROR) {
		err_quit("setsockopt()");
	}

	SOCKADDR_IN peeraddr;
	int addrlen;
	COMM_MSG comm_msg;
	CHAT_MSG* chat_msg;
	DRAWLINE_MSG* draw_msg;

	while (1) {
		addrlen = sizeof(peeraddr);
		retvalUDP = recvfrom(listen_sock_UDPv4, (char*)&comm_msg, BUFSIZE, 0, (SOCKADDR*)&peeraddr, &addrlen);
		if (retvalUDP == 0 || retvalUDP == SOCKET_ERROR || comm_msg.type == KICKOUT) {
			break;
		}

		if (comm_msg.type == CHATTING) {
			chat_msg = (CHAT_MSG*)&comm_msg;
			if (strcmp(chat_msg->client_id, g_chatmsg.client_id) == 0) {
				DisplayText_Send("[%s] %s\r\n", chat_msg->client_id, chat_msg->buf);
				DisplayText_Recv("%s\r\n", " ");
			}
			else
			{
				DisplayText_Recv("[%s] %s\r\n", chat_msg->client_id, chat_msg->buf);
				DisplayText_Send("%s\r\n", " ");
			}
		}
		else if (comm_msg.type == DRAWERAS) {
			draw_msg = (DRAWLINE_MSG*)&comm_msg;
			g_drawcolor = RGB(255, 255, 255);
			g_drawmsg.type = draw_msg->type;
			g_drawmsg.x0 = draw_msg->x0;
			g_drawmsg.y0 = draw_msg->y0;
			g_drawmsg.x1 = draw_msg->x1;
			g_drawmsg.y1 = draw_msg->y1;

			g_drawmsg.width = draw_msg->width;
			SendMessage(g_hDrawWnd, WM_DRAWIT,
				MAKEWPARAM(draw_msg->x0, draw_msg->y0),
				MAKELPARAM(draw_msg->x1, draw_msg->y1));
		}
		else {
			draw_msg = (DRAWLINE_MSG*)&comm_msg;
			g_drawcolor = draw_msg->color;
			g_drawmsg.type = draw_msg->type;
			g_drawmsg.x0 = draw_msg->x0;
			g_drawmsg.y0 = draw_msg->y0;
			g_drawmsg.x1 = draw_msg->x1;
			g_drawmsg.y1 = draw_msg->y1;

			g_drawmsg.width = draw_msg->width;
			SendMessage(g_hDrawWnd, WM_DRAWIT,
				MAKEWPARAM(draw_msg->x0, draw_msg->y0),
				MAKELPARAM(draw_msg->x1, draw_msg->y1));
		}
	}

	// 멀티캐스트 그룹 탈퇴
	retvalUDP = setsockopt(listen_sock_UDPv4, IPPROTO_IP, IP_DROP_MEMBERSHIP,
		(char*)&mreq_v4, sizeof(mreq_v4));
	if (retvalUDP == SOCKET_ERROR) err_quit("setsockopt()");

	return 0;
}

DWORD WINAPI WriteThread_UDP(LPVOID arg) {
	int retvalUDP;

	//int ttl_v4 = 2; // 문제
	//retvalUDP = setsockopt(send_sock_UDPv4, IPPROTO_IP, IP_MULTICAST_TTL,
	//	(char*)ttl_v4, sizeof(ttl_v4));
	//if (retvalUDP == SOCKET_ERROR) {
	//	err_quit("setsockopt()");
	//}

	ZeroMemory(&remoteaddr_v4, sizeof(remoteaddr_v4));
	remoteaddr_v4.sin_family = AF_INET;
	remoteaddr_v4.sin_addr.s_addr = inet_addr(MULTICAST_SEND_IPv4);
	remoteaddr_v4.sin_port = htons(REMOTEPORT);

	retvalUDP = sendto(send_sock_UDPv4, (char*)&g_initmsg, BUFSIZE, 0,
		(SOCKADDR*)&remoteaddr_v4, sizeof(remoteaddr_v4));

	while (1) {

		WaitForSingleObject(g_hWriteEvent, INFINITE);

		if (strlen(g_chatmsg.buf) == 0) {
			EnableWindow(g_hButtonSendMsg, TRUE);
			// 읽기 완료 알리기
			SetEvent(g_hReadEvent);
			continue;
		}
		// 데이터 보내기
		retvalUDP = sendto(send_sock_UDPv4, (char*)&g_chatmsg, BUFSIZE, 0,
			(SOCKADDR*)&remoteaddr_v4, sizeof(remoteaddr_v4));
		if (retvalUDP == SOCKET_ERROR) {
			DisplayText_Send("writethread_UDP_Break\r\n");
			break;
		}

		// '메시지 전송' 버튼 활성화
		EnableWindow(g_hButtonSendMsg, TRUE);

		// 읽기 완료 알리기
		SetEvent(g_hReadEvent);
	}

	return 0;
}

DWORD WINAPI ReadThread_UDPv6(LPVOID arg) {
	bool optval = TRUE;
	int retvalUDP = setsockopt(listen_sock_UDPv6, SOL_SOCKET,
		SO_REUSEADDR, (char*)&optval, sizeof(optval));
	if (retvalUDP == SOCKET_ERROR) err_quit("setsockopt()");

	// bind()
	SOCKADDR_IN6 serveraddrUDPv6;
	ZeroMemory(&serveraddrUDPv6, sizeof(serveraddrUDPv6));
	serveraddrUDPv6.sin6_family = AF_INET6;
	serveraddrUDPv6.sin6_addr = in6addr_any;
	serveraddrUDPv6.sin6_port = htons(SERVERPORT);
	retvalUDP = bind(listen_sock_UDPv6, (SOCKADDR*)&serveraddrUDPv6, sizeof(serveraddrUDPv6));
	if (retvalUDP == SOCKET_ERROR) err_quit("bind()");


	// 주소 변환(문자열 -> IPv6)
	SOCKADDR_IN6 tmpaddr;
	int addrlenTmp = sizeof(tmpaddr);
	WSAStringToAddress(MULTICASTIPv6, AF_INET6, NULL,
		(SOCKADDR*)&tmpaddr, &addrlenTmp);

	// 멀티캐스트 그룹 가입
	struct ipv6_mreq mreq_v6;
	mreq_v6.ipv6mr_multiaddr = tmpaddr.sin6_addr;
	mreq_v6.ipv6mr_interface = 0;
	retvalUDP = setsockopt(listen_sock_UDPv6, IPPROTO_IPV6, IPV6_ADD_MEMBERSHIP,
		(char*)&mreq_v6, sizeof(mreq_v6));
	if (retvalUDP == SOCKET_ERROR) {
		err_quit("setsockopt()");
	}

	SOCKADDR_IN6 peeraddr;
	int addrlen;
	COMM_MSG comm_msg;
	CHAT_MSG* chat_msg;
	DRAWLINE_MSG* draw_msg;

	while (1) {
		addrlen = sizeof(peeraddr);
		retvalUDP = recvfrom(listen_sock_UDPv6, (char*)&comm_msg, BUFSIZE, 0, (SOCKADDR*)&peeraddr, &addrlen);
		if (retvalUDP == 0 || retvalUDP == SOCKET_ERROR || comm_msg.type == KICKOUT) {
			break;
		}

		if (comm_msg.type == CHATTING) {
			chat_msg = (CHAT_MSG*)&comm_msg;
			if (strcmp(chat_msg->client_id, g_chatmsg.client_id) == 0) {
				DisplayText_Send("[%s] %s\r\n", chat_msg->client_id, chat_msg->buf);
				DisplayText_Recv("%s\r\n", " ");
			}
			else
			{
				DisplayText_Recv("[%s] %s\r\n", chat_msg->client_id, chat_msg->buf);
				DisplayText_Send("%s\r\n", " ");
			}
		}
		else if (comm_msg.type == DRAWERAS) {
			draw_msg = (DRAWLINE_MSG*)&comm_msg;
			g_drawcolor = RGB(255, 255, 255);
			g_drawmsg.type = draw_msg->type;
			g_drawmsg.x0 = draw_msg->x0;
			g_drawmsg.y0 = draw_msg->y0;
			g_drawmsg.x1 = draw_msg->x1;
			g_drawmsg.y1 = draw_msg->y1;

			g_drawmsg.width = draw_msg->width;
			SendMessage(g_hDrawWnd, WM_DRAWIT,
				MAKEWPARAM(draw_msg->x0, draw_msg->y0),
				MAKELPARAM(draw_msg->x1, draw_msg->y1));
		}
		else {
			draw_msg = (DRAWLINE_MSG*)&comm_msg;
			g_drawcolor = draw_msg->color;
			g_drawmsg.type = draw_msg->type;
			g_drawmsg.x0 = draw_msg->x0;
			g_drawmsg.y0 = draw_msg->y0;
			g_drawmsg.x1 = draw_msg->x1;
			g_drawmsg.y1 = draw_msg->y1;

			g_drawmsg.width = draw_msg->width;
			SendMessage(g_hDrawWnd, WM_DRAWIT,
				MAKEWPARAM(draw_msg->x0, draw_msg->y0),
				MAKELPARAM(draw_msg->x1, draw_msg->y1));
		}
	}

	// 멀티캐스트 그룹 탈퇴
	retvalUDP = setsockopt(listen_sock_UDPv6, IPPROTO_IPV6, IPV6_DROP_MEMBERSHIP,
		(char*)&mreq_v6, sizeof(mreq_v6));
	if (retvalUDP == SOCKET_ERROR) err_quit("setsockopt()");

	return 0;
}

DWORD WINAPI WriteThread_UDPv6(LPVOID arg) {
	int retvalUDP;

	//int ttl_v4 = 2; // 문제
	//retvalUDP = setsockopt(send_sock_UDPv4, IPPROTO_IP, IP_MULTICAST_TTL,
	//	(char*)ttl_v4, sizeof(ttl_v4));
	//if (retvalUDP == SOCKET_ERROR) {
	//	err_quit("setsockopt()");
	//}

	
	ZeroMemory(&remoteaddr_v6, sizeof(remoteaddr_v6));
	remoteaddr_v6.sin6_family = AF_INET6;
	int addrlen = sizeof(remoteaddr_v6);
	WSAStringToAddress(MULTICAST_SEND_IPv6, AF_INET6, NULL,
		(SOCKADDR*)&remoteaddr_v6, &addrlen);
	remoteaddr_v6.sin6_port = htons(REMOTEPORT);

	retvalUDP = sendto(send_sock_UDPv6, (char*)&g_initmsg, BUFSIZE, 0,
		(SOCKADDR*)&remoteaddr_v6, sizeof(remoteaddr_v6));

	while (1) {

		WaitForSingleObject(g_hWriteEvent, INFINITE);

		if (strlen(g_chatmsg.buf) == 0) {
			EnableWindow(g_hButtonSendMsg, TRUE);
			// 읽기 완료 알리기
			SetEvent(g_hReadEvent);
			continue;
		}
		// 데이터 보내기
		retvalUDP = sendto(send_sock_UDPv6, (char*)&g_chatmsg, BUFSIZE, 0,
			(SOCKADDR*)&remoteaddr_v6, sizeof(remoteaddr_v6));
		if (retvalUDP == SOCKET_ERROR) {
			DisplayText_Send("writethread_UDP_Break\r\n");
			break;
		}

		// '메시지 전송' 버튼 활성화
		EnableWindow(g_hButtonSendMsg, TRUE);

		// 읽기 완료 알리기
		SetEvent(g_hReadEvent);
	}

	return 0;
}


// 자식 윈도우 프로시저
LRESULT CALLBACK WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	HDC hDC;
	int cx, cy;
	PAINTSTRUCT ps;
	RECT rect;
	HPEN hPen, hOldPen;
	static HBITMAP hBitmap;
	static HDC hDCMem;
	static int x0, y0;
	static int x1, y1;
	static BOOL bDrawing = FALSE;

	switch (uMsg) {
	case WM_CREATE:
		hDC = GetDC(hWnd);
		// 화면을 저장할 비트맵 생성
		cx = GetDeviceCaps(hDC, HORZRES);
		cy = GetDeviceCaps(hDC, VERTRES);
		hBitmap = CreateCompatibleBitmap(hDC, cx, cy);

		// 메모리 DC 생성
		hDCMem = CreateCompatibleDC(hDC);

		// 비트맵 선택 후 메모리 DC 화면을 흰색으로 칠함
		SelectObject(hDCMem, hBitmap);
		SelectObject(hDCMem, GetStockObject(WHITE_BRUSH));
		SelectObject(hDCMem, GetStockObject(WHITE_PEN));
		Rectangle(hDCMem, 0, 0, cx, cy);

		ReleaseDC(hWnd, hDC);
		return 0;

	case WM_LBUTTONDOWN:
		x0 = LOWORD(lParam);
		y0 = HIWORD(lParam);
		bDrawing = TRUE;
		return 0;
	case WM_MOUSEMOVE:
		if (bDrawing && g_bStart && (g_drawmsg.type == DRAWLINE || g_drawmsg.type == DRAWERAS)) {
			x1 = LOWORD(lParam);
			y1 = HIWORD(lParam);

			// 선 그리기 메시지 보내기
			g_drawmsg.x0 = x0;
			g_drawmsg.y0 = y0;
			g_drawmsg.x1 = x1;
			g_drawmsg.y1 = y1;

			if(g_isUDP == FALSE)
				send(g_sock, (char*)&g_drawmsg, BUFSIZE, 0);
			else {
				if (g_isIPv6 == TRUE)
					sendto(send_sock_UDPv6, (char*)&g_drawmsg, BUFSIZE, 0,
						(SOCKADDR*)&remoteaddr_v6, sizeof(remoteaddr_v6));
				else
					sendto(send_sock_UDPv4, (char*)&g_drawmsg, BUFSIZE, 0,
						(SOCKADDR*)&remoteaddr_v4, sizeof(remoteaddr_v4)); 
			}
			x0 = x1;
			y0 = y1;
		}
		return 0;
	case WM_LBUTTONUP:
		if (bDrawing && g_bStart && g_drawmsg.type != DRAWLINE && g_drawmsg.type != DRAWERAS) {
			x1 = LOWORD(lParam);
			y1 = HIWORD(lParam);

			g_drawmsg.x0 = x0;
			g_drawmsg.y0 = y0;
			g_drawmsg.x1 = x1;
			g_drawmsg.y1 = y1;

			if (g_isUDP == FALSE)
				send(g_sock, (char*)&g_drawmsg, BUFSIZE, 0);
			else {
				if (g_isIPv6 == TRUE)
					sendto(send_sock_UDPv6, (char*)&g_drawmsg, BUFSIZE, 0,
						(SOCKADDR*)&remoteaddr_v6, sizeof(remoteaddr_v6));
				else
					sendto(send_sock_UDPv4, (char*)&g_drawmsg, BUFSIZE, 0,
						(SOCKADDR*)&remoteaddr_v4, sizeof(remoteaddr_v4));
			}
		}
		bDrawing = FALSE;
		return 0;
	case WM_DRAWIT:
		hDC = GetDC(hWnd);
		hPen = CreatePen(PS_SOLID, g_drawmsg.width, g_drawcolor);

		//직선,선
		if (g_drawmsg.type == DRAWLINE || g_drawmsg.type == DRAWSTRA) {
			// 화면에 그리기
			hOldPen = (HPEN)SelectObject(hDC, hPen);
			MoveToEx(hDC, LOWORD(wParam), HIWORD(wParam), NULL);
			LineTo(hDC, LOWORD(lParam), HIWORD(lParam));
			SelectObject(hDC, hOldPen);

			// 메모리 비트맵에 그리기
			hOldPen = (HPEN)SelectObject(hDCMem, hPen);
			MoveToEx(hDCMem, LOWORD(wParam), HIWORD(wParam), NULL);
			LineTo(hDCMem, LOWORD(lParam), HIWORD(lParam));
			SelectObject(hDC, hOldPen);

			DeleteObject(hPen);
			ReleaseDC(hWnd, hDC);
		}

		//삼각형
		else if (g_drawmsg.type == DRAWTRIA) {
			hOldPen = (HPEN)SelectObject(hDC, hPen);
			SelectObject(hDC, GetStockObject(NULL_BRUSH));
			MoveToEx(hDC, g_drawmsg.x0, g_drawmsg.y0, NULL);
			LineTo(hDC, (g_drawmsg.x1 - g_drawmsg.x0) / 2 + g_drawmsg.x0, g_drawmsg.y1);
			LineTo(hDC, g_drawmsg.x1, g_drawmsg.y0);
			LineTo(hDC, g_drawmsg.x0, g_drawmsg.y0);
			SelectObject(hDC, hOldPen);
			DeleteObject(hPen);
			ReleaseDC(hWnd, hDC);
		}

		//사각형
		else if (g_drawmsg.type == DRAWRECT) {
			hOldPen = (HPEN)SelectObject(hDC, hPen);
			SelectObject(hDC, GetStockObject(NULL_BRUSH));
			MoveToEx(hDC, g_drawmsg.x0, g_drawmsg.y0, NULL);
			LineTo(hDC, g_drawmsg.x0, g_drawmsg.y1);
			LineTo(hDC, g_drawmsg.x1, g_drawmsg.y1);
			LineTo(hDC, g_drawmsg.x1, g_drawmsg.y0);
			LineTo(hDC, g_drawmsg.x0, g_drawmsg.y0);
			SelectObject(hDC, hOldPen);
			DeleteObject(hPen);
			ReleaseDC(hWnd, hDC);
		}

		//원
		else if (g_drawmsg.type == DRAWCIRC) {
			hOldPen = (HPEN)SelectObject(hDC, hPen);
			SelectObject(hDC, GetStockObject(NULL_BRUSH));
			Ellipse(hDC, g_drawmsg.x0, g_drawmsg.y0, LOWORD(lParam), HIWORD(lParam));
			SelectObject(hDC, hOldPen);
			DeleteObject(hPen);
			ReleaseDC(hWnd, hDC);
		}
		else if (g_drawmsg.type == DRAWERAS) {
			hOldPen = (HPEN)SelectObject(hDC, hPen);
			MoveToEx(hDC, LOWORD(wParam), HIWORD(wParam), NULL);
			LineTo(hDC, LOWORD(lParam), HIWORD(lParam));
			SelectObject(hDC, hOldPen);
			hOldPen = (HPEN)SelectObject(hDCMem, hPen);
			MoveToEx(hDCMem, LOWORD(wParam), HIWORD(wParam), NULL);
			LineTo(hDCMem, LOWORD(lParam), HIWORD(lParam));
			SelectObject(hDC, hOldPen);
			DeleteObject(hPen);
			ReleaseDC(hWnd, hDC);
		}
		return 0;
	case WM_PAINT:
		hDC = BeginPaint(hWnd, &ps);

		// 메모리 비트맵에 저장된 그림을 화면에 전송
		GetClientRect(hWnd, &rect);
		BitBlt(hDC, 0, 0, rect.right - rect.left,
			rect.bottom - rect.top, hDCMem, 0, 0, SRCCOPY);

		EndPaint(hWnd, &ps);
		return 0;
	case WM_DESTROY:
		DeleteObject(hBitmap);
		DeleteDC(hDCMem);
		//PostQuitMessage(0);
		return 0;
	}

	return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

// 에디트 컨트롤에 문자열 출력
void DisplayText_Recv(char* fmt, ...)
{
	va_list arg;
	va_start(arg, fmt);

	char cbuf[1024];
	vsprintf(cbuf, fmt, arg);

	int nLength = GetWindowTextLength(g_hEditStatus);
	SendMessage(g_hEditStatus, EM_SETSEL, nLength, nLength);
	SendMessage(g_hEditStatus, EM_REPLACESEL, FALSE, (LPARAM)cbuf);

	va_end(arg);
}

// 수정
void DisplayText_Send(char* fmt, ...)
{
	va_list arg;
	va_start(arg, fmt);

	char cbuf[1024];
	vsprintf(cbuf, fmt, arg);

	int nLength = GetWindowTextLength(g_hEditStatus2);
	SendMessage(g_hEditStatus2, EM_SETSEL, nLength, nLength);
	SendMessage(g_hEditStatus2, EM_REPLACESEL, FALSE, (LPARAM)cbuf);

	va_end(arg);
}

// 사용자 정의 데이터 수신 함수
int recvn(SOCKET s, char* buf, int len, int flags)
{
	int received;
	char* ptr = buf;
	int left = len;

	while (left > 0) {
		received = recv(s, ptr, left, flags);
		if (received == SOCKET_ERROR)
			return SOCKET_ERROR;
		else if (received == 0)
			break;
		// left = 256
		left -= received;
		ptr += received;
	}

	return (len - left);
}

// 소켓 함수 오류 출력 후 종료
void err_quit(char* msg)
{
	LPVOID lpMsgBuf;
	FormatMessage(
		FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
		NULL, WSAGetLastError(),
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPTSTR)&lpMsgBuf, 0, NULL);
	MessageBox(NULL, (LPCTSTR)lpMsgBuf, msg, MB_ICONERROR);
	LocalFree(lpMsgBuf);
	exit(1);
}

// 소켓 함수 오류 출력
void err_display(char* msg)
{
	LPVOID lpMsgBuf;
	FormatMessage(
		FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
		NULL, WSAGetLastError(),
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPTSTR)&lpMsgBuf, 0, NULL);
	printf("[%s] %s", msg, (char*)lpMsgBuf);
	LocalFree(lpMsgBuf);
}