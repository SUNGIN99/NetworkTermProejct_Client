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

#define BUFSIZE     257                    // ���� �޽��� ��ü ũ��
#define MSGSIZE     (BUFSIZE-sizeof(int)-ID_SIZE-CHECK_WHO)  // ä�� �޽��� �ִ� ����
#define ID_SIZE     20
#define CHECK_WHO   1

#define CHATTING    2000                   // �޽��� Ÿ��: ä��
#define DRAWLINE    2001                   // �޽��� Ÿ��: �� �׸���
#define DRAWSTRA    2002				   // �޽��� Ÿ��: ���� �׸���
#define DRAWTRIA    2003				   // �޽��� Ÿ��: �ﰢ�� �׸���		
#define DRAWRECT    2004				   // �޽��� Ÿ��: �簢�� �׸���			
#define DRAWCIRC    2005				   // �޽��� Ÿ��: �� �׸���
#define DRAWERAS    2006				   // �޽��� Ÿ��: ���찳

#define ACCESS	    3000				   // �޽��� Ÿ��: Ŭ���̾�Ʈ ���̵� ������ ���� ����
#define KICKOUT     3001
#define WM_DRAWIT   (WM_USER+1)            // ����� ���� ������ �޽���

// ���� �޽��� ����
// sizeof(COMM_MSG) == 256
struct COMM_MSG
{
	int  type;
	char dummy[MSGSIZE];
};

// ä�� �޽��� ����
// sizeof(CHAT_MSG) == 257
struct CHAT_MSG
{
	int  type;
	char client_id[ID_SIZE];
	char buf[MSGSIZE];
	char whoSent;
};

// �� �׸��� �޽��� ����
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

//<����> ���� ���� ������ ������ ������ ����
struct INIT_MSG {
	int type;
	char client_id[ID_SIZE];
	char buf[MSGSIZE];
	char whoSent;
};

// UDP ���� �Ķ���� <����>
struct SOCKET_SendnRecv {
	SOCKET recv;
	SOCKET send;
};

static SOCKET	     listen_sock_UDPv4;
static SOCKET	     send_sock_UDPv4;
static SOCKET	     listen_sock_UDPv6;
static SOCKET	     send_sock_UDPv6; // UDP���� �������� <����>

static HINSTANCE     g_hInst; // ���� ���α׷� �ν��Ͻ� �ڵ�
static HWND          g_hDrawWnd; // �׸��� �׸� ������
static HWND          g_hButtonSendMsg; // '�޽��� ����' ��ư
static HWND          g_hEditStatus; // ���� �޽��� ��� (����)
static char          g_ipaddr[64]; // ���� IP �ּ�
static u_short       g_port; // ���� ��Ʈ ��ȣ
static BOOL          g_isIPv6; // IPv4 or IPv6 �ּ�?
static HANDLE        g_hClientThread; // ������ �ڵ�
static volatile BOOL g_bStart; // ��� ���� ����
static SOCKET        g_sock; // Ŭ���̾�Ʈ ����

static HANDLE        g_hReadEvent, g_hWriteEvent; // �̺�Ʈ �ڵ�

static CHAT_MSG      g_chatmsg; // ä�� �޽��� ����
static DRAWLINE_MSG  g_drawmsg; // �� �׸��� �޽��� ����
static int           g_drawcolor; // �� �׸��� ����
static int           g_drawr; // ������ ����
static HWND			 hEditUserID;//����� ID

// ����
static INIT_MSG		g_initmsg;

static HWND			g_hEditStatus2; // ���� �޽��� ��� (���� ����)
static BOOL			g_isUDP; // üũ�ϸ� UDP, �ƴϸ� �׳� TCP
static BOOL			g_boardValid; // ���� �׸��� Ȱ��ȭ ����
DWORD WINAPI WriteThread_UDP(LPVOID);
DWORD WINAPI ReadThread_UDP(LPVOID);
DWORD WINAPI ClientMainUDP(LPVOID);
DWORD WINAPI WriteThread_UDPv6(LPVOID);
DWORD WINAPI ReadThread_UDPv6(LPVOID);

SOCKADDR_IN remoteaddr_v4;
SOCKADDR_IN6 remoteaddr_v6;


