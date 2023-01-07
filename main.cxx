#include <cstdint>
#include <functional>
#include <iostream>
#include <memory>
#include <random>
#include <sstream>
#include <string>

#include "vtkLogger.h"

#include "asio.hpp"              // for asio
#include "rxcpp/rx-includes.hpp" // for rxcpp

namespace io = asio;
using tcp = io::ip::tcp;
using error_code = io::error_code;

namespace vtk_services {
using RegistryType = std::unordered_map<std::string, std::size_t>;

/**
 * Encapsulate two SimpleSubject(s) used to enqueue incoming/outgoing messages.
 */
struct Communicator {
  // services can use the send subject to enqueue outgoing messages.
  rxcpp::subjects::subject<std::string> sendSbjct;
  unsigned long sendCounter = 0;
  // use the recvSubject to act upon new incoming messages.
  rxcpp::subjects::subject<std::string> recvSbjct;
  unsigned long recvCounter = 0;
};

//-------------------------------------------------------------------
RegistryType::value_type CreateService(const std::string serviceName) {
  const auto gid = std::hash<std::string>{}(serviceName);
  const auto serviceItem =
      std::make_pair(std::string("services.") + serviceName, gid);
  vtkLogF(TRACE, "=> Created %s:%zu", serviceItem.first.c_str(),
          serviceItem.second);
  return serviceItem;
}

//-------------------------------------------------------------------
std::string ToText(RegistryType registry) {
  std::stringstream msg;
  for (const auto &serviceItem : registry) {
    msg << serviceItem.first << ":" << serviceItem.second << '|';
  }
  return msg.str();
}

//-------------------------------------------------------------------
RegistryType FromText(std::string text) {
  RegistryType registry;
  while (!text.empty()) {
    auto itemSep = text.find_first_of('|');
    auto colonSep = text.find_first_of(':');
    if (itemSep != std::string::npos && colonSep != std::string::npos) {
      std::string serviceName = text.substr(0, colonSep);
      text = text.replace(0, colonSep + 1, "");
      std::string servicegid = text.substr(0, itemSep - colonSep - 1);
      text = text.replace(0, itemSep - colonSep, "");
      std::stringstream gidStream;
      gidStream << servicegid;
      std::size_t gid = 0;
      gidStream >> gid;
      registry.emplace(std::make_pair(serviceName, gid));
      vtkLogF(INFO, "=> %s:%zu", serviceName.c_str(), gid);
    }
  }
  return registry;
}

//-------------------------------------------------------------------
std::string ToHex(const std::string &input) {

  std::ostringstream output;
  for (const char &c : input) {
    output << std::hex << int(c);
  }
  return output.str();
}

//-------------------------------------------------------------------
bool MatchesAscii(const char *input, const std::size_t length,
                  const std::string match) {
  bool result = true;
  std::string hexMatch = ToHex(match);
  for (int i = 0; i < length; ++i) {
    result &= (input[i] == hexMatch[i]);
  }
  return result;
}

// emulate a service.
//-------------------------------------------------------------------
rxcpp::composite_subscription ServiceEmulator(Communicator &comm,
                                              const std::string serviceName,
                                              const std::size_t serviceId) {
  return comm.recvSbjct
      .get_observable()
      // ignore zero-length messages
      .filter([](auto msg) { return (msg.length() != 0); })
      // route to correct destination
      .filter([serviceId](std::string msg) {
        return (msg.find(std::to_string(serviceId)) != std::string::npos);
      })
      // remove that service gid.
      .map([](std::string msg) {
        auto colonSep = msg.find(":");
        return msg.substr(colonSep + 1, msg.length() - colonSep);
      })
      // log message without gid.
      .tap([](std::string msg) {
        vtkLogF(TRACE, "=> Enqueue msg: %s", msg.c_str());
      })
      // switch to service thread.
      .observe_on(rxcpp::observe_on_new_thread())
      // service can now act accordingly. here, we log the message.
      .subscribe(
          // on_next
          [serviceName, &comm](auto msg) {
            comm.recvCounter++;
            vtkLogger::SetThreadName(serviceName);
            // do something. for example, execute a command, run
            // vtkAlgorithm::Update(), render, read/write file.
            // for now, just log the message.
            vtkLogF(INFO, "%s", msg.c_str());
            // send a reply.
            std::string reply = std::string("response-") + msg;
            comm.sendSbjct.get_subscriber().on_next(reply);
          },
          // on_completed
          []() { vtkLogF(INFO, "exit"); });
}
} // namespace vtk_services

