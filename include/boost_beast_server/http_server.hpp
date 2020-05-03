#ifndef BOOST_BEAST_SERVER_HTTP_SERVER_HPP_
#define BOOST_BEAST_SERVER_HTTP_SERVER_HPP_
#ifndef _WIN32_WINNT
#   define _WIN32_WINNT 0x0601
#endif
#include <functional>
#include <regex>
#include <type_traits>
#include <unordered_map>
#include <boost/beast.hpp>
#include <boost/asio.hpp>
namespace server {
namespace beast = boost::beast;         // from <boost/beast.hpp>
namespace http = beast::http;           // from <boost/beast/http.hpp>
namespace net = boost::asio;            // from <boost/asio.hpp>
using tcp = boost::asio::ip::tcp;       // from <boost/asio/ip/tcp.hpp>
using request = http::request<http::string_body>;
using net::io_context;
class session_send;
namespace detail {
    using strict_match_registerd_events_t = std::vector<std::unordered_map<std::string, std::function<void(session_send&, request&&)>>>;
    using regex_match_registerd_events_t = std::vector<std::vector<std::pair<std::regex, std::function<void(session_send&, request&&, const std::smatch&)>>>>;
    struct registerd_events_t {
        strict_match_registerd_events_t strict_match;
        regex_match_registerd_events_t regex_match;
    };
    class session : public std::enable_shared_from_this<session>
    {
    public:
        std::shared_ptr<void> res_;
        beast::tcp_stream stream_;
    private:
        beast::flat_buffer buffer_;
        http::request<http::string_body> req_;
        std::shared_ptr<registerd_events_t> registerd_events;

    public:
        // Take ownership of the stream
        session(
            tcp::socket&& socket,
            const std::shared_ptr<registerd_events_t>& registerd_events
        );

        // Start the asynchronous operation
        void run();

        void do_read();

        void on_read(beast::error_code ec, std::size_t bytes_transferred);
        void handle_request();

        void on_write(bool close, beast::error_code ec, std::size_t bytes_transferred);

        void do_close();
    };
    template<bool isRequest, class Body, class Fields>
    void send(detail::session& self, http::message<isRequest, Body, Fields>&& msg)
    {
        // The lifetime of the message has to extend
        // for the duration of the async operation so
        // we use a shared_ptr to manage it.
        auto sp = std::make_shared<http::message<isRequest, Body, Fields>>(std::move(msg));

        // Store a type-erased version of the shared
        // pointer in the class to keep it alive.
        self.res_ = sp;

        // Write the response
        http::async_write(
            self.stream_,
            *sp,
            beast::bind_front_handler(&session::on_write, self.shared_from_this(), sp->need_eof())
        );
    }
    template<typename T>
    struct is_message : std::false_type {};
    template<bool isRequest, class Body, class Fields>
    struct is_message<http::message<isRequest, Body, Fields>> : std::true_type {};
    template<typename T>
    inline constexpr bool is_message_v = is_message<T>::value;

    template<bool con, typename F>
    struct is_strict_match_request_handler_impl : std::false_type {};
    template<typename F>
    struct is_strict_match_request_handler_impl<true, F> : is_message<std::invoke_result_t<F, request&&>> {};
    template<typename F>
    struct is_strict_match_request_handler : is_strict_match_request_handler_impl<
        std::is_invocable_v<F, request&&>, F
    > {};
    template<typename F>
    inline constexpr bool is_strict_match_request_handler_v = is_strict_match_request_handler<F>::value;

    template<bool con, typename F>
    struct is_regex_match_request_handler_impl : std::false_type {};
    template<typename F>
    struct is_regex_match_request_handler_impl<true, F> : is_message<std::invoke_result_t<F, request&&, const std::smatch&>> {};
    template<typename F>
    struct is_regex_match_request_handler : is_regex_match_request_handler_impl<
        std::is_invocable_v<F, request&&, const std::smatch&>, F
    > {};
    template<typename F>
    inline constexpr bool is_regex_match_request_handler_v = is_regex_match_request_handler<F>::value;
}
class session_send {
private:
    detail::session& self;
public:
    session_send(detail::session& self) : self(self) {}
    session_send(const session_send&) = delete;
    session_send(session_send&&) = delete;
    template<bool isRequest, class Body, class Fields>
    void send(http::message<isRequest, Body, Fields>&& msg)
    {
        detail::send(self, std::move(msg));
    }
};

void run_io_service(io_context& ioc, const unsigned int thread_num);
http::response<http::string_body> create_text_responce(
    const request& req, http::status status, beast::string_param content_type, std::string body
);
// Accepts incoming connections and launches the sessions
class listener : public std::enable_shared_from_this<listener>
{
private:
    io_context& ioc_;
    tcp::acceptor acceptor_;
    bool is_running;
    std::shared_ptr<detail::registerd_events_t> registerd_events;
    inline static constexpr std::size_t http_verb_max = static_cast<std::size_t>(http::verb::unlink);
public:
    static std::shared_ptr<listener> create(io_context& ioc, tcp::endpoint endpoint);
    // Start accepting incoming connections
    void run();
    bool isAbleToRegister() const noexcept { return this->is_running; }
    template<
        typename F,
        std::enable_if_t<detail::is_strict_match_request_handler_v<F>, std::nullptr_t> = nullptr
    >
    void addRequestListener(http::verb method, std::string path, F&& f)
    {
        this->canRegister();
        const auto me = std::size_t(method);
        auto& ev = this->registerd_events->strict_match;
        if (ev.size() <= me) {
            ev.resize(me + 1);
        }
        ev[me].emplace(std::move(path), [f = std::forward<F>(f)](session_send& s, request&& req) {
            return s.send(f(std::move(req)));
        });
    }
    template<
        typename F,
        std::enable_if_t<detail::is_regex_match_request_handler_v<F>, std::nullptr_t> = nullptr
    >
    void addRequestListener(http::verb method, std::regex path, F&& f)
    {
        this->canRegister();
        const auto me = std::size_t(method);
        auto& ev = this->registerd_events->regex_match;
        if (ev.size() <= me) {
            ev.resize(me + 1);
        }
        ev[me].emplace_back(std::move(path), [f = std::forward<F>(f)](session_send& s, request&& req, const std::smatch& m) {
            return s.send(f(std::move(req), m));
        });
    }
    template<
        typename F,
        std::enable_if_t<detail::is_strict_match_request_handler_v<F>, std::nullptr_t> = nullptr
    >
    void get(std::string path, F&& f)
    {
        this->addRequestListener(http::verb::get, std::move(path), std::forward<F>(f));
    }
    template<
        typename F,
        std::enable_if_t<detail::is_regex_match_request_handler_v<F>, std::nullptr_t> = nullptr
    >
    void get(std::regex path, F&& f)
    {
        this->addRequestListener(http::verb::get, std::move(path), std::forward<F>(f));
    }
    template<
        typename F,
        std::enable_if_t<detail::is_strict_match_request_handler_v<F>, std::nullptr_t> = nullptr
    >
    void post(std::string path, F&& f)
    {
        this->addRequestListener(http::verb::post, std::move(path), std::forward<F>(f));
    }
    template<
        typename F,
        std::enable_if_t<detail::is_regex_match_request_handler_v<F>, std::nullptr_t> = nullptr
    >
    void post(std::regex path, F&& f)
    {
        this->addRequestListener(http::verb::post, std::move(path), std::forward<F>(f));
    }
private:
    listener(io_context& ioc, tcp::endpoint endpoint);
    void canRegister() const;
    void do_accept();
    void on_accept(beast::error_code ec, tcp::socket socket);
};
}
#endif
