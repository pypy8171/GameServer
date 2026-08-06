// chat_client — M0.5 채팅 릴레이 검증용 콘솔 클라이언트.
//   접속 → 닉네임 join → stdin 한 줄 = 한 발화. 수신은 백그라운드 스레드가 출력.
//   패킷 통신(프레이밍/방향/릴레이)을 '눈으로' 확인하는 하네스.
//
// 스레드 규율: 소켓 읽기는 io 스레드(async_read)만, 쓰기는 메인 스레드(sync write)만.
//   방향이 다르므로 full-duplex 동시 진행이 안전(각 방향은 단일 스레드).
#include <asio.hpp>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#endif

#include "chat.pb.h"
#include "core/packet/packet.h"

using namespace game::core;
using namespace game::proto;

namespace
{
// Windows 콘솔 기본 코드페이지(CP949)가 UTF-8 리터럴 바이트를 깨뜨린다.
//   소스는 /utf-8 로 UTF-8 저장 → 콘솔 입출력 CP도 UTF-8로 맞춰야 한글이 안 깨진다.
void EnableUtf8Console()
{
#if defined(_WIN32)
  SetConsoleOutputCP(CP_UTF8);
  SetConsoleCP(CP_UTF8);
#endif
}
}  // namespace

namespace
{

// 서버로부터 들어오는 프레임을 계속 읽어 출력한다.
class Reader : public std::enable_shared_from_this<Reader>
{
 public:
  explicit Reader(asio::ip::tcp::socket& socket) : socket_(socket)
  {
  }

  void Start()
  {
    ReadHeader();
  }

 private:
  void ReadHeader()
  {
    header_.assign(kHeaderSize, 0);
    auto self = shared_from_this();
    asio::async_read(socket_, asio::buffer(header_.data(), kHeaderSize),
                     [this, self](std::error_code ec, std::size_t) {
                       if (ec)
                       {
                         return;  // 연결 종료 → 읽기 루프 종료
                       }
                       // 와이어=little-endian 고정. raw memcpy 는 호스트 바이트오더에
                       //   의존 → 서버/디스패치와 동일하게 DecodeHeader 로 통일(D6/F4).
                       const PacketHeader h = DecodeHeader(header_.data());
                       if (h.size < kHeaderSize || h.size > kMaxPacketSize)
                       {
                         return;
                       }
                       ReadBody(h.id,
                                static_cast<uint16_t>(h.size - kHeaderSize));
                     });
  }

  void ReadBody(uint16_t id, uint16_t body_size)
  {
    body_.assign(body_size, 0);
    auto self = shared_from_this();
    asio::async_read(socket_, asio::buffer(body_.data(), body_size),
                     [this, self, id](std::error_code ec, std::size_t) {
                       if (ec)
                       {
                         return;
                       }
                       Handle(id, body_.data(),
                              static_cast<int>(body_.size()));
                       ReadHeader();
                     });
  }

  void Handle(uint16_t id, const uint8_t* body, int size)
  {
    switch (static_cast<PacketId>(id))
    {
      case PacketId::ChatJoinResponse: {
        ChatJoinResponse r;
        if (r.ParseFromArray(body, size))
        {
          if (r.ok())
          {
            std::cout << "* 입장 완료. 메시지를 입력하세요.\n";
          }
          else
          {
            std::cout << "* 입장 실패: " << r.reason() << '\n';
          }
        }
        break;
      }
      case PacketId::ChatNotify: {
        ChatNotify b;
        if (b.ParseFromArray(body, size))
        {
          std::cout << b.sender() << ": " << b.text() << '\n';
        }
        break;
      }
      case PacketId::SystemNotify: {
        SystemNotify s;
        if (s.ParseFromArray(body, size))
        {
          std::cout << "* " << s.text() << '\n';
        }
        break;
      }
      default:
        break;
    }
  }

  asio::ip::tcp::socket& socket_;
  std::vector<uint8_t> header_;
  std::vector<uint8_t> body_;
};

}  // namespace

int main(int argc, char** argv)
{
  EnableUtf8Console();
  GOOGLE_PROTOBUF_VERIFY_VERSION;

  const std::string port = (argc > 1) ? argv[1] : "7777";
  std::string nickname = (argc > 2) ? argv[2] : "";
  if (nickname.empty())
  {
    std::cout << "닉네임: ";
    std::getline(std::cin, nickname);
  }

  asio::io_context io;
  asio::ip::tcp::socket socket(io);
  try
  {
    asio::ip::tcp::resolver resolver(io);
    asio::connect(socket, resolver.resolve("127.0.0.1", port));
  }
  catch (const std::exception& e)
  {
    std::cerr << "연결 실패: " << e.what() << '\n';
    return 1;
  }

  // 신원 등록.
  ChatJoinRequest join;
  join.set_nickname(nickname);
  asio::write(socket, asio::buffer(MakePacket(PacketId::ChatJoinRequest, join)));

  // 수신 루프를 백그라운드 스레드에서.
  auto reader = std::make_shared<Reader>(socket);
  reader->Start();
  std::thread io_thread([&io] { io.run(); });

  // 메인 스레드: stdin 한 줄 = 한 발화.
  std::string line;
  while (std::getline(std::cin, line))
  {
    if (line.empty())
    {
      continue;
    }
    ChatSayRequest say;
    say.set_text(line);
    std::error_code ec;
    asio::write(socket, asio::buffer(MakePacket(PacketId::ChatSayRequest, say)), ec);
    if (ec)
    {
      break;
    }
  }

  socket.close();
  io.stop();
  io_thread.join();
  google::protobuf::ShutdownProtobufLibrary();
  return 0;
}
