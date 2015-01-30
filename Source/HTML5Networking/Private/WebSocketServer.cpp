// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.
#if !PLATFORM_HTML5

#include "HTML5NetworkingPCH.h"
#include "WebSocketServer.h"
#include "WebSocket.h"

#if PLATFORM_WINDOWS
#include "AllowWindowsPlatformTypes.h"
#endif

#include "libwebsockets.h"

#if PLATFORM_WINDOWS
#include "HideWindowsPlatformTypes.h"
#endif

// a object of this type is associated by libwebsocket to every connected session. 
struct PerSessionData
{
	FWebSocket *Socket; // each session is actually a socket to a client
};


// real networking handler. 
static int unreal_networking_server(
	struct libwebsocket_context *,
	struct libwebsocket *wsi,
	enum libwebsocket_callback_reasons reason,
	void *user,
	void *in,
	size_t
	len
);

#if !UE_BUILD_SHIPPING
	void libwebsocket_debugLog(int level, const char *line)
	{ 
		UE_LOG(LogHTML5Networking, Warning, TEXT("websocket server: %s"), ANSI_TO_TCHAR(line));
	}
#endif 

bool FWebSocketServer::Init(uint32 Port, FWebsocketClientConnectedCallBack CallBack)
{
	// setup log level.
#if !UE_BUILD_SHIPPING
	lws_set_log_level(LLL_ERR | LLL_WARN | LLL_NOTICE | LLL_DEBUG | LLL_INFO, libwebsocket_debugLog);
#endif 

	Protocols = new libwebsocket_protocols[3];
	FMemory::Memzero(Protocols, sizeof(libwebsocket_protocols) * 3);

	Protocols[0].name = "binary";
	Protocols[0].callback = FWebSocket::unreal_networking_server;
	Protocols[0].per_session_data_size = sizeof(PerSessionData);
	Protocols[0].rx_buffer_size = 10 * 1024 * 1024;

	Protocols[1].name = nullptr;
	Protocols[1].callback = nullptr;
	Protocols[1].per_session_data_size = 0;

	struct lws_context_creation_info Info;
	memset(&Info, 0, sizeof(lws_context_creation_info));
	// look up libwebsockets.h for details. 
	Info.port = Port;
	// we listen on all available interfaces. 
	Info.iface = NULL;
	Info.protocols = &Protocols[0];
	// no extensions
	Info.extensions = NULL;
	Info.gid = -1;
	Info.uid = -1;
	Info.options = 0;
	// tack on this object. 
	Info.user = this;
	Info.port = Port; 
	Context = libwebsocket_create_context(&Info);

	if (Context == NULL) 
	{
		return false; // couldn't create a server. 
	}

	ConnectedCallBack = CallBack; 

	return true;
}

bool FWebSocketServer::Tick()
{
	{
		libwebsocket_service(Context, 0);
		libwebsocket_callback_on_writable_all_protocol(&Protocols[0]);
	}
	return true;
}

FWebSocketServer::FWebSocketServer()
{}

FWebSocketServer::~FWebSocketServer()
{
	if (Context)
	{
		libwebsocket_context_destroy(Context);
		Context = NULL;
	}

	 delete Protocols; 
	 Protocols = NULL; 
}

FString FWebSocketServer::Info()
{
	return FString(ANSI_TO_TCHAR(libwebsocket_canonical_hostname(Context)));
}

// callback. 
int FWebSocket::unreal_networking_server
	(
		struct libwebsocket_context *Context, 
		struct libwebsocket *Wsi, 
		enum libwebsocket_callback_reasons Reason, 
		void *User, 
		void *In, 
		size_t Len
	)
{
	PerSessionData* BufferInfo = (PerSessionData*)User;
	FWebSocketServer* Server = (FWebSocketServer*)libwebsocket_context_user(Context);

	switch (Reason)
	{
		case LWS_CALLBACK_ESTABLISHED: 
			{
				BufferInfo->Socket = new FWebSocket(Context, Wsi);
				Server->ConnectedCallBack.ExecuteIfBound(BufferInfo->Socket);
				libwebsocket_set_timeout(Wsi, NO_PENDING_TIMEOUT, 0);
			}
			break;

		case LWS_CALLBACK_RECEIVE:
			{
				BufferInfo->Socket->OnRawRecieve(In, Len);
				libwebsocket_set_timeout(Wsi, NO_PENDING_TIMEOUT, 0);
			}
			break; 

		case LWS_CALLBACK_SERVER_WRITEABLE: 
			{
				BufferInfo->Socket->OnRawWebSocketWritable(Wsi);
				libwebsocket_set_timeout(Wsi, NO_PENDING_TIMEOUT, 0);
			}
			break; 
		case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
			{
				BufferInfo->Socket->ErrorCallBack.ExecuteIfBound();
			}
			break;
	}

	return 0; 
}

#endif 