// 채팅 릴레이 종단간(loopback) 통합 테스트.
//   실제 Server + 2개의 TCP 클라 소켓으로 join → 입장알림 → 발화 릴레이를 검증한다.
//   copy-per-recipient 팬아웃·세션 수명주기·방향 규율이 실제 소켓 위에서 도는지 확인.
#include <gtest/gtest.h>

#include <asio.hpp>
#include <cstdint>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "chat.pb.h"
#include "chat_server/chat_handlers.h"
#include "core/dispatch/dispatcher.h"
#include "core/net/server.h"
#include "core/net/session_registry.h"
#include "core/packet/packet.h"

using namespace game::core;
using namespace game::proto;
using namespace game::chat;
using asio::ip::tcp;

namespace {

constexpr uint16_t TestPort = 39217;

struct Frame {
  uint16_t id;
  std::vector<uint8_t> body;
};

// 소켓에서 한 프레임([헤더+바디])을 동기적으로 읽는다.
Frame ReadFrame(tcp::socket& s) {
  std::vector<uint8_t> hdr(kHeaderSize);
  asio::read(s, asio::buffer(hdr.data(), kHeaderSize));
  PacketHeader h;
  std::memcpy(&h, hdr.data(), kHeaderSize);
  std::vector<uint8_t> body(h.size - kHeaderSize);
  if (!body.empty()) {
    asio::read(s, asio::buffer(body.data(), body.size()));
  }
  return Frame{h.id, std::move(body)};
}

tcp::socket Connect(asio::io_context& io) {
  tcp::socket sock(io);
  tcp::resolver resolver(io);
  asio::connect(sock, resolver.resolve("127.0.0.1", std::to_string(TestPort)));
  return sock;
}

void SendJoin(tcp::socket& s, const std::string& nick) {
  ChatJoinRequest join;
  join.set_nickname(nick);
  asio::write(s, asio::buffer(MakePacket(PacketId::ChatJoinRequest, join)));
}

}  // namespace

TEST(ChatRelay, JoinAnnouncesAndSayRelaysToAll) {
  // ---- 서버: 별도 io_context + 스레드 ----
  asio::io_context server_io;
  SessionRegistry registry;
  Dispatcher dispatcher;
  RegisterChatHandlers(dispatcher, registry);
  Server server(server_io, TestPort, dispatcher, registry);
  server.set_on_disconnect(MakeDisconnectHook(registry));
  server.Start();
  std::thread server_thread([&server_io] { server_io.run(); });

  // ---- 클라이언트: 동기 소켓 ----
  asio::io_context cio;

  // alice join → JoinResult(ok)
  tcp::socket alice = Connect(cio);
  SendJoin(alice, "alice");
  Frame f = ReadFrame(alice);
  EXPECT_EQ(f.id, static_cast<uint16_t>(PacketId::ChatJoinResponse));
  {
    ChatJoinResponse r;
    ASSERT_TRUE(r.ParseFromArray(f.body.data(), static_cast<int>(f.body.size())));
    EXPECT_TRUE(r.ok());
  }

  // bob join → bob 은 JoinResult, alice 는 입장 시스템 알림을 받는다.
  tcp::socket bob = Connect(cio);
  SendJoin(bob, "bob");
  f = ReadFrame(bob);
  EXPECT_EQ(f.id, static_cast<uint16_t>(PacketId::ChatJoinResponse));

  f = ReadFrame(alice);  // alice: "bob 입장"
  EXPECT_EQ(f.id, static_cast<uint16_t>(PacketId::SystemNotify));
  {
    SystemNotify sys;
    ASSERT_TRUE(sys.ParseFromArray(f.body.data(),
                                   static_cast<int>(f.body.size())));
    EXPECT_NE(sys.text().find("bob"), std::string::npos);
  }

  // alice 발화 → alice·bob 모두 ChatNotify(sender=alice) 수신.
  ChatSayRequest say;
  say.set_text("hello");
  asio::write(alice, asio::buffer(MakePacket(PacketId::ChatSayRequest, say)));

  for (tcp::socket* who : {&alice, &bob}) {
    Frame b = ReadFrame(*who);
    EXPECT_EQ(b.id, static_cast<uint16_t>(PacketId::ChatNotify));
    ChatNotify bc;
    ASSERT_TRUE(bc.ParseFromArray(b.body.data(), static_cast<int>(b.body.size())));
    EXPECT_EQ(bc.sender(), "alice");  // 발신자는 서버가 세션에서 채움
    EXPECT_EQ(bc.text(), "hello");
  }

  // ---- 정리 ----
  alice.close();
  bob.close();
  server_io.stop();
  server_thread.join();
}
