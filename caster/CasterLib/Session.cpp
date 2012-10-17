#define DEBUG_LEVEL SERVER_DEBUG_LEVEL
#include "CasterLib.hpp"

CasterSession::CasterSession(CasterServer& server, SocketType socket) : PacketSock(socket), Server(server) {
	Server.m_sessionList.push_back(this);

	infof("Session %s connected.", sockName().c_str());
}

CasterSession::~CasterSession() {
	CasterSessionList::iterator itor = std::find(Server.m_sessionList.begin(), Server.m_sessionList.end(), this);
	if(itor != Server.m_sessionList.end())
		Server.m_sessionList.erase(itor);

	if(isValid())
		infof("Session %s disconnected.", sockName().c_str());
}

void CasterSession::onPacket(const void* data, unsigned size) {
	if(size == 0) {
		close();
	}

	const CasterPacket* packet = (const CasterPacket*)data;

	switch(packet->Type) {
		case CLIENTPT_GetImage:
			{
				CasterSessionClient* session = new CasterSessionClient(Server, *this);
				session->onGetImage(*(const CasterPacketGetImage*)data);
				close();
			}
			break;

		case CLIENTPT_SendImage:
			{
				CasterSessionSender* session = new CasterSessionSender(Server, *this);
				session->onSendImage(*(const CasterPacketSendImage*)data);
				close();
			}
			break;

		default:
			debugp("session", "got unknown packet type from client");
			close();
			break;
	}
}
