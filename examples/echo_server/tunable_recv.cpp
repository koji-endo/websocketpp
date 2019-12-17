// thread数を可変にするところはhttps://qiita.com/nsnonsugar/items/501fb9e5195a6a04f98bを参考にした。

#include "cxxopts.hpp"
#include <arpa/inet.h> //htons(), inet_addr()
#include <iomanip>
#include <iostream>
#include <mutex>
#include <string>
#include <sys/socket.h> //sendto(), socket()
#include <thread>
#include <time.h>
#include <unistd.h> //close()
#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>

typedef websocketpp::server<websocketpp::config::asio> server;

using websocketpp::lib::bind;
using websocketpp::lib::placeholders::_1;
using websocketpp::lib::placeholders::_2;

// pull out the type of messages sent by our config
typedef server::message_ptr message_ptr;

using namespace std;

mutex mtx;
void echo_server();
void udprecv(FILE *fp, const char *address, int port, int buffersize,
             int count);
void on_message(server *s, websocketpp::connection_hdl hdl, message_ptr msg);

int main(int argc, char **argv) {
  //引数を処理するための部分
  cxxopts::Options options("cxxopts_test");
  int buffersize = 1440;
  int count = 1000;
  int port;
  unsigned int num_thread = 10;
  string adst;
  string protocol = "udp";
  string filename;
  try {
    string to;
    options.add_options()("to", "address like 192.168.2.100:60000",
                          cxxopts::value<string>(to))(
        "f,file", "file input name", cxxopts::value<string>(filename))(
        "b,buffer", "buffersize default:1440", cxxopts::value<int>(buffersize))(
        "c,count", "count:1000", cxxopts::value<int>(count))(
        "p,protocol", "default udp", cxxopts::value<string>(protocol))(
        "n,num", "num of thread 10",
        cxxopts::value<unsigned int>(num_thread))("h,help", "Print help");
    options.parse_positional({"file", "to"});

    auto result = options.parse(argc, argv);

    if (result.count("help")) {
      cout << options.help({}) << endl;
      return 0;
    }
    port = stoi(to.substr(to.find_first_of(":", 0) + 1));
    adst = to.substr(0, to.find_first_of(":", 0));
  } catch (cxxopts::OptionException &e) {
    cout << options.usage() << endl;
    cerr << e.what() << endl;
  }

  const char *address = adst.c_str();
  cout << "target address: " << address << endl;
  cout << "target port: " << port << endl;
  cout << "buffer size: " << buffersize << endl;
  cout << "count: " << count << endl;
  struct timespec timeReg[2];

  // Create a server endpoint

  FILE *fp;
  const char *filenamech = filename.c_str();
  fp = fopen(filenamech, "rb");

  clock_gettime(CLOCK_REALTIME, &(timeReg[0]));

  vector<thread> threads;
  threads.reserve(num_thread);
  // スレッドを生成して実行開始
  cout << "start threads (number: " << num_thread << ")" << endl;

  for (size_t i = 0; i < num_thread - 1; ++i) {
    if (protocol == "udp") {
      threads.emplace_back(
          thread(udprecv, fp, address, port, buffersize, count));
    }
  }

  threads.emplace_back(thread(echo_server));
  for (auto &thread : threads) {
    thread.join();
  }

  clock_gettime(CLOCK_REALTIME, &(timeReg[1]));
  fclose(fp);
  double dur = ((timeReg[1].tv_sec) - (timeReg[0].tv_sec)) * 1e3 +
               ((timeReg[1].tv_nsec) - (timeReg[0].tv_nsec)) * 1e-6;
  cout << fixed << setprecision(3) << dur << "ms" << endl;
  cout << buffersize * count / dur * 0.001 << "[MB/s]" << endl;
  return 0;
}

void udprecv(FILE *fp, const char *address, int port, int buffersize,
             int count) {

  // struct timespec waitTime;
  // waitTime.tv_sec = 0;
  // waitTime.tv_nsec = 1e8;
  struct sockaddr_in addr;
  int sock_df;
  sock_df = socket(AF_INET, SOCK_DGRAM, 0);
  if (sock_df < 0) {
    perror("Couldn't make a socket");
  }
  addr.sin_family = AF_INET;                 // IPv4を指定
  addr.sin_port = htons(port);               //ポート番号
  addr.sin_addr.s_addr = inet_addr(address); //サーバー側のアドレス
  char rebuf[buffersize];
  bind(sock_df, (struct sockaddr *)&addr, sizeof(addr));
  for (int c_i = 0; c_i < count; c_i++) {
    // mtx.lock();
    // fread(rebuf, 1, buffersize, fp);
    // mtx.unlock();
    memset(rebuf, 0, sizeof(rebuf));
    int recnum = recv(sock_df, rebuf, sizeof(rebuf), 0);
    if (recnum < 0) {
      perror("recv error");
    } else {
      cout << c_i << ":" << recnum << "\t";
    }
    struct timespec *ti = (struct timespec *)(rebuf + 4);
    cout << (*ti).tv_sec << " " << (*ti).tv_nsec << "\t";
    cout << *((int *)rebuf) << endl;
    // clock_nanosleep(CLOCK_REALTIME, 0, &waitTime, NULL);
  }
  close(sock_df);
}

// Define a callback to handle incoming messages
void on_message(server *s, websocketpp::connection_hdl hdl, message_ptr msg) {
  std::cout << "on_message called with hdl: " << hdl.lock().get()
            << " and message: " << msg->get_payload() << std::endl;

  // check for a special command to instruct the server to stop listening so
  // it can be cleanly exited.
  if (msg->get_payload() == "stop-listening") {
    s->stop_listening();
    return;
  }

  try {
    // s->send(hdl, msg->get_payload(), msg->get_opcode());
    s->send(hdl, "msg->get_payload()", msg->get_opcode());
  } catch (websocketpp::exception const &e) {
    std::cout << "Echo failed because: "
              << "(" << e.what() << ")" << std::endl;
  }
}

void echo_server() {
  server echo_server;

  try {
    // Set logging settings
    echo_server.set_access_channels(websocketpp::log::alevel::all);
    echo_server.clear_access_channels(websocketpp::log::alevel::frame_payload);

    // Initialize Asio
    echo_server.init_asio();

    // Register our message handler
    echo_server.set_message_handler(
        bind(&on_message, &echo_server, ::_1, ::_2));

    // Listen on port 9002
    echo_server.listen(9002);

    // Start the server accept loop
    echo_server.start_accept();

    // Start the ASIO io_service run loop
    echo_server.run();
  } catch (websocketpp::exception const &e) {
    std::cout << e.what() << std::endl;
  } catch (...) {
    std::cout << "other exception" << std::endl;
  }
}
