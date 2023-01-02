#include <iostream>
#include <string>
#include <array>
#include <cstddef>
#include <memory>
#include <thread>
#include <vector>
#include <deque>
#include <functional>
#include "boost/asio.hpp"

namespace asio = boost::asio;
using boost::asio::ip::tcp;

struct Led;

struct LedCommand {
    const char * const command;
    std::function<std::string (Led&, std::string const&)> action;
};

struct LedCommands {
    explicit LedCommands (std::initializer_list<LedCommand> cmd_list) 
        : m_commands{ cmd_list } {}

    std::string parse (Led & led, std::string const& msg) {
        for (auto const& [cmd, action] : m_commands) {
            //std::cerr << "cmd|recv: " << cmd << "|" << msg << "\n";
            const auto at = msg.find(cmd);
            if (at != 0) continue;
            auto from = std::strlen(cmd);
            if (from >= msg.size()) return action(led, "");
            auto arg = msg.substr(from + 1);
            //std::cerr << "[arg: " << arg << "] ";
            return action(led, arg);
        }
        return "FAILED";
    }
private:
    const std::vector<LedCommand> m_commands;
};

struct Led {
    enum class State : bool { on = true, off = false };
    enum class Color : unsigned char { red, green, blue };

    template <typename T, T Min, T Max>
    struct Range { static_assert(Min < Max);
        using value_type = T;

        explicit Range (T value) : num{value} {
            if (!set(value)) { throw std::range_error {"expected a value in range Min..Max"}; }
        }

        bool set (T value) {
            if (Min <= value && value <= Max) {
                num = value;
                return true;
            }
            return false;
        }

        T get() const {
            return num;
        }

    private:
        T num;
    };

    Led() 
    : m_state{ State::off }
    , m_color{ Color::green }
    , m_rate{ 0 }
    {}

    auto get_state () const {
        return m_state;
    }

    bool set_state (std::string const& sstate) {
        //std::cerr << "set_state " << sstate << "\n";
        if (sstate == "on") {
            m_state = State::on;
            return true;
        } else if (sstate == "off") {
            m_state = State::off;
            return true;
        } 
        return false;
    }

    auto get_color () const {
        return m_color;
    }

    bool set_color (std::string const& scolor) {
        //std::cerr << "set_color " << scolor << "\n";
        if (scolor == "red") {
            m_color = Color::red;
            return true;
        } else if (scolor == "green") {
            m_color = Color::green;
            return true;
        } else if (scolor == "blue") {
            m_color = Color::blue;
            return true;
        }
        return false;
    }

    auto get_rate () const {
        return m_rate.get();
    }

    bool set_rate (std::string const& srate) {
        //std::cerr << "set_rate " << srate << "\n";
        const auto rate = std::stol(srate);
        return m_rate.set( rate );
    }

    friend std::ostream& operator<< (std::ostream& os, Led const& light) {
        os << "[" << (light.get_state() == Led::State::on ? '*' : ' ') << "] ";
        if (light.get_state() == Led::State::off) {
            return os;
        }
        switch(light.get_color()) {
            case Led::Color::red   : os << "red"; break;
            case Led::Color::green : os << "green"; break;
            case Led::Color::blue  : os << "blue"; break;
        }
        os << " " << light.get_rate();
        for (std::size_t i=0; i<light.get_rate(); ++i) os << ".";
        // os << "\n";
        return os;
    }

    using Rate = Range<std::size_t, 0,5>;

private:
    State m_state;
    Color m_color;
    Rate m_rate;
};


struct LedHandler : std::enable_shared_from_this<LedHandler> {
    static constexpr std::size_t ReadBufferSize = 128;

    LedHandler (asio::io_service & io, Led & light, LedCommands & cmd_list) 
    : m_io{ io }
    , m_write_strand{ io }
    , m_socket{ io }
    , m_light{ light }
    , m_commands{ cmd_list }
    {}

    ~LedHandler() {
        // std::cerr << "~Handler\n";
        m_socket.close();
    }

    tcp::socket& socket () {
        return m_socket;
    }

    void start () {
        read_some();
    }

    void send (std::string message) {
        m_io.post( 
            m_write_strand.wrap(
                [self=shared_from_this(), message=std::move(message)] {
                    self->queue_message( std::move(message) );
                }
            )
        );
    }

protected:
    void read_some () {
        // std::cerr << "READING...\n";
        m_socket.async_read_some(
            asio::buffer(m_input),
            [self=shared_from_this()]
            (boost::system::error_code const& err, std::size_t bytes_transferred) {
                self->read_finished(err, bytes_transferred);
            }
        );
    }

    void read_finished (boost::system::error_code const& error, std::size_t bytes_transferred) { 
        if (error) return;

        std::string message (&m_input[0], &m_input[bytes_transferred]);
        m_message += message;

        if (message.find('\n') == std::string::npos) {
            // std::cerr << "...received part...: " << message << "\n";
            read_some();
        } else {
            while (!m_message.empty()) {
                auto newline = m_message.find('\n');
                auto cmd = m_message.substr(0, newline);
                m_message = m_message.substr(newline+1);
                // std::cerr << "SERVER RECEIVED: \"" << cmd << "\"\n";
                auto res = m_commands.parse( m_light, cmd );
                send(res+"\n");
            }
        }
    }

