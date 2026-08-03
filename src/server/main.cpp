#include <algorithm>
#include <asio.hpp>
#include <csignal>
#include <cstdint>
#include <iostream>
#include <thread>
#include <vector>

#include "core/dispatch/dispatcher.h"
#include "core/net/server.h"
#include "core/net/session.h"
#include "core/packet/packet.h"
#include "packet.pb.h"

using namespace game::core;

int main(int argc, char** argv) {
  GOOGLE_PROTOBUF_VERIFY_VERSION;

  const uint16_t port =
      (argc > 1) ? static_cast<uint16_t>(std::stoi(argv[1])) : 7777;
  const unsigned threads = std::max(1u, std::thread::hardware_concurrency());
  // ---- 핸들러 등록 (Echo) ----

  Dispatcher dispatcher;
  dispatcher.Register(
      PacketId::kEchoRequest,
      [](const SessionPtr& session, const uint8_t* body, uint16_t size) {
        game::proto::EchoRequest req;
        if (!req.ParseFromArray(body, size)) {
          std::cerr << "[echo] parse failed\n";
          return;
        }
        game::proto::EchoResponse res;
        res.set_message(req.message());
        session->Send(MakePacket(PacketId::kEchoResponse, res));
      });

  // ---- 서버 기동 ----
  asio::io_context io;
  Server server(io, port, dispatcher);
  server.Start();

  // ---- graceful shutdown ----
  asio::signal_set signals(io, SIGINT, SIGTERM);
  signals.async_wait([&io](std::error_code, int) {
    std::cout << "\n[server] shutting down\n";
    io.stop();
  });

  // ---- io_context 를 hardware_concurrency 만큼 스레드로 run ----
  std::vector<std::thread> pool;
  pool.reserve(threads > 0 ? threads - 1 : 0);
  for (unsigned i = 1; i < threads; ++i) {
    pool.emplace_back([&io] { io.run(); });
  }
  io.run();
  for (auto& t : pool) {
    t.join();
  }

  google::protobuf::ShutdownProtobufLibrary();
  return 0;
}
