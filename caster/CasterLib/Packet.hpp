#pragma once


enum ClientPacketType
{
	//! Ustawia opis klienta
	// <device-name:string>
	CLIENTPT_GetImage,

	//! Rzada danych
	// <length>
	//	<sector-id:unsigned>
	CLIENTPT_GetData, 

	CLIENTPT_GetPong,

	//! ¯¹da danych bloka
	// <sector:unsigned>
	// <length:length>
	//  <frag:unsigned short>
	CLIENTPT_GetBlockData,

	//! Wysyla dane do serwera (zdalne tworzenie obrazu)
	// <device-name>
	CLIENTPT_SendImage,

	//! Wysyla kolejne paczki danych (istniejacy blok)
	// <sector-id>
	CLIENTPT_SendBlock,

	//! Wysyla dane (wielkosci dowolnej)
	// <offset:long long> <data>
	CLIENTPT_SendData,

	//! Koniec transmisji
	CLIENTPT_SendCommit
};

enum ServerPacketType
{
	//! Odsyla hello
	// <image-name:string>
	// <length>
	//   <sector-id:unsigned>
	//   <disk-offset:long long>
	//   <disk-size:long long>
	SERVERPT_Image, 
	SERVERPT_Ping,
	SERVERPT_Block,
	SERVERPT_BlockData,
	SERVERPT_Ready,
	SERVERPT_Finished,

	//! Wysyla liste dostepnych blokow
	//! <length:length>
	//!   <id:unsigned>
	//!   <hash:Hash>
	SERVERPT_SendImage,
	SERVERPT_BlockList,

	SERVERPT_Message
};

enum CasterFinishedErrorCode {
	Finished,
	NoDeviceFound,
	InvalidDeviceName,
	InvalidBlockID,
	InvalidBlockData
};

inline string va(CasterFinishedErrorCode errorCode) {
	switch(errorCode) {
		case Finished:						return "finished";
		case NoDeviceFound:				return "no device found";
		case InvalidDeviceName:		return "invalid device name";
		case InvalidBlockID:			return "invalid block ID";
		case InvalidBlockData:		return "invalid block data";
		default:									return "(unknown)";
	}
}

#pragma pack(1)
struct CasterPacket {
	byte Type;
};

struct CasterPacketFinished : CasterPacket {
	byte ErrorCode;
};

struct CasterPacketGetImage : CasterPacket {
	char DeviceName[MAX_DEVICE_NAME];
};

struct CasterPacketGetPong : CasterPacket {
};

struct CasterPacketGetData : CasterPacket {
	unsigned Count;
	unsigned List[1];

	static CasterPacketGetData* alloc(unsigned count) {
		CasterPacketGetData* self = new(sizeof(CasterPacketGetData) + count * sizeof(unsigned)) CasterPacketGetData;
		self->Count = count;
		return self;
	}

	unsigned size() const {
		return sizeof(*this) + Count * sizeof(unsigned) - sizeof(List);
	}
};

struct CasterPacketPing : CasterPacket {
};

struct CasterPacketImage : CasterPacket {
	unsigned short Version;
	char ImageName[MAX_IMAGE_NAME];
	unsigned Multicast;
};

struct CasterPacketBlock : CasterPacket {
	unsigned Id;
	unsigned DataSize, RealSize;
	::Hash Hash;
	unsigned DataCrc32;
	unsigned Count;
	long long List[1];

	static CasterPacketBlock* alloc(unsigned count) {
		CasterPacketBlock* self = new(sizeof(CasterPacketBlock) + count * sizeof(long long)) CasterPacketBlock;
		self->Count = count;
		return self;
	}

	unsigned size() const {
		return sizeof(*this) + Count * sizeof(long long) - sizeof(List);
	}
};


struct CasterPacketSenderBlock : CasterPacket {
	long long Offset;
	unsigned Id;
};

struct CasterPacketSenderBlockData : CasterPacket {
	long long Offset;
	::Hash Hash;
	unsigned DataCrc32;
	unsigned DataSize;
	unsigned RealSize;
};

struct CasterPacketBlockData : CasterPacket {
	unsigned Id;
	::Hash Hash;
	unsigned DataSize;

	const char* data() const {
		return (const char*)(this+1);
	}
	char* data() {
		return (char*)(this+1);
	}

	static CasterPacketBlockData* alloc(unsigned size) {
		CasterPacketBlockData* self = new(sizeof(CasterPacketBlockData) + size) CasterPacketBlockData;
		self->DataSize = size;
		return self;
	}

	unsigned size() const {
		return sizeof(*this) + DataSize;
	}
};

struct CasterPacketSenderHash {
	unsigned Id;
	::Hash Hash;
};

struct CasterPacketSenderHashList : CasterPacket {
	unsigned Count;
	CasterPacketSenderHash List[1];

	static CasterPacketSenderHashList* alloc(unsigned count) {
		CasterPacketSenderHashList* self = new(sizeof(CasterPacketSenderHashList) + count * sizeof(CasterPacketSenderHash)) CasterPacketSenderHashList;
		self->Count = count;
		return self;
	}

	unsigned size() const {
		return sizeof(*this) + Count * sizeof(List[0]) - sizeof(List);
	}
};

struct CasterPacketSendImage : CasterPacket {
	char DeviceName[MAX_DEVICE_NAME];
};
#pragma pack()