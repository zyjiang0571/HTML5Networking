// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.
#include "HTML5NetworkingPCH.h"
#include "WebSocket.h"

#if PLATFORM_HTML5
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <assert.h>
#include <emscripten.h>
#endif

#if PLATFORM_WINDOWS
#include "AllowWindowsPlatformTypes.h"
#endif


#if !PLATFORM_HTML5
#include "libwebsockets.h"
#include "private-libwebsockets.h"
#endif 

#if PLATFORM_WINDOWS
#include "HideWindowsPlatformTypes.h"
#endif


#if !PLATFORM_HTML5
uint8 PREPADDING[LWS_SEND_BUFFER_PRE_PADDING];
uint8 POSTPADDING[LWS_SEND_BUFFER_POST_PADDING];
#endif 

// real networking handler. 

// a object of this type is associated by libwebsocket to every connected session. 
struct PerSessionData
{
	uint32	WriteState; 
	uint32  TotalDataSent; 
};

#if !PLATFORM_HTML5
static void libwebsocket_debugLogS(int level, const char *line)
{
	UE_LOG(LogHTML5Networking, Warning, TEXT("client: %s"), ANSI_TO_TCHAR(line));
}
#endif 

FWebSocket::FWebSocket(
		const FInternetAddr& ServerAddress
)
:IsServerSide(false)
{

#if !PLATFORM_HTML5_BROWSER

#if !UE_BUILD_SHIPPING
	lws_set_log_level(LLL_ERR | LLL_WARN | LLL_NOTICE | LLL_DEBUG | LLL_INFO, libwebsocket_debugLogS);
#endif 

	Protocols = new libwebsocket_protocols[3];
	FMemory::Memzero(Protocols, sizeof(libwebsocket_protocols) * 3);

	Protocols[0].name = "binary";
	Protocols[0].callback = FWebSocket::unreal_networking_client;
	Protocols[0].per_session_data_size = sizeof(PerSessionData);
	Protocols[0].rx_buffer_size = 10 * 1024 * 1024;

	Protocols[1].name = nullptr;
	Protocols[1].callback = nullptr;
	Protocols[1].per_session_data_size = 0;

	struct lws_context_creation_info Info;
	memset(&Info, 0, sizeof Info);

	Info.port = CONTEXT_PORT_NO_LISTEN;
	Info.protocols = &Protocols[0];
	Info.gid = -1;
	Info.uid = -1;
	Info.user = this;

	Context = libwebsocket_create_context(&Info);

	check(Context); 


	Wsi = libwebsocket_client_connect_extended
							(Context, 
							TCHAR_TO_ANSI(*ServerAddress.ToString(false)), 
							ServerAddress.GetPort(), 
							false, "/", TCHAR_TO_ANSI(*ServerAddress.ToString(false)), TCHAR_TO_ANSI(*ServerAddress.ToString(false)), Protocols[1].name, -1,this);

	check(Wsi);

#endif 

#if PLATFORM_HTML5_BROWSER

	struct sockaddr_in addr;
	int res;

	SockFd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (SockFd == -1) {
		UE_LOG(LogHTML5Networking, Error, TEXT("Socket creationg failed "));
	}
	else
	{
		UE_LOG(LogHTML5Networking, Warning, TEXT(" Socked %d created "), SockFd);
	}

	fcntl(SockFd, F_SETFL, O_NONBLOCK);

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(ServerAddress.GetPort());

	if (inet_pton(AF_INET, TCHAR_TO_ANSI(*ServerAddress.ToString(false)), &addr.sin_addr) != 1) {
		UE_LOG(LogHTML5Networking, Warning, TEXT("inet_pton failed "));
		return; 
	}

	int Ret = connect(SockFd, (struct sockaddr *)&addr, sizeof(addr));
	UE_LOG(LogHTML5Networking, Warning, TEXT(" Connect socket returned %d"), Ret);

#endif 
}

FWebSocket::FWebSocket(WebSocketInternalContext* InContext, WebSocketInternal* InWsi)
:	Context(InContext), 
	Wsi(InWsi),
	IsServerSide(true)
{
}


