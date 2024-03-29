//
// Created by realbro on 1/6/20.
//

#include "UnixDomainSocketClient.h"
#include "FileDescriptorTool.h"
#include "EpollWrapper.h"
#include "SocketWrapper.h"
#include "Message.h"

#include <iostream>
#include <chrono>
#include <thread>
#include <string.h>
#include <unistd.h>

#include <sys/epoll.h>
#include <sys/ioctl.h>

namespace {
constexpr char kFILE_NAME[] = "/tmp/test_server";
constexpr int kMAX_EVENT_COUNT = 3;
constexpr int kMAX_READ_SIZE = 2048;
}

UnixDomainSocketClient::UnixDomainSocketClient() : stopped_(false),
                                                   isConnected_(false),
                                                   isEpollAdded_(false),
                                                   product_code_(-1),
                                                   epoll_fd_(-1),
                                                   client_socket_fd_(-1),
                                                   message_id(0) {
}

UnixDomainSocketClient::~UnixDomainSocketClient() {
  Finalize();
}

bool UnixDomainSocketClient::Initialize(int &product_code, t_ListenerCallbackProc callback) {
  product_code_ = product_code;
  async_message_callback_ = callback;
  memset(&server_addr_, 0, sizeof(server_addr_));
  server_addr_.sun_family = AF_UNIX;
  strcpy(server_addr_.sun_path, kFILE_NAME);

  if (!SocketWrapper::Create(client_socket_fd_)) {
    std::cout << "Socket Create Failed" << std::endl;
    return false;
  }
  if (!EpollWrapper::EpollCreate(1, true, epoll_fd_)) {
    std::cout << "Epoll Create Failed" << std::endl;
    return false;
  }

  std::thread main_thread(&UnixDomainSocketClient::MainHandler, this);
  main_thread.detach();

  return true;
}

bool UnixDomainSocketClient::Finalize() {
  stopped_ = true;
  //Thread 종료부분이 Graceful 하지 아니함.
  close(client_socket_fd_);
  close(epoll_fd_);
}

void *UnixDomainSocketClient::MainHandler(void *arg) {
  UnixDomainSocketClient *mgr = reinterpret_cast<UnixDomainSocketClient *>(arg);

  while (!mgr->stopped_) {
    bool restart_flag = false;

    if (0 > mgr->client_socket_fd_) { // socket을 생성하고 connection 시킴
      if (SocketWrapper::Create(mgr->client_socket_fd_)) {
        mgr->isConnected_ = true;

        std::cout << "Socket Connect OK" << std::endl;
      } else {
        std::cout << "Socket Create Failed" << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        continue;
      }
    }

    if (!mgr->isConnected_) { // socket을 생성하고 connection 시킴
      if (SocketWrapper::Connect(mgr->client_socket_fd_, mgr->server_addr_)) {
        mgr->isConnected_ = true;
        FileDescriptorTool::SetNonBlock(mgr->client_socket_fd_, true);
        char buffer[10] = {0,};
        sprintf(buffer, "product:%d", mgr->product_code_);

        write(mgr->client_socket_fd_, buffer, sizeof(buffer));
        std::cout << "Socket Connect OK" << std::endl;
      } else {
        std::cout << "Socket Connect Failed" << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        continue;
      }
    }
    if (!mgr->isEpollAdded_) { // Epoll 등록
      std::cout << "EPOLL VALUE : " << mgr->epoll_fd_ << "Remake!!" << std::endl;
      if (EpollWrapper::EpollControll(mgr->epoll_fd_,
                                      mgr->client_socket_fd_,
                                      EPOLLIN | EPOLLOUT | EPOLLERR | EPOLLET,
                                      EPOLL_CTL_ADD)) {
        std::cout << "~~~~~epoll controll success~~~~~~~" << std::endl;
        mgr->isEpollAdded_ = true;
      } else {
        std::cout << "~~~~~epoll failed!!!!!!!!!!!!!!!!!!~~~~~~~" << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        continue;
      }
    }
    // 정상적으로 처리 될경우
    epoll_event gettingEvent[kMAX_EVENT_COUNT] = {0, {0}};
    int event_count = epoll_wait(mgr->epoll_fd_, gettingEvent, kMAX_EVENT_COUNT, 10 /* ms */);

    if (event_count > 0) { // 이벤트 발생, event_count = 0 처리안함.
      for (int i = 0; i < event_count; ++i) {
        //printf("event count [%d], event type [%d], from [%d]\n", i, gettingEvent[i].events, gettingEvent[i].data.fd);
        if (gettingEvent[i].data.fd == mgr->client_socket_fd_)    // 듣기 소켓에서 이벤트가 발생함
        {
          int client_fd = mgr->client_socket_fd_;
          char message[kMAX_READ_SIZE] = {0,};
          int read_size = read(client_fd, message, sizeof(message));
          if (read_size < 0) { // continue
            continue;
          }
          if (read_size == 0) { // Error or Disconnect
            std::cout << "Disconnect Event !!!!!!!!!" << std::endl;
            restart_flag = true;
            break;
          }
          // 정상
          Message msg(reinterpret_cast<const uint8_t *>(message), read_size); // 메세지 제작
          char read_more[kMAX_READ_SIZE] = {0,};
          int read_more_size = 0;
          do  // read more
          {
            memset(read_more, 0x00, sizeof(read_more));
            read_more_size = read(client_fd, read_more, sizeof(read_more));
            if (read_more_size > 0) {
              msg.AppendData(read_more, read_more_size); // 메세지 제작
            }
          } while (read_more_size > 0);
          int32_t server_code = 0x00;

          MessageParser parser(msg);
          if (parser.IsHeader()) { // header 정보가 있을경우
            mgr->message_manager_.Add(server_code, msg);
          } else {  // communicated product type
            if (mgr->message_manager_.IsExistMessage(server_code)) {
              Message msg_need_append = mgr->message_manager_.MessagePop(server_code);
              msg_need_append.AppendData(reinterpret_cast<char *>(&msg.GetRawData()[0]), msg.GetMessageSize());
              mgr->message_manager_.Add(server_code, msg_need_append);
            }
          }

          if (mgr->message_manager_.IsPerfectMessage(server_code)) {
            Message msg_complete = mgr->message_manager_.MessagePop(server_code);
            MessageParser msg_parser(msg_complete);
            printf("*****msg size1 : [%d]*****\n", msg_complete.GetMessageSize());
            printf("*****msg size2 : [%d]*****\n", msg_parser.GetDataSize());
            int msgid = msg_parser.GetMessageId();
            for (auto &it : mgr->response_msg_id) {   // response 를 기다려야 하는 메세지들
              if (it.first == msgid) {
                printf("match case!!!");
                mgr->need_response_message_[msgid] = msg_complete;
                break;
              }
            }
            if (mgr->async_message_callback_) {
              printf("*****callback function called*****\n");
              mgr->async_message_callback_(msg_complete);
            }
            printf("*****Message Done*****\n");
          }

        }
      }
    }
    if (event_count < 0 || restart_flag) {  // epoll 상에서 문제가 발생할 경우 혹은 쓰레드 재기동이 필요한경우. 조건문이 좀 이상함 바로 위 조건문이랑 맞출것
      std::cout << "***********Error BREAK Restart!!!!" << std::endl;
      EpollWrapper::EpollControll(mgr->epoll_fd_,
                                  mgr->client_socket_fd_,
                                  EPOLLIN | EPOLLOUT | EPOLLERR | EPOLLET,
                                  EPOLL_CTL_DEL);
      close(mgr->client_socket_fd_);

      mgr->client_socket_fd_ = -1;
      mgr->isEpollAdded_ = false;
      mgr->isConnected_ = false;
      continue;
    }
  }
  std::cout << "Client EpollHandler Thread!!!! END OF Thread" << std::endl;
  pthread_exit(NULL);
}