namespace vtk_services_asio {
// Keep the data alive until async_write completes.
// This class is similar to shared_const_buffer found in
// asio/src/examples/cpp11/buffers/reference_counted.cpp
class SharedConstBuffer {
public:
  explicit SharedConstBuffer(const std::string &data)
      : mData(new std::vector<char>(data.begin(), data.end())),
        mBuffer(asio::buffer(*mData)) {}
  // Implement the ConstBufferSequence requirements.
  using value_type = asio::const_buffer;
  using const_iterator = const asio::const_buffer *;
  const asio::const_buffer *begin() const { return &mBuffer; }
  const asio::const_buffer *end() const { return &mBuffer + 1; }

private:
  std::shared_ptr<std::vector<char>> mData;
  asio::const_buffer mBuffer;
};

/* This class wraps up low-level asio::async_ free functions on a socket.
 * It follows similar design as the s11n_example::connection class
 * found in asio/src/examples/cpp03/serialization/connection.hpp
 */
class Connection : public std::enable_shared_from_this<Connection> {
public:
  Connection(io::io_context &ioCtx);
  ~Connection() { vtkLog(TRACE, << "Connection destroyed"); }
  tcp::socket &Socket();

  // Write/read message to/from the underlying socket for this connection.
  void AsyncRead(std::string &message);
  void AsyncWrite(const std::string &message);

  // handle completed read of header.
  void HandleReadHeader(const error_code &ec, std::string &message);
  // handle completed read of data.
  void HandleReadData(const error_code &ec, std::string &message);

