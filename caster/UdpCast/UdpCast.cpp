#include <cmath>
#include "../AsyncLib/AsyncLib.hpp"
#include "../AsyncLib/Common.hpp"
#include "../AsyncLib/Log.hpp"
#include "../AsyncLib/ForEach.hpp"
#include "UdpCastPacket.hpp"
#include "UdpCastClient.hpp"
#include "UdpCastServer.hpp"

#ifdef _WIN32
#include <io.h>
#define write _write
#endif // _WIN32

#ifndef _WIN32
#define CSV_SERVER_FD		3
#define CSV_CLIENT_FD		4
#endif

struct Client : public UdpCastClient {
	char index;
	unsigned TransmissionCount;

	bool onConsumeData(const string& data) {
		//static bool consume = true;
		#ifdef _WIN32
		bool consume = (~GetKeyState(VK_SHIFT)&0x80) != 0;
		#else
		bool consume = true;
		#endif

		bool invalid = false;

		if(data.empty()) {
			index = 0;
			++TransmissionCount;
		}

		char savedIndex = index;

		for(unsigned i = 0; i < data.size(); ++i) {
			if(data[i] != index++) {
				invalid = true;
			}
		}

		if(invalid)
			++InvalidDataCount;

		if(!consume)
			index = savedIndex;

		return consume;
	}
};

struct Server : public UdpCastServer {
	char index;
	unsigned TransmissionCount;

	virtual bool onGetData(string& data, unsigned maxSize) {
		if((rand() % 2000) == 0) {
			++TransmissionCount;
			data.clear();
			index = 0;
			return true;
		}

		data.resize(maxSize);
		for(unsigned i = 0; i < data.size(); ++i) {
			data[i] = index++;
		}
		return true;
	}
};

const string BindAddress = "127.0.0.1";
const string SendAddress = "127.0.0.1";
const short Port = 6000;

#ifdef _WIN32
#include <conio.h>
static void gotoxy(short x, short y) {
	COORD coord = {x, y};
	SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), coord);
}
#define clrscr() system("cls")
#else // _WIN32
#define clrscr() printf("\033[2J")
#define gotoxy(x,y) printf("\033[%i;%iH",x,y)
#endif // _WIN32

static void printf2(const char* fmt, ...) {
	char buffer[151];
	memset(buffer, 0, sizeof(buffer));
	va_list list;
	va_start(list, fmt);
	vsnprintf(buffer, 150, fmt, list);
	#ifdef _WIN32
	buffer[strlen(buffer)] = ' ';
	buffer[149] = 0;
	buffer[150] = 0;
	#endif
	
	printf("%s\r", buffer);
	fflush(stdout);
}

struct UdpTest {
	auto_ptr< ::Server> Server;
	auto_ptr< ::Client> Client;

	UdpTest(const char* what, const char* bind, const char* send, const char* multicast) {
		Timer::after(TimerDelegate(this, &UdpTest::showRate), 1.0);

		if(!strcmp(what, "loop") || !strcmp(what, "server")) {
			Server.reset(new ::Server);
			Server->createServer(bind);
			if(multicast)
				Server->setSendAddress(multicast);
			else
				Server->setSendAddress(send);
			Server->setBlocking(true);
			Server->setSendBufferSize(65536);
			Server->sendKeepAlive();

			if(getenv("SMAXSIZE"))
				Server->MaxSize = atoi(getenv("SMAXSIZE"));

#ifdef CSV_SERVER_FD
			const char* header = "Time;Uptime;SendRate;RecvRate;ContentLength;SendLength;NakCount;SendCount;KeepAliveCount;UpdateCount;SlowStart;WindowSize;RTT;Clients;TransmissionCount;\n";
			write(CSV_SERVER_FD, header, strlen(header));
#endif
		}

		if(!strcmp(what, "loop") || !strcmp(what, "client")) {
			Client.reset(new ::Client);
			Client->createServer(!strcmp(what, "loop") ? send : bind, true);
			Client->setSendAddress(!strcmp(what, "loop") ? bind : send);
			if(multicast)
				Client->addGroupAddress(multicast);
			Client->setBlocking(true);
			Client->sendJoin();

#ifdef CSV_CLIENT_FD
			const char* header = "Time;Uptime;SendRate;RecvRate;ContentLength;KeepAliveCount;UpdateCount;DataCount;DataDuplicate;InvalidDataCount;Seq;WindowSize;TransmissionCount;\n";
			write(CSV_CLIENT_FD, header, strlen(header));
#endif
		}
	}

