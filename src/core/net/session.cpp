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
                 SessionRegistry& registry, std::size_t send_queue_cap_bytes,
                 const SessionPolicy& policy)
    : socket_(std::move(socket)),
      strand_(asio::make_strand(socket_.get_executor())),
      dispatcher_(dispatcher),
      registry_(registry),
      id_(g_next_session_id.fetch_add(1, std::memory_order_relaxed)),
      send_budget_(send_queue_cap_bytes),
      timer_(strand_),  // strand 실행기 → async_wait 콜백이 strand 안에서 실행
      handshake_timeout_(policy.handshake_timeout),
      idle_timeout_(policy.idle_timeout),
      rate_bucket_(policy.rate_burst, policy.rate_per_sec),
      heartbeat_timer_(strand_),
      heartbeat_interval_(policy.heartbeat_interval),
      // ms→초(double) 변환해 순수 Heartbeat 에 주입. 0 이면 Heartbeat 가 비활성.
      heartbeat_(std::chrono::duration<double>(policy.heartbeat_timeout).count())
{
  std::error_code ec;
  remote_ = socket_.remote_endpoint(ec);
  socket_.set_option(asio::ip::tcp::no_delay(true), ec);  // Nagle off: 저지연
}

namespace
{
// 단조 시계의 현재값을 초(double)로. 토큰버킷(순수)에 시각을 주입하기 위한 어댑터.
double SteadyNowSeconds()
{
  return std::chrono::duration<double>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}
}  // namespace

void Session::Start()
{
  registry_.Add(shared_from_this());
  // 미인증 상태의 절대 마감을 건다(설정 시). 인증 성공 시 유휴 마감으로 전환된다.
  ArmHandshakeDeadline();
  ReadHeader();
}

void Session::ArmHandshakeDeadline()
{
  if (handshake_timeout_.count() <= 0)
  {
    return;  // 비활성
  }
  auto self = shared_from_this();
  timer_.expires_after(handshake_timeout_);
  timer_.async_wait(
      asio::bind_executor(strand_, [this, self](std::error_code ec) {
        if (ec || closed_ || authenticated_)
        {
          return;  // 취소(재무장/종료)이거나 이미 인증 완료 — 무시
        }
        LOG_WARN("[session] 핸드셰이크 타임아웃 (id={}) — 강제 종료", id_);
        Close();
      }));
}

void Session::ArmIdleDeadline()
{
  if (idle_timeout_.count() <= 0)
  {
    return;  // 비활성
  }
  auto self = shared_from_this();
  timer_.expires_after(idle_timeout_);
  timer_.async_wait(
      asio::bind_executor(strand_, [this, self](std::error_code ec) {
        if (ec || closed_)
        {
          return;  // 취소(활동으로 리셋/종료) — 무시
        }
        LOG_WARN("[session] 유휴 타임아웃 (id={}) — 강제 종료", id_);
        Close();
      }));
}

bool Session::Authenticate(std::string principal)
{
  if (authenticated_)
  {
    return false;
  }
  principal_ = std::move(principal);
  authenticated_ = true;
  // 핸드셰이크 마감 → 유휴 마감으로 전환. expires_after 가 미인증 대기를 취소한다.
  //   (Authenticate 는 dispatch 경로에서 strand 안에서만 호출된다.)
  ArmIdleDeadline();
  ArmHeartbeat();  // 인증 후에만 생존성 프로브 시작(미인증은 핸드셰이크 마감이 담당)
  return true;
}

void Session::ArmHeartbeat()
{
  if (heartbeat_interval_.count() <= 0 || !heartbeat_.enabled())
  {
    return;  // 비활성(주기 또는 timeout 미설정)
  }
  // 정상 운영 전제: interval < timeout. 역이면 첫 Ping 을 쏘기도 전에 timeout 초과로
  //   전 세션이 조용히 끊긴다(Classify 는 발신 전에 판정, any-recv 기준). 오설정을
  //   무음 실패시키지 않고 경고만 남긴다(계약 위반 조기 발견 — 동작은 정책값 존중).
  const double interval_sec =
      std::chrono::duration<double>(heartbeat_interval_).count();
  if (interval_sec >= heartbeat_.timeout_sec())
  {
    LOG_WARN(
        "[session] heartbeat 오설정: interval({}ms) >= timeout({}ms) — 프로브 전 "
        "종료 위험 (id={})",
        heartbeat_interval_.count(),
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::duration<double>(heartbeat_.timeout_sec()))
            .count(),
        id_);
  }
  heartbeat_.OnActivity(SteadyNowSeconds());  // 인증 시점을 활동 기준으로
  ScheduleHeartbeat();
}