  // read handler
  std::function<void(const error_code &ec, const std::vector<char> &data)>
      OnRead = nullptr;
  // write handler
  std::function<void(const error_code &ec)> OnWrite = nullptr;
  // error handler
  std::function<void(const error_code &ec)> OnError = nullptr;

private:
  // socket used to read, write data
  tcp::socket mSocket;
  // no. of bytes for a fixed length header
  static constexpr int mHeaderlength = 8;
  // holds outbound header i.e, the size of data following the header
  std::string mOutboundHeader;
  // holds outbound data
  std::string mOutboundData;
  // holds inbound header
  char mInboundHeader[mHeaderlength] = {};
  // holds inbound data
  std::vector<char> mInboundData;
  // used to post execution of read, write, error handlers.
  io::io_context &mIOCtx;
};

using ConnectionPtr = std::shared_ptr<Connection>;

//-------------------------------------------------------------------
Connection::Connection(io::io_context &ioCtx) : mIOCtx(ioCtx), mSocket(ioCtx) {
  vtkLog(TRACE, << "New connection");
}

//-------------------------------------------------------------------
tcp::socket &Connection::Socket() { return this->mSocket; }

//-------------------------------------------------------------------
void Connection::AsyncRead(std::string &message) {
  // Issue a read operation to read exactly the number of bytes in a header.
  asio::async_read(this->mSocket, asio::buffer(this->mInboundHeader),
                   [this, &message, self = shared_from_this()](
                       const error_code &ec, std::size_t /*length*/) {
                     this->HandleReadHeader(ec, message);
                   });
}

//-------------------------------------------------------------------
void Connection::HandleReadHeader(const error_code &ec, std::string &message) {
  if (ec) {
    vtkLog(ERROR, << "Failed to read header. Closing socket.");
    this->mSocket.close();
    if (this->OnError) {
      this->OnError(ec);
    } else {
      throw ec;
    }
  } else {
    // determine length of data.
    std::istringstream is(
        std::string(this->mInboundHeader, this->mHeaderlength));
    std::size_t inboundDataSize = 0;
    if (!(is >> std::hex >> inboundDataSize)) {
      // header is invalid. inform the caller.
      if (this->OnError != nullptr) {
        this->OnError(asio::error::invalid_argument);
      }
      return;
    }

    vtkLog(TRACE, "About to read " << inboundDataSize << " bytes");
    // start an async read of the data.
    this->mInboundData.resize(inboundDataSize);
    asio::async_read(this->mSocket, asio::buffer(this->mInboundData),
                     [this, &message, self = shared_from_this()](
                         const error_code &ec, std::size_t /*length*/) {
                       this->HandleReadData(ec, message);
                     });
  }
}

//-------------------------------------------------------------------
void Connection::HandleReadData(const error_code &ec, std::string &message) {
  if (ec) {
    vtkLog(ERROR, << "Failed to read data. Closing socket.");
    this->mSocket.close();
    if (this->OnError) {
      this->OnError(ec);
    } else {
      throw ec;
    }
  } else {
    // extract the message from the data.
    vtkLog(TRACE, "Read " << std::string(this->mInboundData.begin(),
                                         this->mInboundData.end()));
    message.clear();
    message.assign(this->mInboundData.begin(), this->mInboundData.end());
    if (this->OnRead != nullptr) {
      // inform caller that message was received.
      this->OnRead(ec, this->mInboundData);
    }
  }
}

//-------------------------------------------------------------------
void Connection::AsyncWrite(const std::string &message) {
  // format the header.
  this->mOutboundData = message;
  std::ostringstream headerStream;
  headerStream << std::setw(mHeaderlength) << std::hex
               << this->mOutboundData.size();
  if ((!headerStream || headerStream.str().size() != mHeaderlength) &&
      this->OnError) {
    // something went wrong, inform the caller.
    asio::post(this->mIOCtx,
               [this]() { this->OnError(asio::error::invalid_argument); });
  }
  this->mOutboundHeader = headerStream.str();

  // write the data to socket. use "gather-write" to send
  // both header and data in a single write operation.
  SharedConstBuffer buffer(this->mOutboundHeader + this->mOutboundData);
  vtkLog(TRACE,
         "Write " << this->mOutboundHeader << "+" << this->mOutboundData);
  asio::async_write(this->mSocket, buffer,
                    [this, self = shared_from_this()](const error_code &ec,
                                                      std::size_t /*length*/) {
                      if (ec) {
                        vtkLog(ERROR,
                               << "Failed to write message. Closing socket.");
                        this->mSocket.close();
                        if (this->OnError) {
                          this->OnError(ec);
                        } else {
                          throw ec;
                        }
                      } else if (this->OnWrite != nullptr) {
                        this->OnWrite(ec);
                      }
                    });
}

class Client {
public:
  Client(io::io_context &ioCtx, std::string host, std::string port);
  vtk_services::Communicator &Communicator();
  vtk_services::RegistryType &ServiceRegistry();
  std::size_t GetServiceGid(const std::string &serviceName);

protected:
  // handle completion of a connect operation
  void HandleConnect(const error_code &ec, tcp::endpoint ep);
  // handle completion a `Connection::AsyncRead` operation.
  void HandleRead(const error_code &ec);
  // handle completion a `Connection::AsyncWrite` operation.
  void HandleWrite(const error_code &ec);

private:
  // our connection to the server
  ConnectionPtr mConnection;
  // data received from the server
  std::string mMessage;
  // service registry.
  vtk_services::RegistryType mServiceRegistry;
  // communicator used to interact with main application and client.
  vtk_services::Communicator mCommunicator;
};

//-------------------------------------------------------------------
Client::Client(io::io_context &ioCtx, std::string host, std::string port)
    : mConnection(new Connection(ioCtx)) {
  // setup handlers
  this->mConnection->OnError = [](const error_code &ec) {
    vtkLog(ERROR, << "Error code " << ec.value());
  };
  using namespace std::placeholders;
  this->mConnection->OnRead = std::bind(&Client::HandleRead, this, _1);
  this->mConnection->OnWrite = std::bind(&Client::HandleWrite, this, _1);
  // Resolve the host name into an IP address.
  tcp::resolver resolver(ioCtx);
  tcp::resolver::results_type endpoints = resolver.resolve(host, port);
  // Start an asynchronous connect operation.
  asio::async_connect(this->mConnection->Socket(), endpoints,
                      [this](const error_code &ec, tcp::endpoint ep) {
                        this->HandleConnect(ec, ep);
                      });
}

//-------------------------------------------------------------------
vtk_services::Communicator &Client::Communicator() {
  return this->mCommunicator;
}

//-------------------------------------------------------------------
vtk_services::RegistryType &Client::ServiceRegistry() {
  return this->mServiceRegistry;
}

//-------------------------------------------------------------------
std::size_t Client::GetServiceGid(const std::string &serviceName) {
  return this->mServiceRegistry.count(serviceName)
             ? this->mServiceRegistry.at(serviceName)
             : 0;
}

//-------------------------------------------------------------------
void Client::HandleConnect(const error_code &ec, tcp::endpoint ep) {
  if (!ec) {
    vtkLog(INFO, << "Connected!");
    this->mConnection->AsyncRead(this->mMessage);
  } else {
    vtkLog(ERROR, << "Connection failed!. Error " << ec.value());
  }
}

//-------------------------------------------------------------------
void Client::HandleRead(const error_code &ec) {
  const std::string match = vtk_services::ToHex("paraview");
  const std::size_t length = match.length();
  // check if the first 16 characters match 7061726176696577
  if (this->mMessage.size() >= length &&
      vtk_services::MatchesAscii(&this->mMessage[0], length, "paraview")) {
    // parse service registry.
    this->mServiceRegistry = vtk_services::FromText(
        std::string(this->mMessage.begin() + length, this->mMessage.end()));
    vtkLog(TRACE, << "Registry received : \n"
                  << vtk_services::ToText(this->mServiceRegistry));
    this->mCommunicator.sendSbjct
        .get_observable()
        // enqueue on main thread.
        .filter([](std::string msg) { return (msg.length() != 0); })
        // switch to another thread and transmit from there.
        .observe_on(rxcpp::observe_on_new_thread())
        .subscribe([this](std::string msg) {
          vtkLogger::SetThreadName("comm.send");
          this->mConnection->AsyncWrite(msg);
          this->mCommunicator.sendCounter++;
        });
  } else {
    // push this->mMessage into recvSubject.
    this->mCommunicator.recvSbjct.get_subscriber().on_next(this->mMessage);
  }
  this->mConnection->AsyncRead(this->mMessage);
}

//-------------------------------------------------------------------
void Client::HandleWrite(const error_code &ec) {
  // do nothing.
}

class Server {
public:
  Server(io::io_context &ioCtx, std::uint16_t port);
  ~Server();
  // register your services.
  void RegisterService(vtk_services::RegistryType::value_type &&serviceItem);
  vtk_services::Communicator &Communicator();
  vtk_services::RegistryType &ServiceRegistry();
  std::size_t GetServiceGid(const std::string &serviceName);

protected:
  // create a new connection with our io_context.
  // Then, setup error, read and write handlers.
  ConnectionPtr NewConnection();
  // handle completion of an accept operation.
  void HandleAccept(const error_code &ec, ConnectionPtr connection);
  // handle completion a `Connection::AsyncRead` operation.
  void HandleRead(const error_code &ec);
  // handle completion a `Connection::AsyncWrite` operation.
  void HandleWrite(const error_code &ec);

private:
  // our connection to a client
  ConnectionPtr mConnection;
  // used to accept incoming connections
  tcp::acceptor mAcceptor;
  // used to create new connection objects for clients
  io::io_context &mIOCtx;
  // a map of services.
  vtk_services::RegistryType mServiceRegistry;
  // message received from client.
  std::string mMessage;
  // used to communicate with service threads from server main thread.
  vtk_services::Communicator mCommunicator;
  // service subscriptions.
  std::vector<rxcpp::composite_subscription> mSubscriptions;
};

//-------------------------------------------------------------------
Server::Server(io::io_context &ioCtx, std::uint16_t port)
    : mIOCtx(ioCtx), mAcceptor(ioCtx, tcp::endpoint(tcp::v4(), port)) {
  this->mConnection = this->NewConnection();
  using namespace std::placeholders;
  this->mAcceptor.async_accept(
      this->mConnection->Socket(),
      std::bind(&Server::HandleAccept, this, _1, this->mConnection));
}

//-------------------------------------------------------------------
Server::~Server() {
  for (auto &s : this->mSubscriptions) {
    s.unsubscribe();
  }
}

//-------------------------------------------------------------------
void Server::RegisterService(
    vtk_services::RegistryType::value_type &&serviceItem) {
  this->mServiceRegistry.emplace(serviceItem);
  this->mSubscriptions.emplace_back(vtk_services::ServiceEmulator(
      this->mCommunicator, serviceItem.first, serviceItem.second));
}

//-------------------------------------------------------------------
vtk_services::Communicator &Server::Communicator() {
  return this->mCommunicator;
}

//-------------------------------------------------------------------
vtk_services::RegistryType &Server::ServiceRegistry() {
  return this->mServiceRegistry;
}

//-------------------------------------------------------------------
std::size_t Server::GetServiceGid(const std::string &serviceName) {
  return this->mServiceRegistry.count(serviceName)
             ? this->mServiceRegistry.at(serviceName)
             : 0;
}

//-------------------------------------------------------------------
ConnectionPtr Server::NewConnection() {
  auto conn = std::make_shared<Connection>(this->mIOCtx);
  // setup handlers.
  conn->OnError = [this](const error_code &ec) {
    vtkLog(ERROR, << "Error code " << ec.value());
  };
  using namespace std::placeholders;
  conn->OnRead = std::bind(&Server::HandleRead, this, _1);
  conn->OnWrite = std::bind(&Server::HandleWrite, this, _1);
  return conn;
}

//-------------------------------------------------------------------
void Server::HandleAccept(const error_code &ec, ConnectionPtr connection) {
  if (!ec) {
    // Successfully accepted a new connection. Send the service registry.
    vtkLog(INFO, << "Accepted connection!");
    std::string serviceRegistryMessage =
        vtk_services::ToHex("paraview") +
        vtk_services::ToText(this->mServiceRegistry);
    connection->AsyncWrite(serviceRegistryMessage);
    this->mCommunicator.sendSbjct
        .get_observable()
        // enqueue on main thread.
        .filter([](std::string msg) { return (msg.length() != 0); })
        // switch to another thread and transmit from there.
        .observe_on(rxcpp::observe_on_new_thread())
        .subscribe([this](std::string msg) {
          vtkLogger::SetThreadName("comm.send");
          this->mConnection->AsyncWrite(msg);
          this->mCommunicator.sendCounter++;
        });
    connection->AsyncRead(this->mMessage);
  } else {
    vtkLog(ERROR, << "Failed to accept connections. Error " << ec.value());
    connection->Socket().close();
    return;
  }
}

//-------------------------------------------------------------------
void Server::HandleRead(const error_code &ec) {
  // push this->mMessage into recvSubject.
  this->mCommunicator.recvSbjct.get_subscriber().on_next(this->mMessage);
  this->mConnection->AsyncRead(this->mMessage);
}

//-------------------------------------------------------------------
void Server::HandleWrite(const error_code &ec) {
  // do nothing.
}

} // namespace vtk_services_asio