	double showRate(unsigned) {
		unsigned line = 0;

		clrscr();
		gotoxy(1, 1);		
		
		if(Server.get()) {
			printf("[SERVER]\n");
			printf("Send: %4ikbps\nRecv: %2ikbps\n", unsigned(Server->sendRate() >> 10) * 8, unsigned(Server->recvRate() >> 10) * 8);
			printf("ContentLength: %ikB\nSendLength: %ikB\n", unsigned(Server->ContentLength >> 10), unsigned(Server->SendLength >> 10));
			printf("NakCount: %i / %2.1f%%\nDataCount: %i\nKeepAliveCount: %i / %2.1f%%\n", Server->NakCount, Server->NakCount * 100.0f / Server->SendCount, 
				Server->SendCount, Server->KeepAliveCount, Server->KeepAliveCount * 100.0f / Server->SendCount);
			printf("UpdateCount: %i\nSlowStart: %i\nWindow: %i\nRTT: %2.2f\nClients: %2i\nTransmissions: %i\n\n", 
				Server->UpdateCount, Server->SlowStart, Server->windowSize(), Server->RTT / 1000.0,
				Server->clientCount(), Server->TransmissionCount);

#ifdef CSV_SERVER_FD
			string csv = va("%s;%lf;%i;%i;%i;%i;%i;%i;%i;%i;%i;%i;%f;%i;%i;\n",
				vaTime().c_str(), timef(),
				unsigned(Server->sendRate() >> 10) * 8, unsigned(Server->recvRate() >> 10) * 8,
				unsigned(Server->ContentLength >> 10), unsigned(Server->SendLength >> 10),
				Server->NakCount, Server->SendCount, Server->KeepAliveCount, Server->UpdateCount, Server->SlowStart, Server->windowSize(), Server->RTT / 1000.0,
				Server->clientCount(), Server->TransmissionCount
				);
			write(CSV_SERVER_FD, csv.c_str(), csv.length());
#endif // CSV_FD
		}

		if(Client.get()) {
			printf("[CLIENT]\n");
			printf("Send: %2ikbps\nRecv: %4ikbps\n", unsigned(Client->sendRate() >> 10) * 8, unsigned(Client->recvRate() >> 10) * 8);
			printf("ContentLength: %ikB\nKeepAliveCount: %i\nUpdateCount: %i\nDataCount: %i\nDataDuplicate: %i\nDataInvalid: %i\nWindow: %i\nTransmissions: %i\n\n", 
				unsigned(Client->ContentLength >> 10), Client->KeepAliveCount, Client->UpdateCount, 
				Client->DataCount, Client->DataDuplicate, Client->InvalidDataCount, Client->windowSize(), Client->TransmissionCount);

#ifdef CSV_CLIENT_FD
			string csv = va("%s;%lf;%i;%i;%i;%i;%i;%i;%i;%i;%i;%i;\n",
				vaTime().c_str(), timef(),
				unsigned(Client->sendRate() >> 10) * 8, unsigned(Client->recvRate() >> 10) * 8,
				unsigned(Client->ContentLength >> 10), Client->KeepAliveCount, Client->UpdateCount,
				Client->DataCount, Client->DataDuplicate, Client->InvalidDataCount, Client->windowSize(), Client->TransmissionCount);
			write(CSV_CLIENT_FD, csv.c_str(), csv.length());
#endif // CSV_CLIENT_FD
		}
		
		fflush(stdout);

		return 1.0;
	}
};

#ifdef _WIN32
#pragma comment(lib, "Winmm.lib")
#endif // _WIN32

int main(int argc, char* argv[]) {
	if(argc != 4 && argc != 5) {
		printf("usage: %s [server|client|loop] <bind:port> <send:port> [multicast]\n", argv[0]);
		exit(0);
	}

#ifdef _WIN32
	TIMECAPS caps;
	timeGetDevCaps(&caps, sizeof(caps));
	timeBeginPeriod(caps.wPeriodMin);
#endif // _WIN32

	//Verbosity = 4;
	UdpTest test(argv[1], argv[2], argv[3], argc == 4 ? NULL : argv[4]);
	while(Fd::waitForEvents(1100)) {
		//printf("-- STEP --\n");
	}
	return 0;
}
