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

using namespace std;

mutex mtx;

void tcpsend(FILE *fp, const char *address, int port, int buffersize,
             int count);
void udpsend(FILE *fp, const char *address, int port, int buffersize,
             int count);

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
  cout << "size of timespec" << sizeof(struct timespec) << endl;
  cout << "size of int" << sizeof(int) << endl;
  FILE *fp;
  const char *filenamech = filename.c_str();
  fp = fopen(filenamech, "rb");

  clock_gettime(CLOCK_REALTIME, &(timeReg[0]));

  vector<thread> threads;
  threads.reserve(num_thread);
  // スレッドを生成して実行開始

  for (size_t i = 0; i < num_thread; ++i) {
    if (protocol == "udp") {
      threads.emplace_back(
          thread(udpsend, fp, address, port, buffersize, count));
    } else {
      threads.emplace_back(
          thread(tcpsend, fp, address, port, buffersize, count));
    }
  }

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

void tcpsend(FILE *fp, const char *address, int port, int buffersize,
             int count) {

  // struct timespec waitTime;
  // waitTime.tv_sec = 0;
  // waitTime.tv_nsec = 1e8;

  struct sockaddr_in addr;
  addr.sin_family = AF_INET;                 // IPv4を指定
  addr.sin_port = htons(port);               //ポート番号
  addr.sin_addr.s_addr = inet_addr(address); //サーバー側のアドレス
  ssize_t send_status;
  char rebuf[buffersize];
  for (int c_i = 0; c_i < count; c_i++) {
    int sock_df;
    mtx.lock();
    size_t fr_err = fread(rebuf, 1, buffersize, fp);
    mtx.unlock();
    if (fr_err < 0) {
      perror("file read error");
    }
    sock_df = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_df < 0) {
      perror("Couldn't make a socket");
    }
    if (connect(sock_df, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
      perror("connect error");
    }
    send_status = write(sock_df, rebuf, buffersize);
    if (send_status < 0) {
      perror("send error");
    }
    // clock_nanosleep(CLOCK_REALTIME, 0, &waitTime, NULL);
    close(sock_df);
  }
}
void udpsend(FILE *fp, const char *address, int port, int buffersize,
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
  ssize_t send_status;
  char rebuf[buffersize];
  for (int c_i = 0; c_i < count; c_i++) {
    mtx.lock();
    size_t fr_err = fread((rebuf + 20), 1, buffersize - 20,
                          fp); // sizeof int=4, sizeof struct timespec 16
    if (fr_err < 0) {
      perror("file read error");
    }
    mtx.unlock();
    *((int *)rebuf) = c_i;
    char *timespec_start = rebuf + 4;
    clock_gettime(CLOCK_REALTIME, (struct timespec *)timespec_start);
    send_status = sendto(sock_df, rebuf, buffersize, 0,
                         (struct sockaddr *)&addr, sizeof(addr));
    if (send_status < 0) {
      perror("send error");
    }
    // clock_nanosleep(CLOCK_REALTIME, 0, &waitTime, NULL);
  }
  close(sock_df);
}
