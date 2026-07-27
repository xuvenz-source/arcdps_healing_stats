#include "ServerFrontendGrpc.h"

#include "Server.h"
#include "../src/Log.h"

#include <chrono>
#include <memory>
#include <mutex>
#include <shared_mutex>

ServerFrontendGrpc::ConnectionInterfaceGrpc::ConnectionInterfaceGrpc(ServerFrontendGrpc* pFrontend)
	: Frontend{pFrontend}
{}

void ServerFrontendGrpc::ConnectionInterfaceGrpc::SendEventToClient(
	const cbtevent& pEvent,
	uint16_t pSenderInstanceId,
	const std::shared_ptr<ConnectionContext>& pConnectionContext,
	const ConnectionContext* pSenderForLogging)
{
	std::lock_guard lock(WriteLock);

	evtc_rpc::messages::CombatEvent message;
	message.Event = pEvent;
	message.SenderInstanceId = pSenderInstanceId;

	if (WritePending == false)
	{
		Frontend->SendEvent(message, new WriteEventCallData(std::shared_ptr<ConnectionContext>(pConnectionContext)), pConnectionContext);
	}
	else
	{
		QueuedEvents.emplace_back(std::move(message));
		LogT("(client {}) Queued CombatEvent from {}", fmt::ptr(this), fmt::ptr(pSenderForLogging));
	}
}

void ServerFrontendGrpc::ConnectionInterfaceGrpc::ForceDisconnect(
	const char* pErrorMessage,
	const std::shared_ptr<ConnectionContext>& pClient,
	bool pRemovedFromTable)
{
	std::lock_guard lock(WriteLock);

	if (pClient->ForceDisconnected.exchange(true, std::memory_order_acq_rel) == true)
	{
		LogD("client is already disconnected");
		return;
	}

	if (Frontend->IsShuttingDown())
	{
		LogI("(client {}) force disconnected (removedFromTable={}) - '{}'. Server is shutting down so not queueing a Finish",
			fmt::ptr(pClient.get()), BOOL_STR(pRemovedFromTable), pErrorMessage);
		return;
	}

	DisconnectCallData* queuedData = new DisconnectCallData{ std::shared_ptr{pClient} };
	Stream.Finish(grpc::Status{ grpc::StatusCode::INVALID_ARGUMENT, pErrorMessage }, queuedData);

	LogI("(client {} tag {}) force disconnected (removedFromTable={}) - '{}'", fmt::ptr(pClient.get()), fmt::ptr(queuedData), BOOL_STR(pRemovedFromTable), pErrorMessage);
}

ServerFrontendGrpc::ServerFrontendGrpc(const char* pListeningEndpoint, const grpc::SslServerCredentialsOptions* pCredentialsOptions, std::shared_ptr<evtc_rpc_server>&& pServer)
	: mServer{pServer}
{
	grpc::ServerBuilder builder;
	builder.AddChannelArgument("GRPC_ARG_KEEPALIVE_TIME_MS", 60000);
	builder.AddChannelArgument("GRPC_ARG_KEEPALIVE_TIMEOUT_MS", 10000);
	builder.AddChannelArgument("GRPC_ARG_KEEPALIVE_PERMIT_WITHOUT_CALLS", 0); // We really don't care about cancelling connections that are not doing anything
	builder.AddChannelArgument("GRPC_ARG_HTTP2_MAX_PINGS_WITHOUT_DATA", 0); // Keep sending keepalive pings forever
	builder.AddChannelArgument("GRPC_ARG_HTTP2_MIN_RECV_PING_INTERVAL_WITHOUT_DATA_MS", 300000); // Does this need configuring? Client is not supposed to be sending keepalives. Keeping it at default
	builder.AddChannelArgument("GRPC_ARG_HTTP2_MAX_PING_STRIKES", 2); // Default

	if (pCredentialsOptions != nullptr)
	{
		auto channel_creds = grpc::SslServerCredentials(*pCredentialsOptions);
		builder.AddListeningPort(pListeningEndpoint, channel_creds);
	}
	else
	{
		builder.AddListeningPort(pListeningEndpoint, grpc::InsecureServerCredentials());
	}

	builder.RegisterService(&mService);

	mCompletionQueue = builder.AddCompletionQueue();
	mGrpcServer = builder.BuildAndStart();

	LogI("Started listening - pListeningEndpoint={}", pListeningEndpoint);
}

void ServerFrontendGrpc::ThreadStartServe(void* pThis)
{
#ifdef LINUX
	pthread_setname_np(pthread_self(), "evtc-rpc-grpc");
#elif defined(_WIN32)
	SetThreadDescription(GetCurrentThread(), L"evtc-rpc-grpc");
#endif

	reinterpret_cast<ServerFrontendGrpc*>(pThis)->Serve();
}

