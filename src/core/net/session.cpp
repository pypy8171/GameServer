#include "core/net/session.h"

#include <atomic>
#include <cstring>
#include <utility>

#include "core/dispatch/dispatcher.h"
#include "core/log/log.h"
#include "core/net/session_registry.h"
#include "core/packet/packet.h"

namespace game::core
{

namespace
{
// 세션 식별자 발급기. 0 은 '없음/전원' 예약값이므로 1 부터 시작.
std::atomic<SessionId> g_next_session_id{1};
}  // namespace

Session::Session(asio::ip::tcp::socket socket, const Dispatcher& dispatcher,
                 SessionRegistry& registry)
    : socket_(std::move(socket)),
      strand_(asio::make_strand(socket_.get_executor())),
      dispatcher_(dispatcher),
      registry_(registry),
      id_(g_next_session_id.fetch_add(1, std::memory_order_relaxed))
{
  std::error_code ec;
  remote_ = socket_.remote_endpoint(ec);
  socket_.set_option(asio::ip::tcp::no_delay(true), ec);  // Nagle off: 저지연
}

void Session::Start()
{
  registry_.Add(shared_from_this());
  ReadHeader();
}

bool Session::Authenticate(std::string principal)
{
  if (authenticated_)
  {
    return false;
  }
  principal_ = std::move(principal);
  authenticated_ = true;
  return true;
}

void Session::ReadHeader()
{
  recv_buf_.assign(kHeaderSize, 0);
  auto self = shared_from_this();
  asio::async_read(
      socket_, asio::buffer(recv_buf_.data(), kHeaderSize),
      asio::bind_executor(strand_, [this, self](std::error_code ec, std::size_t) {
        if (ec)
        {
          Close();
          return;
        }
        const PacketHeader header = DecodeHeader(recv_buf_.data());
        if (header.size < kHeaderSize || header.size > kMaxPacketSize)
        {
          LOG_WARN("invalid packet size={} from id={} — closing", header.size,
                   id_);
          Close();
          return;
        }
        ReadBody(static_cast<uint16_t>(header.size - kHeaderSize));
      }));
}

void Session::ReadBody(uint16_t body_size)
{
  recv_buf_.resize(kHeaderSize + body_size);
  auto self = shared_from_this();
  asio::async_read(
      socket_, asio::buffer(recv_buf_.data() + kHeaderSize, body_size),
      asio::bind_executor(strand_, [this, self](std::error_code ec, std::size_t) {
        if (ec)
        {
          Close();
          return;
        }
        dispatcher_.Dispatch(self, recv_buf_.data(),
                             static_cast<uint16_t>(recv_buf_.size()));
        ReadHeader();  // 다음 패킷
      }));
}

void Session::Send(std::vector<uint8_t> packet)
{
  auto self = shared_from_this();
  asio::post(strand_, [this, self, p = std::move(packet)]() mutable {
    if (closed_)
    {
      return;  // 종료 중이면 큐잉하지 않음
    }
    send_queue_.push_back(std::move(p));
    if (!writing_)
    {
      DoWrite();
    }
  });
}

void Session::DoWrite()
{
  writing_ = true;
  auto self = shared_from_this();
  asio::async_write(
      socket_, asio::buffer(send_queue_.front()),
      asio::bind_executor(strand_, [this, self](std::error_code ec, std::size_t) {
        if (ec)
        {
          writing_ = false;
          Close();
          return;
        }
        send_queue_.pop_front();
        if (!send_queue_.empty())
        {
          DoWrite();
        }
        else
        {
          writing_ = false;
        }
      }));
}

void Session::Close()
{
  // 멱등: 여러 에러 경로에서 불려도 정리는 1회.
  if (closed_)
  {
    return;
  }
  closed_ = true;

  registry_.Remove(id_);  // 더 이상 브로드캐스트 대상 아님
  if (on_disconnect_)
  {
    on_disconnect_(shared_from_this());  // 앱 훅: 남은 세션에 "퇴장" 알림 등
  }

  std::error_code ec;
  socket_.close(ec);
}

}  // namespace game::core