bool FWebSocket::Send(uint8* Data, uint32 Size)
{
	TArray<uint8> Buffer;
	// insert size. 

#if !PLATFORM_HTML5
	Buffer.Append((uint8*)&PREPADDING, sizeof(PREPADDING));
#endif 

	Buffer.Append((uint8*)&Size, sizeof (uint32));
	Buffer.Append((uint8*)Data, Size);

#if !PLATFORM_HTML5
	Buffer.Append((uint8*)&POSTPADDING, sizeof(POSTPADDING));
#endif 

	OutgoingBuffer.Add(Buffer);

	return true;
}

void FWebSocket::SetRecieveCallBack(FWebsocketPacketRecievedCallBack CallBack)
{
	RecievedCallBack = CallBack; 
}

FString FWebSocket::RemoteEndPoint()
{
#if !PLATFORM_HTML5
	ANSICHAR Peer_Name[128];
	ANSICHAR Peer_Ip[128];
	libwebsockets_get_peer_addresses(Context, Wsi, libwebsocket_get_socket_fd(Wsi), Peer_Name, sizeof Peer_Name, Peer_Ip, sizeof Peer_Ip);
	return FString(Peer_Name);
#endif

#if PLATFORM_HTML5
	return FString(TEXT("TODO:REMOTEENDPOINT"));
#endif
}


FString FWebSocket::LocalEndPoint()
{
#if !PLATFORM_HTML5
	return FString(ANSI_TO_TCHAR(libwebsocket_canonical_hostname(Context)));
#endif 

#if PLATFORM_HTML5
	return FString(TEXT("TODO:LOCALENDPOINT"));
#endif

}

void FWebSocket::Tick()
{

#if !PLATFORM_HTML5
	{
		libwebsocket_service(Context, 0);
		if (!IsServerSide)
			libwebsocket_callback_on_writable_all_protocol(&Protocols[0]);
	}
#endif 

#if PLATFORM_HTML5

	fd_set fdr;
	fd_set fdw;
	int res;

	// make sure that server.fd is ready to read / write
	FD_ZERO(&fdr);
	FD_ZERO(&fdw);
	FD_SET(SockFd, &fdr);
	FD_SET(SockFd, &fdw);
	res = select(64, &fdr, &fdw, NULL, NULL);

	if (res == -1) {
		UE_LOG(LogHTML5Networking, Warning, TEXT("Select Failed!"));
		return;
	}
	
	if (FD_ISSET(SockFd, &fdr)) {
		// we can read! 
		this->OnRawRecieve(NULL, NULL);
	}

	if (FD_ISSET(SockFd, &fdw)) {
		// we can write
		this->OnRawWebSocketWritable(NULL);
	}

#endif 
}

void FWebSocket::SetConnectedCallBack(FWebsocketInfoCallBack CallBack)
{
	ConnectedCallBack = CallBack; 
}

void FWebSocket::SetErrorCallBack(FWebsocketInfoCallBack CallBack)
{
	ErrorCallBack = CallBack; 
}

void FWebSocket::OnRawRecieve(void* Data, uint32 Size)
{
#if !PLATFORM_HTML5
	RecievedBuffer.Append((uint8*)Data, Size);

	while (RecievedBuffer.Num())
	{
		uint32 BytesToBeRead = *(uint32*)RecievedBuffer.GetData();
		if (BytesToBeRead <= ((uint32)RecievedBuffer.Num() - sizeof(uint32)))
		{
			RecievedCallBack.Execute((void*)((uint8*)RecievedBuffer.GetData() + sizeof(uint32)), BytesToBeRead);
			RecievedBuffer.RemoveAt(0, sizeof(uint32) + BytesToBeRead );
		}
		else
		{
			break;
		}
	}
#endif

#if PLATFORM_HTML5

	uint8 Buffer[1024]; // should be at MAX PACKET SIZE. 
	int Result = recv(SockFd, Buffer, sizeof(uint32), 0);

	uint32 DataToBeRead = 0;*(uint32*)Buffer;

	if (Result == -1) 
	{
		UE_LOG(LogHTML5Networking, Log, TEXT("Read message size failed!"));
		this->ErrorCallBack.ExecuteIfBound(); 
	}
	else
	{
		DataToBeRead = *(uint32*)Buffer;
		UE_LOG(LogHTML5Networking, Log, TEXT("Read 4 bytes showing the size"), DataToBeRead);
	}

	check(Result == sizeof(uint32)); 

	// read rest of the data. 
	Result = recv(SockFd, Buffer, DataToBeRead, 0);

	if(Result < 0 )
	{
		UE_LOG(LogHTML5Networking, Log, TEXT("Read message failed!"));
		this->ErrorCallBack.ExecuteIfBound();
	}
	else
	{
		UE_LOG(LogHTML5Networking, Log, TEXT("Read %d bytes and Executing."), DataToBeRead);
		check(DataToBeRead == Result);
		RecievedCallBack.Execute(Buffer, DataToBeRead);
	}

#endif 

}

