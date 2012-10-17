#pragma once

#pragma pack(1)
enum MclType {
	MclNone,
	
	MclJoin, // send by receiver
	MclJoinResponse, // send by sender and receiver
	
	MclKeepAlive, // send by sender
	MclData, // send by sender
	MclUpdate, // send by receiver
	
	MclLeave, // send by receiver
	MclLeaveResponse // send by sender and receiver
};

const int MaxPacketSize = 60000;

struct MclPacket {
	byte Type;
};

struct MclJoinPacket : MclPacket {
	long long ID;
};

struct MclJoinResponsePacket : MclPacket {
	long long ID;
	unsigned short Seq;
	unsigned Tick; // minimalny oferowany numer sekwencyjny
	bool Accept : 1;
};

struct MclLeavePacket : MclPacket {
};

struct MclLeaveResponsePacket : MclPacket {
};

struct MclKeepAlivePacket : MclPacket {
	unsigned short Seq, MaxSeq; // minimalny oferowany numer sekwencyjny, kolejny numer do wys³ania
	unsigned Tick;
};

struct MclDataPacket : MclKeepAlivePacket {
	bool KeepAlive : 1;
};

struct MclDataPacket2 : MclDataPacket {
	byte Data[MaxPacketSize];
};

struct MclUpdatePacket : MclPacket {
	unsigned short Seq, MaxSeq; // nastepny oczekiwany numer sekwencyjny, maksymalny widziany numer sekwencyjny
	unsigned Tick;
	float Rate;
};

struct MclUpdatePacket2 : MclUpdatePacket {
	byte Data[1400];
};
#pragma pack()