//-------------------------------------------------------------------
int main(int argc, char *argv[]) {
  std::string addr, port;
  int numMessages = 20;
  bool isServer = false;
  bool parseSuccess = false;

  for (int i = 1; i < argc; ++i) {
    const char *arg = argv[i];
    switch (arg[1]) {
    case 'c': {
      addr = argv[i + 1];
      port = argv[i + 2];
      parseSuccess = true;
      break;
    }
    case 's': {
      port = argv[i + 1];
      isServer = true;
      parseSuccess = true;
      break;
    }
    case 'v': {
      vtkLogger::SetStderrVerbosity(
          vtkLogger::ConvertToVerbosity(std::atoi(&arg[2])));
      parseSuccess = true;
      break;
    }
    case 'n': {
      numMessages = std::atoi(&arg[2]);
      parseSuccess = true;
      break;
    }
    default:
      break;
    }
  }

  if (!parseSuccess) {
    std::cerr << "Usage as server: ./main -s <port>\n";
    std::cerr
        << "Usage as client: ./main -c <address> <port> [-n <numMessages>]\n";
    return -1;
  }

  io::io_context ioCtx;
  using namespace vtk_services_asio;

  if (isServer) {
    // as a server
    Server server(ioCtx, std::atoi(port.c_str()));
    // create 3 services.
    for (const auto &serviceName : {"io", "data", "render"}) {
      server.RegisterService(vtk_services::CreateService(serviceName));
    }
    // run the server loop.
    std::thread t([&ioCtx]() { ioCtx.run(); });
    // the completion token of server's async_read simply calls async_read
    // again. Since we offloaded the io_context to run in a dedicated thread,
    // main thread is free to do other stuff. ex: execute
    // user python scripts and so on..
    std::cout << "Now simulating repl-like interaction with server. Enter q or "
                 "exit to finish the program.\n"
              << "Please prefix a valid service name.\n"
              << "Example: services.data:update\n> ";
    for (std::string line; std::getline(std::cin, line);) {
      auto service = line.substr(0, line.find_first_of(':'));
      if (auto sid = server.GetServiceGid(service)) {
        line = line.replace(0, line.find_first_of(':'), std::to_string(sid));
        server.Communicator().recvSbjct.get_subscriber().on_next(line);
      } else if (line == std::string("q") || line == std::string("exit")) {
        break;
      } else if (!line.empty()) {
        std::cerr
            << "Invalid input. Please prefix a valid service name. Example: "
               "services.data:update'\n";
      }
      std::cout << "> ";
    }
    std::thread wait_thread([&server]() {
      while (server.Communicator().recvCounter !=
             server.Communicator().sendCounter) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
    });
    // wait for sends == recvs
    wait_thread.join();
    ioCtx.stop();
    std::cout << "Sent(" << server.Communicator().sendCounter << "). Received("
              << server.Communicator().recvCounter << ")\n";
    t.join();
  } else {
    // as a client
    Client client(ioCtx, addr, port);
    std::thread t([&ioCtx]() { ioCtx.run(); });
    auto &serviceRegistry = client.ServiceRegistry();

    // before we begin sending messages, wait for communicator to initialize.
    while (!client.Communicator().sendSbjct.has_observers()) {
      vtkLog(TRACE, "=> wait for comm initialize");
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // fake some messages on three services.
    std::vector<std::string> dataMsgs = {
        std::to_string(serviceRegistry.at("services.data")) + ":update-state",
        std::to_string(serviceRegistry.at("services.data")) + ":object-delete",
        std::to_string(serviceRegistry.at("services.data")) +
            ":update-pipeline"};
    std::vector<std::string> renderMsgs = {
        std::to_string(serviceRegistry.at("services.render")) + ":render",
        std::to_string(serviceRegistry.at("services.render")) + ":render-2",
        std::to_string(serviceRegistry.at("services.render")) + ":render-3",
        std::to_string(serviceRegistry.at("services.render")) + ":render-4"};
    std::vector<std::string> ioMsgs = {
        std::to_string(serviceRegistry.at("services.io")) + ":read",
        std::to_string(serviceRegistry.at("services.io")) + ":read-2",
        std::to_string(serviceRegistry.at("services.io")) + ":write",
        std::to_string(serviceRegistry.at("services.io")) + ":read-3",
        std::to_string(serviceRegistry.at("services.io")) + ":write-2",
        std::to_string(serviceRegistry.at("services.io")) + ":read-4"};

    std::vector<std::vector<std::string>> messagePool(
        {dataMsgs, renderMsgs, ioMsgs});

    int counter = 0;
    std::random_device rd;
    std::mt19937 rng(rd());
    std::uniform_int_distribution<std::size_t> poolIdxRnd(0, 2);
    std::vector<std::uniform_int_distribution<std::size_t>> msgIdxRnds;
    msgIdxRnds.emplace_back(0, dataMsgs.size() - 1);
    msgIdxRnds.emplace_back(0, renderMsgs.size() - 1);
    msgIdxRnds.emplace_back(0, ioMsgs.size() - 1);

    // client issues remote commands on the server.
    // here, it can handle responses from remote services.
    std::atomic<int> numResponses = 0;
    auto responseSubscription =
        client.Communicator()
            .recvSbjct
            .get_observable()
            // ideally, we want to setup rxcpp notify on earlier wakeup.
            // to handle responses on main thread. but that is more work.
            // so handle responses on a new thread.
            .observe_on(rxcpp::observe_on_new_thread())
            .subscribe([&numResponses](std::string msg) {
              vtkLogger::SetThreadName("response");
              vtkLogF(INFO, "reply: %s", msg.c_str());
              numResponses++;
            });
    while (counter < numMessages) {
      // pick a random message collection from the pool.
      auto poolIdx = poolIdxRnd(rng);
      auto msgIdx = msgIdxRnds[poolIdx](rng);
      // message the service
      auto msg = messagePool[poolIdx][msgIdx];
      client.Communicator().sendSbjct.get_subscriber().on_next(msg);
      ++counter;
    }
    // wait until responses are received.
    while (numMessages != numResponses) {
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    // simulate a command-line like interaction with the server.
    // user types "services.data:update-state", we'll replace services.data
    // with a gid and send it.
    std::cout << "Sent(" << numMessages << "). Received(" << numResponses
              << ")\n";
    std::cout << "Now simulating interactive communication. Enter q or exit to "
                 "finish the program.\n"
              << "Please prefix a valid service name.\n"
              << "Example: services.data:update\n> ";
    for (std::string line; std::getline(std::cin, line);) {
      auto service = line.substr(0, line.find_first_of(':'));
      if (auto sid = client.GetServiceGid(service)) {
        line = line.replace(0, line.find_first_of(':'), std::to_string(sid));
        client.Communicator().sendSbjct.get_subscriber().on_next(line);
      } else if (line == std::string("q") || line == std::string("exit")) {
        break;
      } else if (!line.empty()) {
        std::cerr
            << "Invalid input. Please prefix a valid service name. Example: "
               "services.data:update'\n";
      }
      std::cout << "> ";
    }
    ioCtx.stop();
    t.join();
  }
  return 0;
}