void Session::ScheduleHeartbeat()
{
  auto self = shared_from_this();
  heartbeat_timer_.expires_after(heartbeat_interval_);
  heartbeat_timer_.async_wait(
      asio::bind_executor(strand_, [this, self](std::error_code ec) {
        if (ec || closed_)
        {
          return;  // 취소(종료) — 무시
        }
        // 마지막 수신 이후 경과가 timeout 을 넘었으면 죽은 세션 → 강제 종료.
        if (heartbeat_.Classify(SteadyNowSeconds()) == HeartbeatAction::Close)
        {
          LOG_WARN("[session] heartbeat 무응답 (id={}) — 강제 종료", id_);
          Close();
          return;
        }
        // 살아있음 → 능동 Ping(유휴 클라 프로브 + NAT keepalive) 후 재무장.
        Send(MakeControlPacket(PacketId::HeartbeatPing));
        ScheduleHeartbeat();
      }));
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
        // 인바운드 rate-limit(S-4): 완성된 패킷 1개당 토큰 1개. 부족하면 플러딩으로
        //   간주해 강제 종료(백프레셔와 같은 "남용은 종료" 규율). 비활성 시 항상 통과.
        if (rate_bucket_.enabled() && !rate_bucket_.TryConsume(SteadyNowSeconds()))
        {
          LOG_WARN("[session] 인바운드 rate-limit 초과 (id={}) — 강제 종료", id_);
          Close();
          return;
        }
        // 수신 활동 = 살아있음. 인증된 세션의 유휴 마감을 리셋한다(미인증은 핸드셰이크
        //   절대 마감 유지 — 미인증 트래픽으로 연장 불가, 슬로로리스 방어).
        if (authenticated_)
        {
          ArmIdleDeadline();
          heartbeat_.OnActivity(SteadyNowSeconds());  // any-recv 가 생존 데드라인 리셋
        }
        // heartbeat Pong 은 코어 제어 프레임 — read 경로에서 가로채 디스패처를 거치지
        //   않는다(위 OnActivity 로 생존 신호는 이미 반영). 디스패처에 넘기면 미등록
        //   핸들러로 unknown-drop 경고만 남으므로 여기서 소비하고 다음 패킷으로.
        const uint16_t pid = DecodeHeader(recv_buf_.data()).id;
        if (pid == static_cast<uint16_t>(PacketId::HeartbeatPong))
        {
          ReadHeader();
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
    // 백프레셔(ADR-E): 큐 누적 바이트가 상한을 넘기면 이 프레임을 수용하지 않고
    //   세션을 강제 종료한다. 초과분 drop 이 아니라 종료인 이유 — 게임 패킷을
    //   무음 유실하면 클라 상태가 어긋난다. 느린/악성 수신자 1명의 무한 큐 성장이
    //   서버 전체를 OOM 으로 죽이는 걸 막는 하드 안전망(H5 종료 경로로 수렴).
    if (!send_budget_.TryAdmit(p.size()))
    {
      LOG_WARN("[session] send 큐 상한 초과 (id={}, cap={}B, queued={}B) — 강제 종료",
               id_, send_budget_.cap(), send_budget_.queued());
      Close();
      return;
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
        // 전송 완료분을 예산에서 회수한 뒤 큐에서 뺀다(회수→pop 순서: OnSent 가
        //   front 크기를 읽으므로 pop 전에 호출). 백프레셔 예산이 다시 열린다.
        send_budget_.OnSent(send_queue_.front().size());
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

  // send_queue_ 를 여기서 비우지 않는다: in-flight async_write 가 send_queue_.front()
  //   을 asio::buffer 로 참조 중일 수 있어, 조기 clear/pop 은 UAF 다. socket_.close()
  //   가 그 write 를 취소(operation_aborted)시키고, 완료 핸들러가 self 를 쥔 채 돌아
  //   나간 뒤 세션 파괴 시 큐가 정상 소멸한다. 백프레셔 강제종료도 이 경로로 수렴.
  registry_.Remove(id_);  // 더 이상 브로드캐스트 대상 아님
  if (on_disconnect_)
  {
    on_disconnect_(shared_from_this());  // 앱 훅: 남은 세션에 "퇴장" 알림 등
  }

  std::error_code ec;
  timer_.cancel(ec);           // 미결 타임아웃 대기 취소 → self 캡처 해제(세션 조기 회수)
  heartbeat_timer_.cancel(ec);  // 생존성 대기도 취소 — 안 하면 다음 틱까지 self 잔류
  socket_.close(ec);
}

}  // namespace game::core