    void queue_message (std::string message) {
        bool write_in_progress = !m_send_queue.empty();
        m_send_queue.push_back( std::move(message) );
        if (!write_in_progress) {
            initiate_send();
        }
    }

    void initiate_send () {
        // std::cerr<< "SENDING...";
        m_send_queue.front() += "\0";
        async_write( 
            m_socket, 
            asio::buffer(m_send_queue.front()),
            m_write_strand.wrap(
                [self=shared_from_this()]
                (boost::system::error_code const& error, std::size_t) {
                    self->send_done(error);
                }
            )
        );
    }

    void send_done (boost::system::error_code const& error) {
        if (!error) {
            m_send_queue.pop_front();
            if (!m_send_queue.empty()) { initiate_send(); }
            else {
                // std::cerr << "SENDING DONE.\n";
                return;
            }
        }
    }

private:
    asio::io_service & m_io;
    asio::io_service::strand m_write_strand;
    tcp::socket m_socket;
    std::array<char, ReadBufferSize> m_input;
    std::deque<std::string> m_send_queue;
    std::string m_message;
    Led & m_light;
    LedCommands & m_commands;
};


struct worker_threads {
    worker_threads (std::size_t n_threads=std::thread::hardware_concurrency()) 
    : m_threads_count{ n_threads }
    { m_pool.reserve(m_threads_count); }

    template <typename F>
    void emplace (F && f) {
        m_pool.emplace_back(std::forward<F>(f));
    }

    std::size_t n_workers() const {
        return m_threads_count;
    }

    ~worker_threads() {
        for (auto&& worker : m_pool) {
            if (worker.joinable()) worker.join();
        }
    }

private:
    std::size_t m_threads_count;
    std::vector<std::thread> m_pool;
};


template <typename ConnectionHandler>
struct Server {
    using SharedHandler = std::shared_ptr<ConnectionHandler>;

    explicit Server ( asio::io_service & io
                    , asio::ip::port_type port
                    , Led & light
                    , LedCommands & cmd_list
                    , std::size_t n_threads=std::thread::hardware_concurrency()
    ) 
    : m_io{ io } 
    , m_acceptor{ m_io, tcp::endpoint(tcp::v4(), port) } 
    , m_workers{ n_threads }
    , m_light{ light }
    , m_commands{ cmd_list }
    {
        accept_new();
    }

    void accept_new () {
        auto handler = std::make_shared<ConnectionHandler>(m_io, m_light, m_commands);

        m_acceptor.async_accept( handler->socket(),
            [this, handler] (auto err) {
                handle_new_connection(handler, err);
            }
        );

        for (std::size_t i=0; i < m_workers.n_workers(); ++i) {
            m_workers.emplace( [this]{ m_io.run(); } );
        }
    }

private:
    void handle_new_connection( SharedHandler handler
                              , boost::system::error_code const& error) 
    {
        if (error) {
            std::cerr << "!ERR: " << error.what();
            //return; 
        }
        handler->start();
        accept_new();
    }

    asio::io_service & m_io;
    tcp::acceptor m_acceptor;
    worker_threads m_workers;
    Led & m_light;
    LedCommands & m_commands;
};

std::string encode (bool flag) {
    return flag ? "OK" : "FAILED";
}

std::string encode () {
    return "OK";
}

std::string encode (Led::State state) {
    return std::string("OK ") + (state == Led::State::on ? "on" : "off");
}

std::string encode (Led::Color color) {
    switch (color) {
        case Led::Color::red   : return "OK red";
        case Led::Color::green : return "OK green";
        case Led::Color::blue  : return "OK blue";
    }
}

std::string encode (typename Led::Rate::value_type rate) {
    return "OK " + std::to_string(rate);
}


static LedCommands list {
    {"set-led-state", [](Led & led, std::string const& arg) { return encode( led.set_state(arg) ); }},
    {"get-led-state", [](Led & led, std::string const&)     { return encode( led.get_state() ); }},
    {"set-led-color", [](Led & led, std::string const& arg) { return encode( led.set_color(arg) ); }},
    {"get-led-color", [](Led & led, std::string const&)     { return encode( led.get_color() ); }},
    {"set-led-rate",  [](Led & led, std::string const& arg) { return encode( led.set_rate(arg) ); }},
    {"get-led-rate",  [](Led & led, std::string const&)     { return encode( led.get_rate() ); }}
};

static Led light;

int main() {
    asio::io_service io;
    Server<LedHandler> server {io, 1234, light, list};
    std::thread light_monitor ([] {
        for (;;) { std::cerr << "             \r" << light; sleep(1); }
    });
    light_monitor.join();
}