void FWebSocket::OnRawWebSocketWritable(WebSocketInternal* wsi)
{
	if (OutgoingBuffer.Num() == 0)
		return;

	TArray <uint8>& Packet = OutgoingBuffer[0];

#if !PLATFORM_HTML5_BROWSER

	uint32 TotalDataSize = Packet.Num() - LWS_SEND_BUFFER_PRE_PADDING - LWS_SEND_BUFFER_POST_PADDING;

	int Sent = libwebsocket_write(Wsi, Packet.GetData() + LWS_SEND_BUFFER_PRE_PADDING, TotalDataSize, (libwebsocket_write_protocol)LWS_WRITE_BINARY);

	check(Sent == TotalDataSize); // if this fires we need a slightly more complicated buffering mechanism. 
	check(Wsi == wsi);

#endif

#if  PLATFORM_HTML5_BROWSER

	// send actual data in one go. 
	int Result = send(SockFd, (uint32*)Packet.GetData(),Packet.Num(), 0);

	if (Result == -1)
	{
		// we are caught with our pants down. fail. 
		UE_LOG(LogHTML5Networking, Log, TEXT("Could not write %d bytes"), Packet.Num());
		this->ErrorCallBack.ExecuteIfBound(); 
	}
	else
	{
		check(Result == Packet.Num());
		UE_LOG(LogHTML5Networking, Log, TEXT("Wrote %d bytes"), Packet.Num());
	}
	
#endif 

	// this is very inefficient we need a constant size circular buffer to efficiently not do unnecessary allocations/deallocations. 
	OutgoingBuffer.RemoveAt(0);

}

FWebSocket::~FWebSocket()
{
#if !PLATFORM_HTML5
	if ( !IsServerSide)
	{
		libwebsocket_context_destroy(Context);
		Context = NULL;
		delete Protocols;
		Protocols = NULL;
	}
#endif 

#if PLATFORM_HTML5
	close(SockFd);
#endif 

}

#if !PLATFORM_HTML5
int FWebSocket::unreal_networking_client(
		struct libwebsocket_context *Context, 
		struct libwebsocket *Wsi, 
		enum libwebsocket_callback_reasons Reason, 
		void *User, 
		void *In, 
		size_t Len)
{
	FWebSocket* Socket = (FWebSocket*)libwebsocket_context_user(Context);;
	switch (Reason)
	{
	case LWS_CALLBACK_CLIENT_ESTABLISHED:
		{
			Socket->ConnectedCallBack.ExecuteIfBound();
			libwebsocket_set_timeout(Wsi, NO_PENDING_TIMEOUT, 0);
			check(Socket->Wsi == Wsi);
		}
		break;
	case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
		{
			Socket->ErrorCallBack.ExecuteIfBound();
			return -1;
		}
		break;
	case LWS_CALLBACK_CLIENT_RECEIVE:
		{
			// push it on the socket. 
			Socket->OnRawRecieve(In, (uint32)Len); 
			check(Socket->Wsi == Wsi);
			libwebsocket_set_timeout(Wsi, NO_PENDING_TIMEOUT, 0);
			break;
		}
	case LWS_CALLBACK_CLIENT_WRITEABLE:
		{
			check(Socket->Wsi == Wsi);
			Socket->OnRawWebSocketWritable(Wsi); 
			libwebsocket_callback_on_writable(Context, Wsi);
			libwebsocket_set_timeout(Wsi, NO_PENDING_TIMEOUT, 0);
			break; 
		}
	case LWS_CALLBACK_CLOSED:
		{
			Socket->ErrorCallBack.ExecuteIfBound();
			return -1;
		}
	}

	return 0; 
}
#endif 

