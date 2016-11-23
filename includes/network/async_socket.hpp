#pragma once


#include <sys/socket.h>
#include <sys/fcntl.h> // fcntl
#include <unistd.h> // close
#include <arpa/inet.h>

#include <string>

namespace io {
  namespace network
  {
    using socket_t = struct sockaddr;
    using std::string;

    class socket_excetion : std::exception {};
    //TODO: NAMING? More of a permission socket issue than a connect one!
    class connect_socket_exception : socket_excetion {};
    class nonblocking_socket_exception : socket_excetion {};

    template <typename InputOutputHanler>
    class async_socket
    {
    public:
      using socket_identifier_t  = typename InputOutputHanler::socket_identifier_t;
      using recv_cb_t         = std::function<void (ssize_t)>;
      using ready_cb_t        = std::function<void (ssize_t)>;
      using connect_handler_t = std::function<void (bool)>;

      async_socket(InputOutputHanler& io)
        : io_(io)
      { }

      async_socket(InputOutputHanler &io, int fd)
        : io_(io)
      {
        fd_ = fd;
        is_connected_ = true;

        id_ = io_.watch(fd_);
      }

      ~async_socket() {
        if (is_connected_)
          close();
      }

      inline ssize_t send(const string& data) {
        return ::send(fd_, data.data(), data.size(), 0);
      }

      inline ssize_t send(const char *data, size_t len) {
        return ::send(fd_, data, len, 0);
      }

      inline ssize_t receive(char *data, size_t len) {
        return ::recv(fd_, data, len, 0);
      }

      inline bool listen() {
        return ::listen(fd_, 0) == 0;
      }

      inline int accept() {
        return ::accept(fd_, nullptr, nullptr);
      }

      bool close() {
        io_.unwatch(id_);
        auto res = ::close(fd_) == 0;
        is_connected_ = false;
        fd_ = -1;
        return res;
      }

      void async_write(const string& data, const ready_cb_t& cb)
      {
        if (!is_connected())
          return;

        return io_.async_write(id_, [this, data, cb]() {
            cb(send(data));
          });
      }

      void async_read(char *buffer, int max_len, const recv_cb_t& cb)
      {
        if (!is_connected())
          return;

        return io_.async_read(id_, [&, buffer, max_len,  cb]() {
            auto l = receive(buffer, max_len);

            if (l == 0)
              this->close();

            cb(l);
          });
      }

      template <typename SocketType, typename... Args>
      void async_connect(int timeout, async_socket::connect_handler_t handler, Args... args)
      {
        if (timeout == 10) // is equal to 1 second
          return handler(false);

        io_.async_timeout(0.1, [this, timeout, args..., handler]() {

            if (-1 == static_cast<SocketType&>(*this).connect(args...))
              return this->async_connect<SocketType>(timeout+1, handler, args...);

            handler(is_connected());
          });
      }

      template<typename SocketType>
      void async_accept(const std::function<void(std::shared_ptr<SocketType>)>& cb)
      {
        return io_.async_read(id_, [&, cb]() {
            int fd = this->accept();
            cb(std::make_shared<SocketType>(io_, fd));
            this->async_accept(cb);
          });
      }

      inline
      bool is_connected() const {
        return is_connected_;
      }

    protected:
      void create_socket(int domain) {
        if (-1 == (fd_ = socket(domain, SOCK_STREAM, 0)))
          throw connect_socket_exception();

        if (-1 == fcntl(fd_, F_SETFL, fcntl(fd_, F_GETFL) | O_NONBLOCK))
          throw nonblocking_socket_exception();

        id_ = io_.watch(fd_);
      }

      int connect_to(socket_t* socket_addr, int len) {
        int ret = ::connect(fd_, socket_addr, len);
        if (!ret)
          is_connected_ = true;

        return ret;
      }

      int bind_to(socket_t* socket_addr, int len) {
        return ::bind(fd_, socket_addr, len);
      }

    private:
      bool is_connected_ = false;
      InputOutputHanler& io_;
      socket_identifier_t id_ = nullptr;
      int fd_ = -1;
    };
  }
}