// ��ȭ���� ���ν���
BOOL CALLBACK DlgProc(HWND, UINT, WPARAM, LPARAM);
// ���� ��� ������ �Լ�
DWORD WINAPI ClientMain(LPVOID arg);
DWORD WINAPI ReadThread(LPVOID arg);
DWORD WINAPI WriteThread(LPVOID arg);
// �ڽ� ������ ���ν���
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

// ���� ��Ʈ�� ��� �Լ�
void DisplayText_Recv(char* fmt, ...);
void DisplayText_Send(char* fmt, ...);

// ����� ���� ������ ���� �Լ�
int recvn(SOCKET s, char* buf, int len, int flags);
// ���� ��� �Լ�
void err_quit(char* msg);
void err_display(char* msg);

// ���� �Լ�
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
	LPSTR lpCmdLine, int nCmdShow)
{
	// ���� �ʱ�ȭ
	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 1;

	// �̺�Ʈ ����
	g_hReadEvent = CreateEvent(NULL, FALSE, TRUE, NULL);
	if (g_hReadEvent == NULL) return 1;
	g_hWriteEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	if (g_hWriteEvent == NULL) return 1;

	// ���� �ʱ�ȭ(�Ϻ�)
	g_chatmsg.type = CHATTING;
	g_drawmsg.type = DRAWLINE;
	g_drawmsg.color = RGB(0, 0, 0);
	g_drawmsg.width = 5;

	//����
	g_initmsg.type = ACCESS;
	strncpy(g_initmsg.buf, "CLIENT_ACCESS", MSGSIZE);

	// ��ȭ���� ����
	g_hInst = hInstance;
	DialogBox(hInstance, MAKEINTRESOURCE(IDD_DIALOG1), NULL, DlgProc);

	// �̺�Ʈ ����
	CloseHandle(g_hReadEvent);
	CloseHandle(g_hWriteEvent);

	// ���� ����
	WSACleanup();
	return 0;
}