bool UnixDomainSocketClient::WriteMessage(const int write_fd, const void *data_, const ssize_t data_size) {
  bool result = true;
  int next_post = 0;
  while (next_post < data_size) {
    ssize_t write_result_size = write(write_fd, data_ + next_post, data_size - next_post);
    if (0 > write_result_size) {
      usleep(100);
      continue;
    } else { // 처리
      next_post += write_result_size;
    }
  }
  return result;
}

bool UnixDomainSocketClient::SendMessage(std::string &send_string) {
  bool result = false;
  if (isConnected_) {
    Message send_msg(send_string.c_str(), send_string.length(), 1, ++message_id);
    ssize_t data_size = send_msg.GetRawData().size();
    std::cout << " send Length : " << send_msg.GetRawData().size() << std::endl;
    if (WriteMessage(client_socket_fd_, &send_msg.GetRawData()[0], data_size)) result = true;
  }
  return result;
}

bool UnixDomainSocketClient::SendMessage(int msg_id, std::string &send_string) {
  bool result = false;
  if (isConnected_) {
    Message send_msg(send_string.c_str(), send_string.length(), 1, msg_id);
    ssize_t data_size = send_msg.GetRawData().size();
    std::cout << " send Length : " << send_msg.GetRawData().size() << std::endl;
    if (WriteMessage(client_socket_fd_, &send_msg.GetRawData()[0], data_size)) result = true;
  }
  return result;
}

std::string UnixDomainSocketClient::SendMessage(std::string &send_string, int wait_time) {
  std::string result;
  if (isConnected_) {
    Message send_msg(send_string.c_str(), send_string.length(), 1, ++message_id);
    ssize_t data_size = send_msg.GetRawData().size();
    std::cout << " send Length : " << send_msg.GetRawData().size() << std::endl;
    response_msg_id[message_id];
    if (WriteMessage(client_socket_fd_, &send_msg.GetRawData()[0], data_size)) {
      while (wait_time > 0) {
        if (need_response_message_.count(message_id)) {
          MessageParser msg_parser(need_response_message_[message_id]);
          int data_size = msg_parser.GetDataSize();
          const char *start_pos = reinterpret_cast<const char *>(msg_parser.GetData());
          result.assign(start_pos, data_size);
          break;
        } else {
          usleep(1000);
          wait_time--;
        }
      }
      response_msg_id.erase(message_id);
    }
  }
  return result;
}