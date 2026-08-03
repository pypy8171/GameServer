#include "core/net/session.h"

#include <cstring>
#include <iostream>
#include <utility>

#include "core/dispatch/dispatcher.h"
#include "core/packet/packet.h"

namespace game::core {

Session::Session(asio::ip::tcp::socket socket, const Dispatcher& dispatcher)
    : socket_(std::move(socket)),
      strand_(asio::make_strand(socket_.get_executor())),
      dispatcher_(dispatcher) {
  std::error_code ec;
  remote_ = socket_.remote_endpoint(ec);
  socket_.set_option(asio::ip::tcp::no_delay(true), ec);  // Nagle off: 저지연
}

void Session::Start() { ReadHeader(); }

void Session::ReadHeader() {
  recv_buf_.assign(kHeaderSize, 0);
  auto self = shared_from_this();
  asio::async_read(
      socket_, asio::buffer(recv_buf_.data(), kHeaderSize),
      asio::bind_executor(strand_, [this, self](std::error_code ec, std::size_t) {
        if (ec) {
          Close();
          return;
        }
        PacketHeader header;
        std::memcpy(&header, recv_buf_.data(), kHeaderSize);
        if (header.size < kHeaderSize || header.size > kMaxPacketSize) {
          std::cerr << "[session] invalid packet size=" << header.size << '\n';
          Close();
          return;
        }
        ReadBody(static_cast<uint16_t>(header.size - kHeaderSize));
      }));
}

void Session::ReadBody(uint16_t body_size) {
  recv_buf_.resize(kHeaderSize + body_size);
  auto self = shared_from_this();
  asio::async_read(
      socket_, asio::buffer(recv_buf_.data() + kHeaderSize, body_size),
      asio::bind_executor(strand_, [this, self](std::error_code ec, std::size_t) {
        if (ec) {
          Close();
          return;
        }
        dispatcher_.Dispatch(self, recv_buf_.data(),
                             static_cast<uint16_t>(recv_buf_.size()));
        ReadHeader();  // 다음 패킷
      }));
}

void Session::Send(std::vector<uint8_t> packet) {
  auto self = shared_from_this();
  asio::post(strand_, [this, self, p = std::move(packet)]() mutable {
    send_queue_.push_back(std::move(p));
    if (!writing_) {
      DoWrite();
    }
  });
}

void Session::DoWrite() {
  writing_ = true;
  auto self = shared_from_this();
  asio::async_write(
      socket_, asio::buffer(send_queue_.front()),
      asio::bind_executor(strand_, [this, self](std::error_code ec, std::size_t) {
        if (ec) {
          writing_ = false;
          Close();
          return;
        }
        send_queue_.pop_front();
        if (!send_queue_.empty()) {
          DoWrite();
        } else {
          writing_ = false;
        }
      }));
}

void Session::Close() {
  std::error_code ec;
  socket_.close(ec);
}

}  // namespace game::core