// ��ȭ���� ���ν���
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

	// ����
	static HWND hUDPCheck;

	switch (uMsg) {
	case WM_INITDIALOG:
		// ��Ʈ�� �ڵ� ���
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

		// ����
		g_hEditStatus2 = GetDlgItem(hDlg, IDC_STATUS2);
		hUDPCheck = GetDlgItem(hDlg, IDC_UDPCHECK);
		hBoardClear = (GetDlgItem(hDlg, IDC_BOARDCLEAR));
		hNewBoard = (GetDlgItem(hDlg, IDC_NEWBOARD));
		hDelBoard =(GetDlgItem(hDlg, IDC_DELBOARD));

		// ��Ʈ�� �ʱ�ȭ
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

		// ������ Ŭ���� ���
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

		// �ڽ� ������ ����
		if (g_boardValid == FALSE) {
			g_hDrawWnd = CreateWindow("MyWndClass", "�׸� �׸� ������", WS_CHILD,
				450, 60, 425, 415, hDlg, (HMENU)NULL, g_hInst, NULL);
			if (g_hDrawWnd == NULL) return 1;
			ShowWindow(g_hDrawWnd, SW_SHOW);
			UpdateWindow(g_hDrawWnd);

			g_boardValid = TRUE;
			EnableWindow(hNewBoard, FALSE);
		}
		return TRUE;

		//����
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

		case IDC_BOARDCLEAR: // �׸��� ����
			if (g_boardValid == TRUE) {
				DestroyWindow(g_hDrawWnd);
				g_hDrawWnd = CreateWindow("MyWndClass", "�׸� �׸� ������", WS_CHILD,
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
				g_hDrawWnd = CreateWindow("MyWndClass", "�׸� �׸� ������", WS_CHILD,
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

		case IDC_ISIPV6: // <����>
			g_isIPv6 = SendMessage(hButtonIsIPv6, BM_GETCHECK, 0, 0);
			g_isUDP = SendMessage(hUDPCheck, BM_GETCHECK, 0, 0);
			if (g_isUDP == false) { // UDP�ȴ�������������
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

		case IDC_UDPCHECK: // <����>
			g_isUDP = SendMessage(hUDPCheck, BM_GETCHECK, 0, 0);
			g_isIPv6 = SendMessage(hButtonIsIPv6, BM_GETCHECK, 0, 0);
			if (g_isUDP == true) { // UDP üũ
				if (g_isIPv6 == false)
					SetDlgItemText(hDlg, IDC_IPADDR, "UDP_IPV4");
				else
					SetDlgItemText(hDlg, IDC_IPADDR, "UDP_IPV6");
			}
			else { // UDP ����
				if (g_isIPv6 == false)
					SetDlgItemText(hDlg, IDC_IPADDR, SERVERIPV4);
				else
					SetDlgItemText(hDlg, IDC_IPADDR, SERVERIPV6);
			}

			return TRUE;

		case IDC_CONNECT:// <����>
			if (GetDlgItemText(hDlg, IDC_USERID, (LPSTR)g_chatmsg.client_id, ID_SIZE) != NULL) {
				// UDP �϶� ���� X, TCP�� �� ����
				g_isUDP = SendMessage(hUDPCheck, BM_GETCHECK, 0, 0);
				// InitMSG�� ����� �̸� �߰�
				GetDlgItemText(hDlg, IDC_USERID, (LPSTR)g_initmsg.client_id, ID_SIZE);

				g_isIPv6 = SendMessage(hButtonIsIPv6, BM_GETCHECK, 0, 0);
				GetDlgItemText(hDlg, IDC_USERID, (LPSTR)g_chatmsg.client_id, ID_SIZE);
				g_port = GetDlgItemInt(hDlg, IDC_PORT, NULL, FALSE);

				if (g_isUDP == false) {
					GetDlgItemText(hDlg, IDC_IPADDR, g_ipaddr, sizeof(g_ipaddr));

					// ���� ��� ������ ����
					g_hClientThread = CreateThread(NULL, 0, ClientMain, NULL, 0, NULL);
					if (g_hClientThread == NULL) {
						MessageBox(hDlg, "TCP Ŭ���̾�Ʈ�� ������ �� �����ϴ�."
							"\r\n���α׷��� �����մϴ�.", "����!", MB_ICONERROR);
						EndDialog(hDlg, 0);
					}
					else {
						EnableWindow(hButtonConnect, FALSE);
						while (g_bStart == FALSE); // ���� ���� ���� ��ٸ�
						EnableWindow(hButtonIsIPv6, FALSE);
						EnableWindow(hEditIPaddr, FALSE);
						EnableWindow(hEditPort, FALSE);
						EnableWindow(hUDPCheck, FALSE);
						EnableWindow(g_hButtonSendMsg, TRUE);
						SetFocus(hEditMsg);
					}
				}
				else { // UDP ���� <����>

					// ���� UDP��� ������ ����
					g_hClientThread = CreateThread(NULL, 0, ClientMainUDP, NULL, 0, NULL);
					if (g_hClientThread == NULL) {
						MessageBox(hDlg, "UDP Ŭ���̾�Ʈ�� ������ �� �����ϴ�."
							"\r\n���α׷��� �����մϴ�.", "����!", MB_ICONERROR);
						EndDialog(hDlg, 0);
					}
					else {
						EnableWindow(hButtonConnect, FALSE);
						while (g_bStart == FALSE); // ���� ���� ���� ��ٸ�
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
			// �б� �ϷḦ ��ٸ�
			WaitForSingleObject(g_hReadEvent, INFINITE);
			GetDlgItemText(hDlg, IDC_MSG, g_chatmsg.buf, MSGSIZE);
			// ���� �ϷḦ �˸�
			SetEvent(g_hWriteEvent);
			// �Էµ� �ؽ�Ʈ ��ü�� ���� ǥ��
			SendMessage(hEditMsg, EM_SETSEL, 0, -1);
			return TRUE;

			// ����
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
			if (MessageBox(hDlg, "������ �����Ͻðڽ��ϱ�?",
				"����", MB_YESNO | MB_ICONQUESTION) == IDYES)
			{
				closesocket(g_sock);
				EndDialog(hDlg, IDCANCEL);
			}
			return TRUE;

			//����
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

// ���� ��� ������ �Լ�
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
	MessageBox(NULL, "������ TCP�� �����߽��ϴ�.", "����!", MB_ICONINFORMATION);

	// �б� & ���� ������ ����
	HANDLE hThread[2];
	hThread[0] = CreateThread(NULL, 0, ReadThread, NULL, 0, NULL);
	hThread[1] = CreateThread(NULL, 0, WriteThread, NULL, 0, NULL);
	if (hThread[0] == NULL || hThread[1] == NULL) {
		MessageBox(NULL, "�����带 ������ �� �����ϴ�."
			"\r\n���α׷��� �����մϴ�.",
			"����!", MB_ICONERROR);
		exit(1);
	}

	g_bStart = TRUE;

	// ������ ���� ���
	retval = WaitForMultipleObjects(2, hThread, FALSE, INFINITE);
	retval -= WAIT_OBJECT_0;
	if (retval == 0)
		TerminateThread(hThread[1], 1);
	else
		TerminateThread(hThread[0], 1);
	CloseHandle(hThread[0]);
	CloseHandle(hThread[1]);

	g_bStart = FALSE;

	MessageBox(NULL, "������ ������ �������ϴ�", "�˸�", MB_ICONINFORMATION);
	EnableWindow(g_hButtonSendMsg, FALSE);

	closesocket(g_sock);
	return 0;
}

// ������ �ޱ�
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

// ������ ������
DWORD WINAPI WriteThread(LPVOID arg)
{
	int retval;


	// ������ ������ ���
	while (1) {
		// ���� �Ϸ� ��ٸ���
		WaitForSingleObject(g_hWriteEvent, INFINITE);

		// ���ڿ� ���̰� 0�̸� ������ ����
		if (strlen(g_chatmsg.buf) == 0) {
			// '�޽��� ����' ��ư Ȱ��ȭ
			EnableWindow(g_hButtonSendMsg, TRUE);
			// �б� �Ϸ� �˸���
			SetEvent(g_hReadEvent);
			continue;
		}

		// ������ ������
		retval = send(g_sock, (char*)&g_chatmsg, BUFSIZE, 0);
		if (retval == SOCKET_ERROR) {
			break;
		}

		// '�޽��� ����' ��ư Ȱ��ȭ
		EnableWindow(g_hButtonSendMsg, TRUE);
		// �б� �Ϸ� �˸���
		SetEvent(g_hReadEvent);
	}

	return 0;
}

// ���� UDP Client ������
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
		MessageBox(NULL, "������ UDPv4�� �����߽��ϴ�.", "����!", MB_ICONINFORMATION);
	}

	else {
		listen_sock_UDPv6 = socket(AF_INET6, SOCK_DGRAM, 0);
		if (listen_sock_UDPv6 == INVALID_SOCKET) err_quit("socket()");

		send_sock_UDPv6 = socket(AF_INET6, SOCK_DGRAM, 0);
		if (send_sock_UDPv6 == INVALID_SOCKET) err_quit("socket()");

		hThread[0] = CreateThread(NULL, 0, ReadThread_UDPv6, NULL, 0, NULL);
		hThread[1] = CreateThread(NULL, 0, WriteThread_UDPv6, NULL, 0, NULL);
		MessageBox(NULL, "������ UDPv6�� �����߽��ϴ�.", "����!", MB_ICONINFORMATION);
	}

	// �б� & ���� ������ ���� UDP
	// UDP IP ���� ������ ������ ������ ��
	if (hThread[0] == NULL || hThread[1] == NULL) {
		MessageBox(NULL, "�����带 ������ �� �����ϴ�."
			"\r\n���α׷��� �����մϴ�.",
			"����!", MB_ICONERROR);
		exit(1);
	}

	g_bStart = TRUE;

	// ������ ���� ���
	retvalUDP = WaitForMultipleObjects(2, hThread, FALSE, INFINITE);
	retvalUDP -= WAIT_OBJECT_0;
	if (retvalUDP == 0)
		TerminateThread(hThread[1], 1);
	else
		TerminateThread(hThread[0], 1);
	CloseHandle(hThread[0]);
	CloseHandle(hThread[1]);

	g_bStart = FALSE;

	MessageBox(NULL, "������ ������ �������ϴ�", "�˸�", MB_ICONINFORMATION);
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

	// ��Ƽĳ��Ʈ �׷� ����
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

	// ��Ƽĳ��Ʈ �׷� Ż��
	retvalUDP = setsockopt(listen_sock_UDPv4, IPPROTO_IP, IP_DROP_MEMBERSHIP,
		(char*)&mreq_v4, sizeof(mreq_v4));
	if (retvalUDP == SOCKET_ERROR) err_quit("setsockopt()");

	return 0;
}

DWORD WINAPI WriteThread_UDP(LPVOID arg) {
	int retvalUDP;

	//int ttl_v4 = 2; // ����
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
			// �б� �Ϸ� �˸���
			SetEvent(g_hReadEvent);
			continue;
		}
		// ������ ������
		retvalUDP = sendto(send_sock_UDPv4, (char*)&g_chatmsg, BUFSIZE, 0,
			(SOCKADDR*)&remoteaddr_v4, sizeof(remoteaddr_v4));
		if (retvalUDP == SOCKET_ERROR) {
			DisplayText_Send("writethread_UDP_Break\r\n");
			break;
		}

		// '�޽��� ����' ��ư Ȱ��ȭ
		EnableWindow(g_hButtonSendMsg, TRUE);

		// �б� �Ϸ� �˸���
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


	// �ּ� ��ȯ(���ڿ� -> IPv6)
	SOCKADDR_IN6 tmpaddr;
	int addrlenTmp = sizeof(tmpaddr);
	WSAStringToAddress(MULTICASTIPv6, AF_INET6, NULL,
		(SOCKADDR*)&tmpaddr, &addrlenTmp);

	// ��Ƽĳ��Ʈ �׷� ����
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

	// ��Ƽĳ��Ʈ �׷� Ż��
	retvalUDP = setsockopt(listen_sock_UDPv6, IPPROTO_IPV6, IPV6_DROP_MEMBERSHIP,
		(char*)&mreq_v6, sizeof(mreq_v6));
	if (retvalUDP == SOCKET_ERROR) err_quit("setsockopt()");

	return 0;
}

DWORD WINAPI WriteThread_UDPv6(LPVOID arg) {
	int retvalUDP;

	//int ttl_v4 = 2; // ����
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
			// �б� �Ϸ� �˸���
			SetEvent(g_hReadEvent);
			continue;
		}
		// ������ ������
		retvalUDP = sendto(send_sock_UDPv6, (char*)&g_chatmsg, BUFSIZE, 0,
			(SOCKADDR*)&remoteaddr_v6, sizeof(remoteaddr_v6));
		if (retvalUDP == SOCKET_ERROR) {
			DisplayText_Send("writethread_UDP_Break\r\n");
			break;
		}

		// '�޽��� ����' ��ư Ȱ��ȭ
		EnableWindow(g_hButtonSendMsg, TRUE);

		// �б� �Ϸ� �˸���
		SetEvent(g_hReadEvent);
	}

	return 0;
}


// �ڽ� ������ ���ν���
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
		// ȭ���� ������ ��Ʈ�� ����
		cx = GetDeviceCaps(hDC, HORZRES);
		cy = GetDeviceCaps(hDC, VERTRES);
		hBitmap = CreateCompatibleBitmap(hDC, cx, cy);

		// �޸� DC ����
		hDCMem = CreateCompatibleDC(hDC);

		// ��Ʈ�� ���� �� �޸� DC ȭ���� ������� ĥ��
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

			// �� �׸��� �޽��� ������
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

		//����,��
		if (g_drawmsg.type == DRAWLINE || g_drawmsg.type == DRAWSTRA) {
			// ȭ�鿡 �׸���
			hOldPen = (HPEN)SelectObject(hDC, hPen);
			MoveToEx(hDC, LOWORD(wParam), HIWORD(wParam), NULL);
			LineTo(hDC, LOWORD(lParam), HIWORD(lParam));
			SelectObject(hDC, hOldPen);

			// �޸� ��Ʈ�ʿ� �׸���
			hOldPen = (HPEN)SelectObject(hDCMem, hPen);
			MoveToEx(hDCMem, LOWORD(wParam), HIWORD(wParam), NULL);
			LineTo(hDCMem, LOWORD(lParam), HIWORD(lParam));
			SelectObject(hDC, hOldPen);

			DeleteObject(hPen);
			ReleaseDC(hWnd, hDC);
		}

		//�ﰢ��
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

		//�簢��
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

		//��
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

		// �޸� ��Ʈ�ʿ� ����� �׸��� ȭ�鿡 ����
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

// ����Ʈ ��Ʈ�ѿ� ���ڿ� ���
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

// ����
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

// ����� ���� ������ ���� �Լ�
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

// ���� �Լ� ���� ��� �� ����
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

// ���� �Լ� ���� ���
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