void ServerFrontendGrpc::Serve()
{
	ConnectCallData* connectCallData = new ConnectCallData{MakeConnectionContext()};
	{
		ConnectionInterfaceGrpc& intf = static_cast<ConnectionInterfaceGrpc&>(*connectCallData->Context->Interface);
		mService.RequestConnect(&intf.ServerContext, &intf.Stream, mCompletionQueue.get(), mCompletionQueue.get(), connectCallData);
	}

	LogT("(tag {}) Queued Connect", fmt::ptr(connectCallData));

	while (true)
	{
		void* tag;
		bool ok;
		if (mCompletionQueue->Next(&tag, &ok) == false)
		{
			LogI("mCompletionQueue->Next returned false, returning");
			return;
		}

		if (mShutdownState.load(std::memory_order_relaxed) == ShutdownState::ShouldShutdown)
		{
			std::unique_lock lock{ mShutdownLock };
			if (mShutdownState.load(std::memory_order_relaxed) == ShutdownState::ShouldShutdown)
			{
				LogI("Starting shutdown");
				// Wait a few milliseconds so we get a chance to flush out all pending messages
				mGrpcServer->Shutdown(std::chrono::system_clock::now() + std::chrono::milliseconds(100));
				mCompletionQueue->Shutdown();

				ShutdownState expected = ShutdownState::ShouldShutdown;
				if (mShutdownState.compare_exchange_strong(expected, ShutdownState::ShuttingDown, std::memory_order_relaxed) == false)
				{
					// Shouldn't be able to happen - this transition should be guarded by mShutdownLock
					LogE("Not changing mShutdownState since it's {}", static_cast<int>(expected));
				}
				else
				{
					LogI("Set mShutdownState to ShuttingDown");
				}
			}
		}

		std::shared_lock lock{ mShutdownLock };

		CallDataType tag_type = static_cast<CallDataBase*>(tag)->Type;
		if (tag_type < CallDataType::Max)
		{
			mServer->SubmitStatistics().CallData[static_cast<size_t>(tag_type)]->Increment();
		}

		ShutdownState shutdown_state = mShutdownState.load(std::memory_order_relaxed);
		if (ok == false || shutdown_state == ShutdownState::ShuttingDown)
		{
			LogI("(tag {}) Got not-ok or shutdown({}) (type {})", fmt::ptr(tag), static_cast<int>(shutdown_state), static_cast<int>(tag_type));

			switch (tag_type)
			{
			case CallDataType::Connect:
			{
				ConnectCallData* message = static_cast<ConnectCallData*>(tag);
				delete message;
				break;
			}
			case CallDataType::ReadMessage:
			{
				ReadMessageCallData* message = static_cast<ReadMessageCallData*>(tag);
				LogI("(client {} tag {}) ReadMessage got not-ok, closing connection", fmt::ptr(message->Context.get()), fmt::ptr(tag));

				mServer->ForceDisconnect("shutdown by client", message->Context);

				delete message;
				break;
			}
			case CallDataType::WriteEvent:
			{
				WriteEventCallData* message = static_cast<WriteEventCallData*>(tag);
				delete message;
				break;
			}
			case CallDataType::Disconnect:
			{
				DisconnectCallData* message = static_cast<DisconnectCallData*>(tag);
				delete message;
				break;
			}
			case CallDataType::WakeUp:
			{
				WakeUpCallData* message = static_cast<WakeUpCallData*>(tag);
				delete message;
				break;
			}
			default:
				LogC("Invalid CallDataType {}", static_cast<int>(tag_type));
				assert(false);
			}

			continue;
		}

		switch (tag_type)
		{
		case CallDataType::Connect:
		{
			ConnectCallData* message = static_cast<ConnectCallData*>(tag);
			HandleConnect(message);

			connectCallData->Context = MakeConnectionContext();
			ConnectionInterfaceGrpc& intf = static_cast<ConnectionInterfaceGrpc&>(*connectCallData->Context->Interface);
			mService.RequestConnect(&intf.ServerContext, &intf.Stream, mCompletionQueue.get(), mCompletionQueue.get(), connectCallData);
			break;
		}
		case CallDataType::ReadMessage:
		{
			ReadMessageCallData* message = static_cast<ReadMessageCallData*>(tag);
			HandleReadMessage(message);

			// Requeue the same tag for a new read. This has to be done after the the handler is done to ensure there isn't a race between two ReadMessages
			ConnectionInterfaceGrpc& intf = static_cast<ConnectionInterfaceGrpc&>(*message->Context->Interface);
			intf.Stream.Read(&message->Message, message);
			break;
		}
		case CallDataType::WriteEvent:
		{
			WriteEventCallData* message = static_cast<WriteEventCallData*>(tag);
			HandleWriteEvent(message);
			break;
		}
		case CallDataType::Disconnect:
		{
			DisconnectCallData* message = static_cast<DisconnectCallData*>(tag);
			delete message;
			break;
		}
		case CallDataType::WakeUp:
		{
			WakeUpCallData* message = static_cast<WakeUpCallData*>(tag);
			delete message;
			break;
		}
		default:
			LogC("Invalid CallDataType {}", static_cast<int>(tag_type));
			assert(false);
		}
	}
}

void ServerFrontendGrpc::Shutdown()
{
	ShutdownState expected = ShutdownState::Online;
	if (mShutdownState.compare_exchange_strong(expected, ShutdownState::ShouldShutdown, std::memory_order_relaxed) == false)
	{
		LogI("Not changing mShutdownState since it's {}", static_cast<int>(expected));
	}
	else
	{
		LogI("Set mShutdownState to ShouldShutdown");
		WakeUpCallData* calldata = new WakeUpCallData;
		calldata->Alarm->Set(mCompletionQueue.get(), std::chrono::system_clock::now(), calldata);
	}
}

bool ServerFrontendGrpc::IsShuttingDown() const
{
	return mShutdownState.load(std::memory_order_relaxed) == ShutdownState::ShuttingDown;
}

std::shared_ptr<ConnectionContext> ServerFrontendGrpc::MakeConnectionContext()
{
	return std::make_shared<ConnectionContext>(std::make_unique<ConnectionInterfaceGrpc>(this));
}

void ServerFrontendGrpc::HandleConnect(ConnectCallData* pCallData)
{
	ConnectionInterfaceGrpc& intf = static_cast<ConnectionInterfaceGrpc&>(*pCallData->Context->Interface);

	// Add a ReadMessageCallData so we can start reading messages on this new connection
	{
		ReadMessageCallData* queuedData = new ReadMessageCallData{ std::shared_ptr{pCallData->Context} };
		intf.Stream.Read(&queuedData->Message, queuedData);
	}

	LogI("(client {} tag {}) new connection from {}",
		fmt::ptr(pCallData->Context.get()), fmt::ptr(pCallData), intf.ServerContext.peer().c_str());
}

void ServerFrontendGrpc::HandleReadMessage(ReadMessageCallData* pCallData)
{
	using namespace evtc_rpc::messages;

	const std::string& blob = pCallData->Message.blob();
	const char* data = blob.data();
	size_t dataSize = blob.size();

	if (dataSize < sizeof(Header))
	{
		LogE("(client {} tag {}) data too short for header ({} vs {})",
			fmt::ptr(pCallData->Context.get()), fmt::ptr(pCallData), dataSize, sizeof(Header));
		mServer->ForceDisconnect("short message header", pCallData->Context);
		return;
	}

	mServer->HandleIncomingMessage(data, dataSize, pCallData->Context);
}

void ServerFrontendGrpc::HandleWriteEvent(WriteEventCallData* pCallData)
{
	ConnectionInterfaceGrpc& intf = static_cast<ConnectionInterfaceGrpc&>(*pCallData->Context->Interface);

	std::lock_guard lock(intf.WriteLock);

	assert(intf.WritePending == true);
	intf.WritePending = false;

	if (intf.QueuedEvents.size() > 0)
	{
		SendEvent(intf.QueuedEvents.front(), pCallData, pCallData->Context);
		intf.QueuedEvents.pop_front();
	}
	else
	{
		LogT("(client {} tag {}) No more events queued", fmt::ptr(pCallData->Context.get()), fmt::ptr(pCallData));
		delete pCallData;
	}
}

void ServerFrontendGrpc::SendEvent(const evtc_rpc::messages::CombatEvent& pEvent, WriteEventCallData* pCallData, const std::shared_ptr<ConnectionContext>& pClient)
{
	ConnectionInterfaceGrpc& intf = static_cast<ConnectionInterfaceGrpc&>(*pClient->Interface);
	assert(intf.WritePending == false);

	evtc_rpc::messages::Header header;
	header.MessageVersion = 1;
	header.MessageType = evtc_rpc::messages::Type::CombatEvent;

	std::string blob;
	blob.resize(sizeof(header) + sizeof(pEvent));
	memcpy(blob.data(), &header, sizeof(header));
	memcpy(blob.data() + sizeof(header), &pEvent, sizeof(pEvent));

	evtc_rpc::Message rpc_message;
	rpc_message.set_blob(std::move(blob));
	intf.Stream.Write(rpc_message, pCallData);

	intf.WritePending = true;

	mServer->SubmitStatistics().MessageTypeTransmit[static_cast<size_t>(evtc_rpc::messages::Type::CombatEvent)]->Increment();

	LogT("(client {} tag {}) Sending CombatEvent from {} source {} target {} skill {} value {}",
		fmt::ptr(pClient.get()), fmt::ptr(pCallData), pEvent.SenderInstanceId, pEvent.Event.src_instid, pEvent.Event.dst_instid, pEvent.Event.skillid, pEvent.Event.value);
}
